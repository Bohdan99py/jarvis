; -------------------------------------------------------
; installer.iss — Inno Setup для J.A.R.V.I.S.
;
; Включает: Jarvis.exe + Qt DLL + VC++ Runtime
; Пользователю НЕ нужно ничего ставить отдельно.
; -------------------------------------------------------

#define MyAppName "J.A.R.V.I.S."
#define MyAppVersion "2.0.0"
#define MyAppPublisher "JARVIS Project"
#define MyAppURL "https://github.com/Bohdan99py/jarvis"
#define MyAppExeName "Jarvis.exe"
#define MyAppBuildDir "build\release_package"

[Setup]
AppId={{A1B2C3D4-E5F6-7890-ABCD-EF1234567890}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppVerName={#MyAppName} v{#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}/issues
AppUpdatesURL={#MyAppURL}/releases
DefaultDirName={autopf}\JARVIS
DefaultGroupName={#MyAppName}
DisableProgramGroupPage=yes
OutputDir=build\installer
OutputBaseFilename=JARVIS-Setup-{#MyAppVersion}
Compression=lzma2/ultra64
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
UninstallDisplayIcon={app}\{#MyAppExeName}
CloseApplications=yes
CloseApplicationsFilter=Jarvis.exe

[Languages]
Name: "russian"; MessagesFile: "compiler:Languages\Russian.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"
Name: "autostart"; Description: "Запускать при старте Windows / Start with Windows"; GroupDescription: "Дополнительно / Additional:"

[Files]
; Всё из release_package (exe + Qt DLL + платформы и т.д.)
Source: "{#MyAppBuildDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

; VC++ Redistributable
Source: "redist\vc_redist.x64.exe"; DestDir: "{tmp}"; Flags: deleteafterinstall; Check: not VCRedistInstalled

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
Name: "{group}\{cm:UninstallProgram,{#MyAppName}}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Registry]
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; \
    ValueType: string; ValueName: "JARVIS"; \
    ValueData: """{app}\{#MyAppExeName}"""; \
    Flags: uninsdeletevalue; Tasks: autostart

[Run]
Filename: "{tmp}\vc_redist.x64.exe"; Parameters: "/install /quiet /norestart"; \
    StatusMsg: "Installing Visual C++ Runtime..."; \
    Flags: waituntilterminated; Check: not VCRedistInstalled

Filename: "{app}\{#MyAppExeName}"; \
    Description: "{cm:LaunchProgram,{#MyAppName}}"; \
    Flags: nowait postinstall skipifsilent

[UninstallDelete]
Type: filesandordirs; Name: "{localappdata}\JARVIS Project"

[Code]
function VCRedistInstalled(): Boolean;
begin
    Result := RegKeyExists(HKLM,
        'SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\x64')
        or RegKeyExists(HKLM,
        'SOFTWARE\WOW6432Node\Microsoft\VisualStudio\14.0\VC\Runtimes\x64');
end;
