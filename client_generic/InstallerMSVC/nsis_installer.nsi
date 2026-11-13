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
!include "FileFunc.nsh"
!include "LogicLib.nsh"

!define MUI_ABORTWARNING
!define MUI_ICON   "..\Client\app.ico"
!define MUI_UNICON "..\Client\app.ico"

!define MUI_FINISHPAGE_RUN "$INSTDIR\${PRODUCT_EXE}"
!define MUI_FINISHPAGE_RUN_TEXT "Run ${PRODUCT_NAME} and sign in"
; The installer runs elevated, so a direct CreateProcess of the app would inherit the
; elevated token. That's the bug behind the "first run as admin, then standard-user
; launches crash" report: the app would write its data files (in %LOCALAPPDATA%) to the
; admin profile, leaving the actual user with empty/missing state. Route the launch
; through Explorer's Shell.Application COM object so it spawns the app in the user's
; (non-elevated) session.
!define MUI_FINISHPAGE_RUN_FUNCTION RunAppAsUser

; Selecting Infinidream as the current screensaver is handled by the app itself on
; first launch (settings.app.keep_screensaver_enabled, on by default), not here.
; Doing it from the elevated installer would write to the wrong HKCU hive whenever
; a non-admin user installed via UAC prompt with someone else's credentials.

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "..\RuntimeMSVC\License.rtf"
!define MUI_PAGE_CUSTOMFUNCTION_LEAVE DirectoryLeave
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

; Always install into a folder of our own. If the user picks e.g. "C:\Program Files"
; or "D:\", install into "...\Infinidream" under it, so the uninstaller never has to
; touch a folder that holds anything else.
Function DirectoryLeave
  ${GetFileName} "$INSTDIR" $0
  ${If} $0 != "${PRODUCT_NAME}"
    StrCpy $1 "$INSTDIR" "" -1
    ${If} $1 == "\"
      StrCpy $INSTDIR "$INSTDIR${PRODUCT_NAME}"
    ${Else}
      StrCpy $INSTDIR "$INSTDIR\${PRODUCT_NAME}"
    ${EndIf}
  ${EndIf}
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
  SetShellVarContext all

  ; Wipe any stale %ProgramData%\Infinidream from earlier builds that stored data there.
  ; LocalAppData is now the data root (per-user, no ACL trap from the elevated installer).
  ; $APPDATA under "all" context = CSIDL_COMMON_APPDATA = %ProgramData%.
  RMDir /r "$APPDATA\Infinidream"

  ; Binaries, runtime DLLs, fonts, images — whatever landed in MSVC\Release\.
  File "${SOURCE_DIR}\${PRODUCT_EXE}"
  File "${SOURCE_DIR}\${PRODUCT_SCR}"
  File "${SOURCE_DIR}\*.dll"
  File "${SOURCE_DIR}\*.png"
  File "${SOURCE_DIR}\*.ttf"

  ; Start Menu shortcut — just the app launcher. Apps & Features (registered via
  ; PRODUCT_UNINST_KEY below) handles uninstall; the website lives in the app's About.
  CreateDirectory "$SMPROGRAMS\${PRODUCT_NAME}"
  CreateShortCut "$SMPROGRAMS\${PRODUCT_NAME}\${PRODUCT_NAME}.lnk"              "$INSTDIR\${PRODUCT_EXE}"
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

Function RunAppAsUser
  ; Drop the installer's elevation: explorer.exe started with a path hands it to the
  ; user's already-running Explorer, which runs at the user's (non-elevated) level and
  ; launches the app from there. The old way, Shell.Application from an elevated
  ; PowerShell, got an in-process (elevated) Shell object and launched the app elevated
  ; (#722 item 2). No plugin, and no hidden PowerShell for AV heuristics to flag.
  ;
  ; Windows only lets the foreground process give away the foreground, and the app is
  ; started by Explorer, so its own SetForegroundWindow would be ignored and its window
  ; would open behind the others. The installer is in the foreground (the user just
  ; clicked Finish), so let any process take it (ASFW_ANY = -1) before launching.
  System::Call 'user32::AllowSetForegroundWindow(i -1)'
  Exec `"$WINDIR\explorer.exe" "$INSTDIR\${PRODUCT_EXE}"`
FunctionEnd

Section Uninstall
  SetShellVarContext all

  ; Only reset the screensaver pointer if it's still pointing at us. Compare files, not
  ; strings: Windows' Screen Saver Settings rewrites the value in 8.3 form
  ; (C:\PROGRA~1\INFINI~1\INFINI~1.SCR) when the user clicks Apply. Both paths go through
  ; the same short-name conversion while the .scr still exists; on volumes without 8.3
  ; names both stay long. (StrCmp ignores case.)
  ReadRegStr $0 HKCU "Control Panel\Desktop" "SCRNSAVE.EXE"
  ${If} $0 != ""
    GetFullPathName /SHORT $1 "$0"
    GetFullPathName /SHORT $2 "$INSTDIR\${PRODUCT_SCR}"
    ${If} $1 == ""
      StrCpy $1 $0
    ${EndIf}
    ${If} $2 == ""
      StrCpy $2 "$INSTDIR\${PRODUCT_SCR}"
    ${EndIf}
    ${If} $1 == $2
      DeleteRegValue HKCU "Control Panel\Desktop" "SCRNSAVE.EXE"
    ${EndIf}
  ${EndIf}

  ; Per-user data lives in %LOCALAPPDATA%\Infinidream — that's the user's data, not
  ; ours to delete (standard Windows convention). The uninstaller runs elevated and
  ; would only see the admin profile's LocalAppData anyway. Clean up the legacy
  ; %ProgramData% location in case it survived from a pre-LocalAppData install.
  ; $APPDATA under "all" context (set above) = CSIDL_COMMON_APPDATA = %ProgramData%.
  RMDir /r "$APPDATA\Infinidream"

  Delete "$SMPROGRAMS\${PRODUCT_NAME}\${PRODUCT_NAME}.lnk"
  ; Legacy shortcuts from older installers — delete on uninstall so they don't orphan.
  Delete "$SMPROGRAMS\${PRODUCT_NAME}\${PRODUCT_NAME} (windowed).lnk"
  Delete "$SMPROGRAMS\${PRODUCT_NAME}\Uninstall ${PRODUCT_NAME}.lnk"
  Delete "$SMPROGRAMS\${PRODUCT_NAME}\Website.lnk"
  RMDir  "$SMPROGRAMS\${PRODUCT_NAME}"

  Delete "$INSTDIR\${PRODUCT_NAME}.url"
  Delete "$INSTDIR\uninst.exe"
  ; Remove the whole folder only if it's ours (the installer always ends the path in
  ; \Infinidream; older installers didn't). Otherwise remove just what we install, and
  ; the folder only if that leaves it empty.
  ${GetFileName} "$INSTDIR" $0
  ${If} $0 == "${PRODUCT_NAME}"
    RMDir /r "$INSTDIR"
  ${Else}
    Delete "$INSTDIR\${PRODUCT_EXE}"
    Delete "$INSTDIR\${PRODUCT_SCR}"
    RMDir "$INSTDIR"
  ${EndIf}

  DeleteRegKey HKLM "${PRODUCT_UNINST_KEY}"
  DeleteRegKey HKLM "${PRODUCT_DIR_REGKEY}"
  DeleteRegKey HKLM "Software\${PRODUCT_NAME}"

  SetShellVarContext current
  SetAutoClose true
SectionEnd
