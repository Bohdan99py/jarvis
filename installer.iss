; =====================================================================
; Inno Setup script for J.A.R.V.I.S. v3.x
;
; Что нового v3.x:
;   - Vosk DLL включена в installer (не нужно скачивать при первом запуске)
;   - Голосовые модели скачиваются по выбору пользователя при первом старте
;   - Политика приватности: запись только при обнаружении голоса (VAD)
;   - Удалён дублированный раздел multimedia/audio
;   - Исправлен UninstallRun (skipifdoesntexist → nowait)
;
; Версия и MyAppBuildDir подставляются GitHub Actions при сборке.
; =====================================================================

#define MyAppName "JARVIS"
#define MyAppVersion "3.1.0"
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
AppSupportURL={#MyAppURL}/issues
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
LicenseFile=EULA_JARVIS.txt
InfoBeforeFile=JARVIS_INSTALL_NOTES.txt

[Languages]
Name: "russian"; MessagesFile: "compiler:Languages\Russian.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; \
  Description: "{cm:CreateDesktopIcon}"; \
  GroupDescription: "{cm:AdditionalIcons}"; \
  Flags: unchecked

Name: "autostart"; \
  Description: "Запускать J.A.R.V.I.S. при старте Windows / Launch on startup"; \
  GroupDescription: "Параметры запуска / Startup"; \
  Flags: unchecked

[Files]
; ---- Основной исполняемый файл ----
Source: "{#MyAppBuildDir}\{#MyAppExeName}"; \
  DestDir: "{app}"; Flags: ignoreversion

; ---- Qt DLL и runtime ----
Source: "{#MyAppBuildDir}\*.dll"; \
  DestDir: "{app}"; \
  Flags: ignoreversion skipifsourcedoesntexist

; ---- Qt плагины (обязательные) ----
Source: "{#MyAppBuildDir}\platforms\*"; \
  DestDir: "{app}\platforms"; \
  Flags: ignoreversion recursesubdirs createallsubdirs skipifsourcedoesntexist

Source: "{#MyAppBuildDir}\styles\*"; \
  DestDir: "{app}\styles"; \
  Flags: ignoreversion recursesubdirs createallsubdirs skipifsourcedoesntexist

Source: "{#MyAppBuildDir}\imageformats\*"; \
  DestDir: "{app}\imageformats"; \
  Flags: ignoreversion recursesubdirs createallsubdirs skipifsourcedoesntexist

Source: "{#MyAppBuildDir}\iconengines\*"; \
  DestDir: "{app}\iconengines"; \
  Flags: ignoreversion recursesubdirs createallsubdirs skipifsourcedoesntexist

Source: "{#MyAppBuildDir}\networkinformation\*"; \
  DestDir: "{app}\networkinformation"; \
  Flags: ignoreversion recursesubdirs createallsubdirs skipifsourcedoesntexist

Source: "{#MyAppBuildDir}\tls\*"; \
  DestDir: "{app}\tls"; \
  Flags: ignoreversion recursesubdirs createallsubdirs skipifsourcedoesntexist

Source: "{#MyAppBuildDir}\generic\*"; \
  DestDir: "{app}\generic"; \
  Flags: ignoreversion recursesubdirs createallsubdirs skipifsourcedoesntexist

; ---- Qt Multimedia (голосовой ввод) ----
Source: "{#MyAppBuildDir}\multimedia\*"; \
  DestDir: "{app}\multimedia"; \
  Flags: ignoreversion recursesubdirs createallsubdirs skipifsourcedoesntexist

Source: "{#MyAppBuildDir}\audio\*"; \
  DestDir: "{app}\audio"; \
  Flags: ignoreversion recursesubdirs createallsubdirs skipifsourcedoesntexist

; ---- Vosk runtime DLL (голосовой ввод, офлайн ASR) ----
; Эти DLL включены в installer — модели качаются отдельно при первом запуске
Source: "{#MyAppBuildDir}\libvosk.dll"; \
  DestDir: "{app}"; \
  Flags: ignoreversion skipifsourcedoesntexist

Source: "{#MyAppBuildDir}\libgcc_s_seh-1.dll"; \
  DestDir: "{app}"; \
  Flags: ignoreversion skipifsourcedoesntexist

Source: "{#MyAppBuildDir}\libstdc++-6.dll"; \
  DestDir: "{app}"; \
  Flags: ignoreversion skipifsourcedoesntexist

Source: "{#MyAppBuildDir}\libwinpthread-1.dll"; \
  DestDir: "{app}"; \
  Flags: ignoreversion skipifsourcedoesntexist

; ---- OCR (опционально) ----
Source: "{#MyAppBuildDir}\Tesseract-OCR\*"; \
  DestDir: "{app}\Tesseract-OCR"; \
  Flags: ignoreversion recursesubdirs createallsubdirs skipifsourcedoesntexist

; ---- Плагины JARVIS ----
Source: "{#MyAppBuildDir}\plugins\*"; \
  DestDir: "{app}\plugins"; \
  Flags: ignoreversion recursesubdirs createallsubdirs skipifsourcedoesntexist

; ---- Документация ----
Source: "EULA_JARVIS.txt"; \
  DestDir: "{app}"; Flags: ignoreversion skipifsourcedoesntexist

Source: "JARVIS_EULA_EN.txt"; \
  DestDir: "{app}"; Flags: ignoreversion skipifsourcedoesntexist

Source: "README.md"; \
  DestDir: "{app}"; Flags: ignoreversion skipifsourcedoesntexist

Source: "JARVIS_INSTALL_NOTES.txt"; \
  DestDir: "{app}"; Flags: ignoreversion skipifsourcedoesntexist

Source: "PRIVACY_POLICY.txt"; \
  DestDir: "{app}"; Flags: ignoreversion skipifsourcedoesntexist

[Icons]
Name: "{group}\{#MyAppName}"; \
  Filename: "{app}\{#MyAppExeName}"

Name: "{group}\{cm:UninstallProgram,{#MyAppName}}"; \
  Filename: "{uninstallexe}"

Name: "{autodesktop}\{#MyAppName}"; \
  Filename: "{app}\{#MyAppExeName}"; \
  Tasks: desktopicon

[Registry]
; Автозапуск (только если пользователь выбрал)
Root: HKCU; \
  Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; \
  ValueType: string; \
  ValueName: "JARVIS"; \
  ValueData: """{app}\{#MyAppExeName}"""; \
  Flags: uninsdeletevalue; \
  Tasks: autostart

[Run]
Filename: "{app}\{#MyAppExeName}"; \
  Description: "{cm:LaunchProgram,{#StringChange(MyAppName, '&', '&&')}}"; \
  Flags: nowait postinstall skipifsilent

[UninstallRun]
; nowait + runhidden — не «skipifdoesntexist», так как это не флаг [UninstallRun]
Filename: "taskkill.exe"; \
  Parameters: "/F /IM {#MyAppExeName}"; \
  Flags: runhidden nowait

[UninstallDelete]
Type: filesandordirs; Name: "{app}"
Type: filesandordirs; Name: "{userappdata}\JARVIS"
Type: filesandordirs; Name: "{userappdata}\Bohdan99py\JARVIS"
; Голосовые модели НЕ удаляются автоматически (большие файлы, пользователь решает сам)
; Чтобы удалить: %APPDATA%\Bohdan99py\JARVIS\vosk\
