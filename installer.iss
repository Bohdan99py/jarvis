; =====================================================================
; Inno Setup script for J.A.R.V.I.S.
; Версия и MyAppBuildDir подставляются GitHub Actions при сборке.
; =====================================================================

#define MyAppName "JARVIS"
#define MyAppVersion "2.5.0"
#define MyAppPublisher "Bohdan99py"
#define MyAppURL "https://github.com/Bohdan99py/jarvis"
#define MyAppExeName "jarvis.exe"
#define MyAppBuildDir "release_package"

[Setup]
AppId={{B0F5A4E2-1C3A-4E29-9D7E-AB2D5F8E9B11}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppVerName={#MyAppName} {#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}
AppUpdatesURL={#MyAppURL}/releases
DefaultDirName={autopf}\{#MyAppName}
DefaultGroupName={#MyAppName}
DisableProgramGroupPage=yes
OutputDir=build\installer
OutputBaseFilename=JARVIS-Setup-{#MyAppVersion}
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
UninstallDisplayIcon={app}\{#MyAppExeName}

; Лицензионные соглашения — сначала RU, потом EN (выбор по языку)
LicenseFile=EULA_JARVIS.txt

; Приветственный экран — инструкция кратко
InfoBeforeFile=JARVIS_INSTALL_NOTES.txt

[Languages]
Name: "russian"; MessagesFile: "compiler:Languages\Russian.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
; Ярлык на рабочем столе
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; \
  GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

; Автозапуск при старте Windows
Name: "autostart"; \
  Description: "Запускать J.A.R.V.I.S. при старте Windows / Launch J.A.R.V.I.S. on Windows startup"; \
  GroupDescription: "Параметры запуска / Startup options"; \
  Flags: unchecked

[Files]
; Главный исполняемый файл
Source: "{#MyAppBuildDir}\{#MyAppExeName}"; DestDir: "{app}"; Flags: ignoreversion

; Qt DLL и плагины
Source: "{#MyAppBuildDir}\*.dll"; DestDir: "{app}"; \
  Flags: ignoreversion skipifsourcedoesntexist
Source: "{#MyAppBuildDir}\platforms\*"; DestDir: "{app}\platforms"; \
  Flags: ignoreversion recursesubdirs createallsubdirs skipifsourcedoesntexist
Source: "{#MyAppBuildDir}\styles\*"; DestDir: "{app}\styles"; \
  Flags: ignoreversion recursesubdirs createallsubdirs skipifsourcedoesntexist
Source: "{#MyAppBuildDir}\imageformats\*"; DestDir: "{app}\imageformats"; \
  Flags: ignoreversion recursesubdirs createallsubdirs skipifsourcedoesntexist
Source: "{#MyAppBuildDir}\iconengines\*"; DestDir: "{app}\iconengines"; \
  Flags: ignoreversion recursesubdirs createallsubdirs skipifsourcedoesntexist
Source: "{#MyAppBuildDir}\networkinformation\*"; DestDir: "{app}\networkinformation"; \
  Flags: ignoreversion recursesubdirs createallsubdirs skipifsourcedoesntexist
Source: "{#MyAppBuildDir}\tls\*"; DestDir: "{app}\tls"; \
  Flags: ignoreversion recursesubdirs createallsubdirs skipifsourcedoesntexist
Source: "{#MyAppBuildDir}\generic\*"; DestDir: "{app}\generic"; \
  Flags: ignoreversion recursesubdirs createallsubdirs skipifsourcedoesntexist

; Poppler (PDF/OCR) — если есть в сборке
Source: "{#MyAppBuildDir}\Tesseract-OCR\*"; DestDir: "{app}\Tesseract-OCR"; \
  Flags: ignoreversion recursesubdirs createallsubdirs skipifsourcedoesntexist

; Плагины JARVIS
Source: "{#MyAppBuildDir}\plugins\*"; DestDir: "{app}\plugins"; \
  Flags: ignoreversion recursesubdirs createallsubdirs skipifsourcedoesntexist

; Документы — EULA на обоих языках, README
Source: "EULA_JARVIS.txt"; DestDir: "{app}"; Flags: ignoreversion skipifsourcedoesntexist
Source: "JARVIS_EULA_EN.txt"; DestDir: "{app}"; Flags: ignoreversion skipifsourcedoesntexist
Source: "README.md"; DestDir: "{app}"; Flags: ignoreversion skipifsourcedoesntexist

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
Name: "{group}\{cm:UninstallProgram,{#MyAppName}}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Registry]
; Автозапуск: Tasks: autostart — пишем ключ в реестр только если пользователь выбрал
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; \
  ValueType: string; ValueName: "JARVIS"; \
  ValueData: """{app}\{#MyAppExeName}"""; \
  Flags: uninsdeletevalue; Tasks: autostart

[Run]
; Запустить после установки (опционально)
Filename: "{app}\{#MyAppExeName}"; \
  Description: "{cm:LaunchProgram,{#StringChange(MyAppName, '&', '&&')}}"; \
  Flags: nowait postinstall skipifsilent

[UninstallRun]
; При удалении — убиваем процесс если запущен
Filename: "taskkill.exe"; Parameters: "/F /IM {#MyAppExeName}"; \
  Flags: runhidden skipifdoesntexist

[UninstallDelete]
; Чистим всё что осталось (логи, кэш)
Type: filesandordirs; Name: "{app}"
; AppData JARVIS — спрашивать не будем, удаляем (настройки, learned_commands.json)
Type: filesandordirs; Name: "{userappdata}\JARVIS"
Type: filesandordirs; Name: "{userappdata}\Bohdan99py\JARVIS"
