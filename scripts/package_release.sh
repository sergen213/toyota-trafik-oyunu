#!/usr/bin/env bash
# Build release artifacts into dist/:
#   ToyotaTrafik-<ver>-x86_64.AppImage     (linuxdeploy; built on this host, so it needs a glibc at least as new)
#   ToyotaTrafik-<ver>-windows-x64.zip     (MinGW-w64 cross build against an MSYS2 ucrt64 prefix, DLLs bundled)
# Usage: scripts/package_release.sh [version]   env: TTO_W64_ROOT=<msys2 ucrt64 prefix> (default work/w64u/ucrt64)
# No game files are read or packaged.
set -euo pipefail
cd "$(dirname "$0")/.."
ROOT=$PWD
VER=${1:-$(sed -nE 's/^project\([a-z_]+ VERSION ([0-9.]+).*/\1/ip' CMakeLists.txt)}
W64=${TTO_W64_ROOT:-$ROOT/work/w64u/ucrt64}
DIST=$ROOT/dist
LINUXDEPLOY=${LINUXDEPLOY:-linuxdeploy.appimage}  # the AppImage build bundles patchelf and the appimage plugin
export NO_STRIP=1  # its bundled strip cannot read .relr.dyn of current Arch libraries
mkdir -p "$DIST"
LICENSES=(third_party/FidelityFX-SDK/LICENSE.txt third_party/ode-LICENSE-BSD.TXT)

# --- Linux AppImage ---
cmake -S . -B build-appimage -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr -DTTO_BUILD_TESTS=OFF
ninja -C build-appimage
rm -rf build-appimage/AppDir
DESTDIR=$ROOT/build-appimage/AppDir cmake --install build-appimage
install -Dm644 README.md docs/LEGAL.md "${LICENSES[@]}" -t build-appimage/AppDir/usr/share/doc/toyota-trafik/
rm -f "$DIST"/ToyotaTrafik-*-x86_64.AppImage
cd build-appimage
"$LINUXDEPLOY" --appdir AppDir --exclude-library "libvulkan.so*" -e AppDir/usr/bin/toyota-trafik -d "$ROOT/data/toyota-trafik.desktop" -i "$ROOT/data/toyota-trafik.svg"
# Licence texts of every Arch package that contributed a bundled library.
for so in AppDir/usr/lib/*.so*; do pacman -Qqo "/usr/lib/$(basename "$so")" 2>/dev/null || true; done | sort -u >AppDir/usr/share/doc/toyota-trafik/bundled-packages.txt
while read -r p; do
  if [[ -d /usr/share/licenses/$p ]]; then cp -r "/usr/share/licenses/$p" AppDir/usr/share/doc/toyota-trafik/licenses-"$p"; fi
done <AppDir/usr/share/doc/toyota-trafik/bundled-packages.txt
# linuxdeploy's bundled patchelf corrupts current Arch libraries (crash in ld.so): restore the untouched copies and
# resolve bundled libraries through LD_LIBRARY_PATH instead of rewritten RPATHs.
for so in AppDir/usr/lib/*.so*; do cp -f "/usr/lib/$(basename "$so")" "$so"; done
rm -f AppDir/AppRun
cat >AppDir/AppRun <<'EOF'
#!/bin/sh
HERE=$(dirname "$(readlink -f "$0")")
export LD_LIBRARY_PATH="$HERE/usr/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
exec "$HERE/usr/bin/toyota-trafik" "$@"
EOF
chmod +x AppDir/AppRun
ARCH=x86_64 VERSION=$VER appimagetool -n AppDir "$DIST/ToyotaTrafik-$VER-x86_64.AppImage"
cd "$ROOT"

# --- Windows zip ---
export TTO_W64_ROOT=$W64  # read by cmake/mingw-w64.cmake, also inside try_compile
cmake -S . -B build-win -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64.cmake \
  -DCMAKE_BUILD_TYPE=Release -DTTO_BUILD_TESTS=OFF -DSTB_INCLUDE_DIR=/usr/include/stb
ninja -C build-win
STAGE=build-win/ToyotaTrafik-$VER-windows-x64
rm -rf "$STAGE" && mkdir -p "$STAGE/data"
x86_64-w64-mingw32-strip -o "$STAGE/toyota-trafik.exe" build-win/toyota-trafik.exe
cp data/maps.json "$STAGE/data/"
cp README.md docs/LEGAL.md "${LICENSES[@]}" data/toyota-trafik.ico "$STAGE/"
# Fontconfig needs its config next to the DLLs (fonts come from %WINDIR%\Fonts).
mkdir -p "$STAGE/etc" && cp -r "$W64/etc/fonts" "$STAGE/etc/"
# DLL closure: every import found in the MSYS2 prefix is bundled; anything else must be a Windows/driver DLL.
system_dll() { [[ ${1,,} =~ ^(api-ms-win-|ext-ms-win-) || ${1,,} =~ ^(kernel32|user32|gdi32|advapi32|shell32|ole32|oleaut32|opengl32|vulkan-1|winmm|imm32|setupapi|version|cfgmgr32|hid|ws2_32|bcrypt|ncrypt|crypt32|secur32|dnsapi|iphlpapi|dwmapi|shlwapi|comdlg32|comctl32|uxtheme|dxgi|d3d11|d3d12|dwrite|mfplat|mf|mfreadwrite|mfuuid|propsys|psapi|rpcrt4|userenv|winhttp|wldap32|normaliz|msvcrt|ucrtbase|ntdll|powrprof|usp10|mscms|avicap32|msacm32|evr|d3d9|dsound|xinput1_4|wintrust|winspool\.drv|ole32|shcore|wtsapi32|authz|netapi32|mpr|pdh|dbghelp|gdiplus|windowscodecs|d2d1|dcomp|wsock32|msimg32|winhttp|mswsock|odbc32|glu32|comdlg32|dbgeng|esent|ksuser|mfcore|avrt|ole32|shell32|oleacc|wininet|urlmon|cryptnet|dhcpcsvc|rtworkq|xinput9_1_0|xinput1_3|bcryptprimitives|crypt32|secur32|xmllite)\.dll$ ]]; }
pending=("$STAGE/toyota-trafik.exe"); declare -A seen
while ((${#pending[@]})); do
  pe=${pending[-1]}; unset 'pending[-1]'
  for dll in $(x86_64-w64-mingw32-objdump -p "$pe" | sed -n 's/.*DLL Name: //p'); do
    key=${dll,,}; [[ -n ${seen[$key]:-} ]] && continue; seen[$key]=1
    system_dll "$dll" && continue  # provided by Windows or the GPU driver (vulkan-1, opengl32): never bundled
    if [[ -f $W64/bin/$dll ]]; then cp "$W64/bin/$dll" "$STAGE/"; pending+=("$STAGE/$dll")
    else echo "unresolved DLL: $dll (from $pe)" >&2; exit 1; fi
  done
done
# Licences and exact versions of the bundled MSYS2 packages (sources: https://github.com/msys2/MINGW-packages).
mkdir -p "$STAGE/licenses"
for dll in "$STAGE"/*.dll; do
  grep -l "^ucrt64/bin/$(basename "$dll")\$" "$W64"/../var/lib/pacman/local/*/files | xargs -r -n1 dirname | xargs -r -n1 basename || true
