Unicode true
ManifestDPIAware true
; v0.1.3 launches updates with CreateProcess. A requireAdministrator manifest
; fails there with ERROR_ELEVATION_REQUIRED before any installer code can run.
RequestExecutionLevel user
SetCompress off

!include MUI2.nsh
!include LogicLib.nsh
!include x64.nsh
!include FileFunc.nsh

!ifndef APP_KEY
    !define APP_KEY "Software\snipaste-open-source"
!endif
!ifndef UNINSTALL_KEY
    !define UNINSTALL_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\snipaste-open-source"
!endif
!ifndef START_MENU_FOLDER
    !define START_MENU_FOLDER "snipaste"
!endif
Name "snipaste"
OutFile "${OUTPUT_FILE}"
InstallDir ""
VIProductVersion "${PRODUCT_VERSION}"
VIAddVersionKey "ProductName" "snipaste"
VIAddVersionKey "FileDescription" "snipaste Setup"
VIAddVersionKey "FileVersion" "${VERSION}"
VIAddVersionKey "ProductVersion" "${VERSION}"
VIAddVersionKey "LegalCopyright" "snipaste contributors"
Var PortableMode

!macro RequireAdministrator suffix
    UserInfo::GetAccountType
    Pop $1
    ${If} $1 != "Admin"
        ClearErrors
        ExecShellWait "runas" "$EXEPATH" '$0 ${suffix}' SW_SHOWNORMAL $1
        ${If} ${Errors}
            SetErrorLevel 1223
        ${Else}
            SetErrorLevel $1
        ${EndIf}
        Quit
    ${EndIf}
!macroend

!define MUI_ABORTWARNING
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "SimpChinese"
!insertmacro MUI_LANGUAGE "English"

Function .onInit
    SetRegView 64
    ; An explicit /D (including in-app updates) takes precedence, followed by
    ; the legacy per-user installation and then the machine installation.
    ReadRegStr $2 HKCU "${APP_KEY}" "InstallDir"
    ${If} $INSTDIR == ""
        StrCpy $INSTDIR $2
        ${If} $INSTDIR == ""
            ReadRegStr $INSTDIR HKLM "${APP_KEY}" "InstallDir"
        ${EndIf}
        ${If} $INSTDIR == ""
            StrCpy $INSTDIR "$PROGRAMFILES64\Snipaste"
        ${EndIf}
    ${EndIf}
    StrCpy $PortableMode 0
    ${GetParameters} $0
    ClearErrors
    ${GetOptions} $0 "/PORTABLE" $1
    ${IfNot} ${Errors}
        StrCpy $PortableMode 1
    ${EndIf}
    ClearErrors
    ${IfNot} ${RunningX64}
        MessageBox MB_ICONSTOP "This package requires 64-bit Windows." /SD IDOK
        SetErrorLevel 1
        Abort
    ${EndIf}
    ${If} $PortableMode == 1
    ${OrIf} $INSTDIR == $2
        ; Upgrade v0.1.3 in place, keeping its user registration and shortcuts.
        SetShellVarContext current
    ${Else}
        !insertmacro RequireAdministrator ""
        SetShellVarContext all
    ${EndIf}
FunctionEnd

Section "snipaste"
    InitPluginsDir
    SetOutPath "$PLUGINSDIR"
    File /oname=package.zip "${ARCHIVE}"
    File /oname=install-windows.ps1 "${SCRIPT_DIRECTORY}\install-windows.ps1"
    ; The NSIS stub is 32-bit; use native PowerShell to inspect 64-bit app processes.
    nsExec::ExecToLog '"$WINDIR\Sysnative\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "$PLUGINSDIR\install-windows.ps1" -Archive "$PLUGINSDIR\package.zip" -Destination "$INSTDIR"'
    Pop $0
    ${If} $0 != 0
        MessageBox MB_ICONSTOP "安装失败，请关闭 snipaste 并检查安装目录权限。原有文件已保留。$\r$\nInstallation failed. Close snipaste and check folder permissions." /SD IDOK
        SetErrorLevel 1
        Abort
    ${EndIf}
    SetOutPath "$INSTDIR"
    ${If} $PortableMode == 0
        WriteUninstaller "$INSTDIR\Uninstall.exe"
        CreateDirectory "$SMPROGRAMS\${START_MENU_FOLDER}"
        CreateShortcut "$SMPROGRAMS\${START_MENU_FOLDER}\snipaste.lnk" "$INSTDIR\snipaste.exe"
        CreateShortcut "$SMPROGRAMS\${START_MENU_FOLDER}\卸载 snipaste.lnk" "$INSTDIR\Uninstall.exe"
        WriteRegStr SHCTX "${APP_KEY}" "InstallDir" "$INSTDIR"
        WriteRegStr SHCTX "${UNINSTALL_KEY}" "DisplayName" "snipaste (Open Source)"
        WriteRegStr SHCTX "${UNINSTALL_KEY}" "DisplayVersion" "${VERSION}"
        WriteRegStr SHCTX "${UNINSTALL_KEY}" "DisplayIcon" "$INSTDIR\snipaste.exe"
        WriteRegStr SHCTX "${UNINSTALL_KEY}" "InstallLocation" "$INSTDIR"
        WriteRegStr SHCTX "${UNINSTALL_KEY}" "UninstallString" '$\"$INSTDIR\Uninstall.exe$\"'
        WriteRegStr SHCTX "${UNINSTALL_KEY}" "QuietUninstallString" '$\"$INSTDIR\Uninstall.exe$\" /S'
        WriteRegDWORD SHCTX "${UNINSTALL_KEY}" "NoModify" 1
        WriteRegDWORD SHCTX "${UNINSTALL_KEY}" "NoRepair" 1
        WriteRegDWORD SHCTX "${UNINSTALL_KEY}" "EstimatedSize" ${INSTALLED_SIZE}
    ${EndIf}
SectionEnd

Function un.onInit
    SetRegView 64
    ReadRegStr $2 HKCU "${APP_KEY}" "InstallDir"
    ${If} $INSTDIR == $2
        SetShellVarContext current
    ${Else}
        ${un.GetParameters} $0
        !insertmacro RequireAdministrator "_?=$INSTDIR"
        SetShellVarContext all
    ${EndIf}
FunctionEnd

Section "Uninstall"
    InitPluginsDir
    SetOutPath "$PLUGINSDIR"
    File /oname=install-windows.ps1 "${SCRIPT_DIRECTORY}\install-windows.ps1"
    nsExec::ExecToLog '"$WINDIR\Sysnative\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "$PLUGINSDIR\install-windows.ps1" -Destination "$INSTDIR" -CloseOnly'
    Pop $0
    ${If} $0 != 0
        MessageBox MB_ICONSTOP "请关闭 snipaste 后重试。$\r$\nClose snipaste and try again." /SD IDOK
        SetErrorLevel 1
        Abort
    ${EndIf}
    !include "${UNINSTALL_FILES}"
    Delete "$INSTDIR\Uninstall.exe"
    RMDir "$INSTDIR"
    ReadRegStr $0 SHCTX "${APP_KEY}" "InstallDir"
    ${If} $0 == $INSTDIR
        Delete "$SMPROGRAMS\${START_MENU_FOLDER}\snipaste.lnk"
        Delete "$SMPROGRAMS\${START_MENU_FOLDER}\卸载 snipaste.lnk"
        RMDir "$SMPROGRAMS\${START_MENU_FOLDER}"
        DeleteRegKey SHCTX "${APP_KEY}"
        DeleteRegKey SHCTX "${UNINSTALL_KEY}"
    ${EndIf}
SectionEnd
