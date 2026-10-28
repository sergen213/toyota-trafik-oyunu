# Building and installing

## Dependencies (distribution packages)

```sh
sudo pacman -S --needed base-devel cmake ninja git sdl3 sdl3_ttf glew glm mpg123 fontconfig stb nlohmann-json gtest noto-fonts \
    vulkan-headers vulkan-icd-loader shaderc ffmpeg python libarchive
# optional: qt6-base (builds toyota-trafik-launcher; skipped when Qt 6 is missing)
# optional: vulkan-validation-layers (VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation)
```

ODE is not packaged with trimesh support on Arch. CMake therefore uses the system `ode` when pkg-config finds it,
and otherwise builds the pinned source tarball `third_party/ode-0.16.6.tar.gz` (SHA-256 checked, BSD licence).
No network access is needed. Versions used during development are listed in `ENVIRONMENT.md`.

## Build

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
ninja -C build
```

Options: `-DTTO_BUILD_TESTS=OFF`, `-DTTO_BUILD_GAME=OFF` (parsers and tests only; no SDL/GL needed).

## Install

```sh
cmake --install build --prefix ~/.local   # installs bin/toyota-trafik and share/toyota-trafik/maps.json
```

The binary looks for `maps.json` next to itself, in `../data` and in `../share/toyota-trafik`. Use `--data <dir>`
to override.

## Game data

Install the game with Wine or copy the folder straight from the CD:
`Kurulum2/program files/ToyotaSA/Toyota Trafik Oyunu v2/` (v2) or `Kurulum1/.../Toyota Trafik Oyunu/` (v1).
The CD folder can be used as-is, because the MSI only copies files. Pass the folder with `--game` or set
`TTO_GAME_DIR`; or let the program import it: `--import <CD folder | Kurulum folder | setup .rar>` copies the game
tree (v2 preferred) into `<profile dir>/game`. Without either, it tries the remembered folder, the default install
folders (`C:\Program Files (x86)\ToyotaSA\…`, `~/.wine/drive_c/…`) and then asks with a dialog. The game folder is
only read, never written. Profiles live in the SDL preference folder (`$XDG_DATA_HOME/toyota-trafik/UserData.dat`,
`%APPDATA%\toyota-trafik\UserData.dat`; same byte format as the original).

## Release artifacts (AppImage, Windows installer and zip)

```sh
scripts/package_release.sh            # version from CMakeLists.txt; output in dist/ with .sha256 files
```

- **AppImage**: `linuxdeploy.appimage` collects the libraries; the untouched Arch copies are then put back (its
  patchelf corrupts current Arch libraries) and `AppRun` sets `LD_LIBRARY_PATH` and `QT_PLUGIN_PATH`; `appimagetool`
  packs it. The Vulkan loader, glibc and GPU drivers come from the host. `AppRun` starts the Qt launcher without
  arguments and the game with arguments (or `TTO_APPIMAGE_ENTRY=toyota-trafik`).
- **Windows**: MinGW-w64 cross build (`cmake/mingw-w64.cmake`) against an MSYS2 **ucrt64** prefix (same CRT as Arch's
  mingw-w64 toolchain). Create the prefix without root:

  ```sh
  # pacman.conf: [options] Architecture=any, SigLevel=Never, DBPath/CacheDir under work/w64u;
  #              [ucrt64] Server = https://repo.msys2.org/mingw/ucrt64
  unshare -r pacman --config work/w64u/pacman.conf --root work/w64u -Sy --noscriptlet \
    mingw-w64-ucrt-x86_64-{sdl3,sdl3-ttf,glew,vulkan-loader,vulkan-headers,mpg123,fontconfig,ffmpeg,glm,nlohmann-json,pkgconf,libarchive,nsis}
  ```

  The script bundles every imported DLL found in the prefix (failing on unknown ones), never the Vulkan loader or
  OpenGL, plus the licences of the bundled MSYS2 packages. Check under Wine:
  `wine toyota-trafik.exe --import 'Z:\…\setup.rar' --check`.

## Running the original instead (reference / comparison)

```sh
scripts/wine_recover.sh setup "<game folder>" ~/tto-wine
scripts/wine_recover.sh run ~/tto-wine
```

- **Windows installer**: `packaging/windows/toyota-trafik.nsi`, compiled by MSYS2's `makensis.exe` under Wine (add
  `mingw-w64-ucrt-x86_64-nsis` to the prefix). Per-user install into `%LOCALAPPDATA%\Programs\Toyota Trafik Oyunu`;
  the uninstaller deletes exactly the installed files (generated list) and keeps `%APPDATA%\toyota-trafik`. Check:
  `wine …-Setup.exe /S`, run the installed exe, then `wine …\uninstall.exe /S`.
