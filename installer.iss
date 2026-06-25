; =====================================================================
; Inno Setup script for J.A.R.V.I.S. v3.x
;
; What's new in v3.x:
;   - Vosk DLL included in installer (no download needed on first run)
;   - Voice models downloaded by user choice on first launch
;   - Privacy: recording only on voice activity detection (VAD)
;   - Removed duplicate multimedia/audio section
;   - Fixed UninstallRun (skipifdoesntexist -> nowait)
;
; Version and MyAppBuildDir are set by GitHub Actions during build.
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
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "russian"; MessagesFile: "compiler:Languages\Russian.isl"

[Tasks]
Name: "desktopicon"; \
  Description: "{cm:CreateDesktopIcon}"; \
  GroupDescription: "{cm:AdditionalIcons}"; \
  Flags: unchecked

Name: "autostart"; \
  Description: "Launch J.A.R.V.I.S. on Windows startup"; \
  GroupDescription: "Startup options"; \
  Flags: unchecked

[Files]
; ---- Main executable ----
Source: "{#MyAppBuildDir}\{#MyAppExeName}"; \
  DestDir: "{app}"; Flags: ignoreversion

; ---- Qt DLL and runtime ----
Source: "{#MyAppBuildDir}\*.dll"; \
  DestDir: "{app}"; \
  Flags: ignoreversion skipifsourcedoesntexist

; ---- Qt plugins (required) ----
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

; ---- Qt SQL (CRITICAL: without this jarvis.db won't open!) ----
; qsqlite.dll is the Qt SQLite driver. Without it DatabaseManager::open()
; fails with "driver not loaded", training counters stay 0, likes aren't
; saved, history isn't written. windeployqt sometimes skips it if it
; doesn't see an explicit QSqlDatabase in main.cpp, so we copy it
; explicitly both in CI (build.yml) and here in the installer.
Source: "{#MyAppBuildDir}\sqldrivers\*"; \
  DestDir: "{app}\sqldrivers"; \
  Flags: ignoreversion recursesubdirs createallsubdirs skipifsourcedoesntexist

; ---- Qt Multimedia (voice input) ----
Source: "{#MyAppBuildDir}\multimedia\*"; \
  DestDir: "{app}\multimedia"; \
  Flags: ignoreversion recursesubdirs createallsubdirs skipifsourcedoesntexist

Source: "{#MyAppBuildDir}\audio\*"; \
  DestDir: "{app}\audio"; \
  Flags: ignoreversion recursesubdirs createallsubdirs skipifsourcedoesntexist

; ---- Vosk runtime DLL (voice input, offline ASR) ----
; These DLLs are included in the installer — models are downloaded separately on first launch
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

; ---- OCR (optional) ----
Source: "{#MyAppBuildDir}\Tesseract-OCR\*"; \
  DestDir: "{app}\Tesseract-OCR"; \
  Flags: ignoreversion recursesubdirs createallsubdirs skipifsourcedoesntexist

; ---- JARVIS plugins ----
Source: "{#MyAppBuildDir}\plugins\*"; \
  DestDir: "{app}\plugins"; \
  Flags: ignoreversion recursesubdirs createallsubdirs skipifsourcedoesntexist

; ---- Documentation ----
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
; Autostart (only if user selected the task)
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
; nowait + runhidden — not "skipifdoesntexist" since that's not a valid [UninstallRun] flag
Filename: "taskkill.exe"; \
  Parameters: "/F /IM {#MyAppExeName}"; \
  Flags: runhidden nowait

[UninstallDelete]
Type: filesandordirs; Name: "{app}"
Type: filesandordirs; Name: "{userappdata}\JARVIS"
Type: filesandordirs; Name: "{userappdata}\Bohdan99py\JARVIS"
; Voice models are NOT deleted automatically (large files — user decides)
; To delete manually: %APPDATA%\Bohdan99py\JARVIS\vosk\
