Unicode true
ManifestDPIAware true
RequestExecutionLevel admin
SetCompress off

!include MUI2.nsh
!include LogicLib.nsh
!include x64.nsh
!include FileFunc.nsh

!define APP_KEY "Software\snipaste-open-source"
!define UNINSTALL_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\snipaste-open-source"
Name "snipaste"
OutFile "${OUTPUT_FILE}"
InstallDir ""
VIProductVersion "${PRODUCT_VERSION}"
VIAddVersionKey "ProductName" "snipaste"
VIAddVersionKey "FileDescription" "snipaste Setup"
VIAddVersionKey "FileVersion" "${VERSION}"
VIAddVersionKey "LegalCopyright" "snipaste contributors"
Var PortableMode

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
    SetShellVarContext all
    ; An explicit /D (including in-app updates) takes precedence, followed by
    ; the registered installation. New installs use the native Program Files.
    ${If} $INSTDIR == ""
        ReadRegStr $INSTDIR HKLM "${APP_KEY}" "InstallDir"
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
        CreateDirectory "$SMPROGRAMS\snipaste"
        CreateShortcut "$SMPROGRAMS\snipaste\snipaste.lnk" "$INSTDIR\snipaste.exe"
        CreateShortcut "$SMPROGRAMS\snipaste\卸载 snipaste.lnk" "$INSTDIR\Uninstall.exe"
        WriteRegStr HKLM "${APP_KEY}" "InstallDir" "$INSTDIR"
        WriteRegStr HKLM "${UNINSTALL_KEY}" "DisplayName" "snipaste (Open Source)"
        WriteRegStr HKLM "${UNINSTALL_KEY}" "DisplayVersion" "${VERSION}"
        WriteRegStr HKLM "${UNINSTALL_KEY}" "DisplayIcon" "$INSTDIR\snipaste.exe"
        WriteRegStr HKLM "${UNINSTALL_KEY}" "InstallLocation" "$INSTDIR"
        WriteRegStr HKLM "${UNINSTALL_KEY}" "UninstallString" '$\"$INSTDIR\Uninstall.exe$\"'
        WriteRegStr HKLM "${UNINSTALL_KEY}" "QuietUninstallString" '$\"$INSTDIR\Uninstall.exe$\" /S'
        WriteRegDWORD HKLM "${UNINSTALL_KEY}" "NoModify" 1
        WriteRegDWORD HKLM "${UNINSTALL_KEY}" "NoRepair" 1
        WriteRegDWORD HKLM "${UNINSTALL_KEY}" "EstimatedSize" ${INSTALLED_SIZE}
    ${EndIf}
SectionEnd

Function un.onInit
    SetRegView 64
    SetShellVarContext all
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
    ReadRegStr $0 HKLM "${APP_KEY}" "InstallDir"
    ${If} $0 == $INSTDIR
        Delete "$SMPROGRAMS\snipaste\snipaste.lnk"
        Delete "$SMPROGRAMS\snipaste\卸载 snipaste.lnk"
        RMDir "$SMPROGRAMS\snipaste"
        DeleteRegKey HKLM "${APP_KEY}"
        DeleteRegKey HKLM "${UNINSTALL_KEY}"
    ${EndIf}
SectionEnd
