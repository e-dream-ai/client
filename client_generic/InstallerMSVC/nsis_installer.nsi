; Infinidream Windows installer (NSIS 3.x, MUI2, Unicode, x64).
; Invoke:
;   makensis /DPRODUCT_VERSION=X.Y.Z [/DSOURCE_DIR=..\MSVC\Release] [/DOUT_FILE=...] nsis_installer.nsi

Unicode true
SetCompressor /SOLID lzma

!ifndef PRODUCT_VERSION
  !define PRODUCT_VERSION "0.0.0"
!endif
!ifndef SOURCE_DIR
  !define SOURCE_DIR "..\MSVC\Release"
!endif
!ifndef OUT_FILE
  !define OUT_FILE "infinidream-windows-${PRODUCT_VERSION}-setup.exe"
!endif

!define PRODUCT_NAME      "Infinidream"
!define PRODUCT_PUBLISHER "infinidream.ai"
!define PRODUCT_WEB_SITE  "https://infinidream.ai"
!define PRODUCT_EXE       "infinidream.exe"
!define PRODUCT_SCR       "infinidream.scr"
!define PRODUCT_DIR_REGKEY  "Software\Microsoft\Windows\CurrentVersion\App Paths\${PRODUCT_EXE}"
!define PRODUCT_UNINST_KEY  "Software\Microsoft\Windows\CurrentVersion\Uninstall\${PRODUCT_NAME}"

Name "${PRODUCT_NAME} ${PRODUCT_VERSION}"
OutFile "${OUT_FILE}"
InstallDir "$PROGRAMFILES64\${PRODUCT_NAME}"
InstallDirRegKey HKLM "${PRODUCT_DIR_REGKEY}" ""
RequestExecutionLevel admin
ShowInstDetails show
ShowUnInstDetails show

VIProductVersion "${PRODUCT_VERSION}.0"
VIAddVersionKey "ProductName"     "${PRODUCT_NAME}"
VIAddVersionKey "CompanyName"     "${PRODUCT_PUBLISHER}"
VIAddVersionKey "ProductVersion"  "${PRODUCT_VERSION}"
VIAddVersionKey "FileVersion"     "${PRODUCT_VERSION}"
VIAddVersionKey "FileDescription" "${PRODUCT_NAME} setup"
VIAddVersionKey "LegalCopyright"  "${PRODUCT_PUBLISHER}"

!include "MUI2.nsh"
!include "WinVer.nsh"
!include "x64.nsh"

!define MUI_ABORTWARNING
!define MUI_ICON   "..\Client\app.ico"
!define MUI_UNICON "..\Client\app.ico"

!define MUI_FINISHPAGE_RUN "$INSTDIR\${PRODUCT_EXE}"
!define MUI_FINISHPAGE_RUN_NOTCHECKED

!define MUI_FINISHPAGE_SHOWREADME ""
!define MUI_FINISHPAGE_SHOWREADME_NOTCHECKED
!define MUI_FINISHPAGE_SHOWREADME_TEXT "Set Infinidream as current screensaver"
!define MUI_FINISHPAGE_SHOWREADME_FUNCTION SetAsCurrentScreensaver

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "..\RuntimeMSVC\License.rtf"
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_LANGUAGE "English"

Function .onInit
  ${IfNot} ${RunningX64}
    MessageBox MB_ICONSTOP "${PRODUCT_NAME} requires 64-bit Windows."
    Abort
  ${EndIf}
  ${IfNot} ${AtLeastWin10}
    MessageBox MB_ICONSTOP "${PRODUCT_NAME} requires Windows 10 or newer."
    Abort
  ${EndIf}
  SetRegView 64
FunctionEnd

Function un.onInit
  MessageBox MB_ICONQUESTION|MB_YESNO|MB_DEFBUTTON2 \
    "Are you sure you want to remove $(^Name) and all of its components?" IDYES +2
  Abort
  SetRegView 64
FunctionEnd

