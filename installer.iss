[Setup]
AppId={{JARVIS-CORE-REPLACE-WITH-YOUR-GUID}}
AppName=JARVIS
AppVersion=3.6.3
AppPublisher=Bohdan
DefaultDirName={autopf}\JARVIS
DisableProgramGroupPage=yes
WizardStyle=modern
Compression=lzma
SolidCompression=yes
OutputDir=build\installer
OutputBaseFilename=Jarvis_Setup_v3.6.3
SetupIconFile=assets\jarvis.ico

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "russian"; MessagesFile: "compiler:Languages\Russian.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked
Name: "autostart"; Description: "Автозапуск JARVIS при старте Windows"; GroupDescription: "Дополнительно:"; Flags: unchecked

[Files]
; Базовый исполняемый файл
Source: "build\Release\jarvis.exe"; DestDir: "{app}"; Flags: ignoreversion
; Твои зависимости (добавь/проверь пути, если они отличаются)
Source: "vosk-win64-0.3.45\*"; DestDir: "{app}\vosk"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "build\Release\*.dll"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\JARVIS"; Filename: "{app}\jarvis.exe"
Name: "{autodesktop}\JARVIS"; Filename: "{app}\jarvis.exe"; Tasks: desktopicon
Name: "{userstartup}\JARVIS"; Filename: "{app}\jarvis.exe"; Tasks: autostart

[Run]
Filename: "{app}\jarvis.exe"; Description: "{cm:LaunchProgram,JARVIS}"; Flags: nowait postinstall skipifsilent