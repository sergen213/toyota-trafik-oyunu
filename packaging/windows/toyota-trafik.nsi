; Per-user installer (no administrator rights) for the native Toyota Trafik Oyunu port.
; Built by scripts/package_release.sh: makensis -DVERSION=x.y.z -DSTAGE=<staged files> -DUNINSTALL_LIST=<file> -DOUTFILE=<exe>
; Contains no game files; the game asks for the user's installed folder, setup CD folder or setup .rar on first start.
Unicode true
SetCompressor /SOLID lzma
RequestExecutionLevel user

!include "MUI2.nsh"

Name "Toyota Trafik Oyunu ${VERSION}"
OutFile "${OUTFILE}"
InstallDir "$LOCALAPPDATA\Programs\Toyota Trafik Oyunu"
InstallDirRegKey HKCU "Software\ToyotaTrafikOyunuNative" "InstallDir"
!define UNINST_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\ToyotaTrafikOyunuNative"

!define MUI_ICON "${STAGE}\toyota-trafik.ico"
!define MUI_UNICON "${STAGE}\toyota-trafik.ico"
!define MUI_ABORTWARNING
!define MUI_FINISHPAGE_RUN "$INSTDIR\toyota-trafik.exe"
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "${STAGE}\LEGAL.md"
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "Turkish"
!insertmacro MUI_LANGUAGE "English"

VIProductVersion "${VERSION}.0"
VIAddVersionKey /LANG=${LANG_ENGLISH} "ProductName" "Toyota Trafik Oyunu (native port)"
VIAddVersionKey /LANG=${LANG_ENGLISH} "FileDescription" "Toyota Trafik Oyunu native port setup"
VIAddVersionKey /LANG=${LANG_ENGLISH} "FileVersion" "${VERSION}"
VIAddVersionKey /LANG=${LANG_ENGLISH} "ProductVersion" "${VERSION}"
VIAddVersionKey /LANG=${LANG_ENGLISH} "LegalCopyright" "Port code: project contributors. Game content not included."

Section "Install"
  SetOutPath "$INSTDIR"
  File /r "${STAGE}\*.*"
  WriteUninstaller "$INSTDIR\uninstall.exe"
  CreateDirectory "$SMPROGRAMS\Toyota Trafik Oyunu"
  CreateShortcut "$SMPROGRAMS\Toyota Trafik Oyunu\Toyota Trafik Oyunu.lnk" "$INSTDIR\toyota-trafik.exe" "" "$INSTDIR\toyota-trafik.ico"
  CreateShortcut "$SMPROGRAMS\Toyota Trafik Oyunu\Uninstall.lnk" "$INSTDIR\uninstall.exe"
  CreateShortcut "$DESKTOP\Toyota Trafik Oyunu.lnk" "$INSTDIR\toyota-trafik.exe" "" "$INSTDIR\toyota-trafik.ico"
  WriteRegStr HKCU "Software\ToyotaTrafikOyunuNative" "InstallDir" "$INSTDIR"
  WriteRegStr HKCU "${UNINST_KEY}" "DisplayName" "Toyota Trafik Oyunu (native port)"
  WriteRegStr HKCU "${UNINST_KEY}" "DisplayVersion" "${VERSION}"
  WriteRegStr HKCU "${UNINST_KEY}" "DisplayIcon" "$INSTDIR\toyota-trafik.ico"
  WriteRegStr HKCU "${UNINST_KEY}" "InstallLocation" "$INSTDIR"
  WriteRegStr HKCU "${UNINST_KEY}" "UninstallString" '"$INSTDIR\uninstall.exe"'
  WriteRegDWORD HKCU "${UNINST_KEY}" "NoModify" 1
  WriteRegDWORD HKCU "${UNINST_KEY}" "NoRepair" 1
SectionEnd

; Removes exactly the installed files (generated list), never the whole folder: the user may have chosen a shared one.
; Profiles and imported game data in %APPDATA%\toyota-trafik are kept.
Section "Uninstall"
  !include "${UNINSTALL_LIST}"
  Delete "$INSTDIR\uninstall.exe"
  RMDir "$INSTDIR"
  Delete "$DESKTOP\Toyota Trafik Oyunu.lnk"
  RMDir /r "$SMPROGRAMS\Toyota Trafik Oyunu"
  DeleteRegKey HKCU "${UNINST_KEY}"
  DeleteRegKey HKCU "Software\ToyotaTrafikOyunuNative"
SectionEnd