Section "MainSection" SEC01
  SetOverwrite on
  SetOutPath "$INSTDIR"

  ; Binaries, runtime DLLs, fonts, images — whatever landed in MSVC\Release\.
  File "${SOURCE_DIR}\${PRODUCT_EXE}"
  File "${SOURCE_DIR}\${PRODUCT_SCR}"
  File "${SOURCE_DIR}\*.dll"
  File "${SOURCE_DIR}\*.png"
  File "${SOURCE_DIR}\*.ttf"

  ; Start Menu shortcuts
  SetShellVarContext all
  CreateDirectory "$SMPROGRAMS\${PRODUCT_NAME}"
  CreateShortCut "$SMPROGRAMS\${PRODUCT_NAME}\${PRODUCT_NAME}.lnk"              "$INSTDIR\${PRODUCT_EXE}"
  CreateShortCut "$SMPROGRAMS\${PRODUCT_NAME}\${PRODUCT_NAME} (windowed).lnk"   "$INSTDIR\${PRODUCT_EXE}" "-X"
  CreateShortCut "$SMPROGRAMS\${PRODUCT_NAME}\Uninstall ${PRODUCT_NAME}.lnk"    "$INSTDIR\uninst.exe"

  WriteIniStr "$INSTDIR\${PRODUCT_NAME}.url" "InternetShortcut" "URL" "${PRODUCT_WEB_SITE}"
  CreateShortCut "$SMPROGRAMS\${PRODUCT_NAME}\Website.lnk" "$INSTDIR\${PRODUCT_NAME}.url"
  SetShellVarContext current
SectionEnd

Section -Post
  WriteUninstaller "$INSTDIR\uninst.exe"

  WriteRegStr HKLM "${PRODUCT_DIR_REGKEY}" "" "$INSTDIR\${PRODUCT_EXE}"
  WriteRegStr HKLM "Software\${PRODUCT_NAME}" "InstallDir" "$INSTDIR"

  WriteRegStr HKLM "${PRODUCT_UNINST_KEY}" "DisplayName"     "$(^Name)"
  WriteRegStr HKLM "${PRODUCT_UNINST_KEY}" "DisplayVersion"  "${PRODUCT_VERSION}"
  WriteRegStr HKLM "${PRODUCT_UNINST_KEY}" "DisplayIcon"     "$INSTDIR\${PRODUCT_EXE}"
  WriteRegStr HKLM "${PRODUCT_UNINST_KEY}" "Publisher"       "${PRODUCT_PUBLISHER}"
  WriteRegStr HKLM "${PRODUCT_UNINST_KEY}" "URLInfoAbout"    "${PRODUCT_WEB_SITE}"
  WriteRegStr HKLM "${PRODUCT_UNINST_KEY}" "InstallLocation" "$INSTDIR"
  WriteRegStr HKLM "${PRODUCT_UNINST_KEY}" "UninstallString" "$\"$INSTDIR\uninst.exe$\""
  WriteRegDWORD HKLM "${PRODUCT_UNINST_KEY}" "NoModify" 1
  WriteRegDWORD HKLM "${PRODUCT_UNINST_KEY}" "NoRepair" 1
SectionEnd

Function SetAsCurrentScreensaver
  WriteRegStr HKCU "Control Panel\Desktop" "ScreenSaveActive" "1"
  WriteRegStr HKCU "Control Panel\Desktop" "SCRNSAVE.EXE"     "$INSTDIR\${PRODUCT_SCR}"
FunctionEnd

Section Uninstall
  SetShellVarContext all

  ; Only reset the screensaver pointer if it's still pointing at us.
  ReadRegStr $0 HKCU "Control Panel\Desktop" "SCRNSAVE.EXE"
  StrCmp $0 "$INSTDIR\${PRODUCT_SCR}" 0 +2
    DeleteRegValue HKCU "Control Panel\Desktop" "SCRNSAVE.EXE"

  MessageBox MB_YESNO|MB_ICONQUESTION \
    "Keep your downloaded content and logs under %ProgramData%\e-dream?$\r$\n$\r$\nChoose Yes to keep, No to delete." \
    IDYES SkipDataDelete
    RMDir /r "$PROGRAMDATA\e-dream"
  SkipDataDelete:

  Delete "$SMPROGRAMS\${PRODUCT_NAME}\${PRODUCT_NAME}.lnk"
  Delete "$SMPROGRAMS\${PRODUCT_NAME}\${PRODUCT_NAME} (windowed).lnk"
  Delete "$SMPROGRAMS\${PRODUCT_NAME}\Uninstall ${PRODUCT_NAME}.lnk"
  Delete "$SMPROGRAMS\${PRODUCT_NAME}\Website.lnk"
  RMDir  "$SMPROGRAMS\${PRODUCT_NAME}"

  Delete "$INSTDIR\${PRODUCT_NAME}.url"
  Delete "$INSTDIR\uninst.exe"
  RMDir /r "$INSTDIR"

  DeleteRegKey HKLM "${PRODUCT_UNINST_KEY}"
  DeleteRegKey HKLM "${PRODUCT_DIR_REGKEY}"
  DeleteRegKey HKLM "Software\${PRODUCT_NAME}"

  SetShellVarContext current
  SetAutoClose true
SectionEnd