done | sort -u >"$STAGE/licenses/bundled-msys2-packages.txt"
while read -r p; do
  n=${p%-*-*}; n=${n#mingw-w64-ucrt-x86_64-}
  find "$W64/share/licenses" -mindepth 1 -maxdepth 1 -iname "$n" -exec cp -r {} "$STAGE/licenses/" \;
done <"$STAGE/licenses/bundled-msys2-packages.txt"
rm -f "$DIST"/ToyotaTrafik-*-windows-x64.zip "$DIST"/ToyotaTrafik-*-windows-x64-Setup.exe
(cd build-win && zip -qr9 "$DIST/ToyotaTrafik-$VER-windows-x64.zip" "$(basename "$STAGE")")

# --- Windows installer (NSIS from the MSYS2 prefix, run under Wine) ---
UNINST=build-win/uninstall.nsh
winpath() { echo "Z:$(realpath "$1" | tr / '\\')"; }
{
  (cd "$STAGE" && find . -type f | sort) | sed 's|^\./||; s|/|\\|g; s|.*|Delete "$INSTDIR\\&"|'
  (cd "$STAGE" && find . -mindepth 1 -type d | sort -r) | sed 's|^\./||; s|/|\\|g; s|.*|RMDir "$INSTDIR\\&"|'
} >"$UNINST"
SETUP=$DIST/ToyotaTrafik-$VER-windows-x64-Setup.exe
WINEPREFIX=${WINEPREFIX:-$ROOT/work/nsis-prefix} WINEDEBUG=-all wine "$W64/bin/makensis.exe" -V2 -INPUTCHARSET UTF8 \
  "-DVERSION=$VER" "-DSTAGE=$(winpath "$STAGE")" "-DUNINSTALL_LIST=$(winpath "$UNINST")" \
  "-DOUTFILE=$(winpath "$DIST")\\$(basename "$SETUP")" "$(winpath packaging/windows/toyota-trafik.nsi)"
[[ -s $SETUP ]] || { echo "installer not built" >&2; exit 1; }

# --- check: no proprietary game files, checksums ---
for a in "$DIST"/ToyotaTrafik-$VER-*; do
  case $a in *.sha256) continue ;; esac
  (cd "$DIST" && sha256sum "$(basename "$a")" >"$(basename "$a").sha256")
done
if unzip -Z1 "$DIST/ToyotaTrafik-$VER-windows-x64.zip" | grep -iE '\.(3ds|smd|vdd|avi|mp3|wav|bmp|jpg|tga)$|TrafikOyunu\.exe|SoruBank|UserData'; then
  echo "proprietary file in zip" >&2; exit 1
fi
ls -la "$DIST"
