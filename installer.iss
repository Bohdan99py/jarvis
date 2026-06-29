; =====================================================================
; Inno Setup script for J.A.R.V.I.S. v3.x — Modernized Dark Theme
;
; Features:
;   - Dark JARVIS-themed UI (deep blue/grey palette)
;   - Streamlined flow: License → Path → Install → Done
;   - No ReadMe/InfoBefore page
;   - "Launch JARVIS" checkbox + "View Changelog" on finish
;   - Custom icon and branding
;   - Vosk DLLs included, voice models downloaded on first launch
;
; Version and MyAppBuildDir are overridden by CI (build.yml).
; =====================================================================

#define MyAppName "JARVIS"
#define MyAppVersion "3.6.3"
#define MyAppPublisher "Bohdan99py"
#define MyAppURL "https://github.com/Bohdan99py/jarvis"
#define MyAppExeName "jarvis.exe"
#define MyAppBuildDir "release_package"

[Setup]
AppId={{B0F5A4E2-1C3A-4E29-9D7E-AB2D5F8E9B11}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppVerName=J.A.R.V.I.S. {#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}/issues
AppUpdatesURL={#MyAppURL}/releases
DefaultDirName={autopf}\{#MyAppName}
DefaultGroupName={#MyAppName}
DisableProgramGroupPage=yes
DisableReadyPage=yes
OutputDir=build\installer
OutputBaseFilename=JARVIS-Setup-{#MyAppVersion}
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
WizardSizePercent=110,110
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
UninstallDisplayIcon={app}\{#MyAppExeName}
SetupIconFile=assets\jarvis.ico
LicenseFile=EULA_JARVIS.txt

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "russian"; MessagesFile: "compiler:Languages\Russian.isl"

[Tasks]
Name: "desktopicon"; \
  Description: "{cm:CreateDesktopIcon}"; \
  GroupDescription: "{cm:AdditionalIcons}"; \
  Flags: checkedonce

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

Source: "PRIVACY_POLICY.txt"; \
  DestDir: "{app}"; Flags: ignoreversion skipifsourcedoesntexist

[Icons]
Name: "{group}\J.A.R.V.I.S."; \
  Filename: "{app}\{#MyAppExeName}"; \
  Comment: "Just A Rather Very Intelligent System"

Name: "{group}\{cm:UninstallProgram,{#MyAppName}}"; \
  Filename: "{uninstallexe}"

Name: "{autodesktop}\J.A.R.V.I.S."; \
  Filename: "{app}\{#MyAppExeName}"; \
  Tasks: desktopicon; \
  Comment: "Just A Rather Very Intelligent System"

[Registry]
Root: HKCU; \
  Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; \
  ValueType: string; \
  ValueName: "JARVIS"; \
  ValueData: """{app}\{#MyAppExeName}"""; \
  Flags: uninsdeletevalue; \
  Tasks: autostart

[Run]
Filename: "{app}\{#MyAppExeName}"; \
  Description: "Launch J.A.R.V.I.S."; \
  Flags: nowait postinstall skipifsilent

Filename: "{#MyAppURL}/releases/tag/v{#MyAppVersion}"; \
  Description: "View Changelog"; \
  Flags: shellexec nowait postinstall skipifsilent unchecked

[UninstallRun]
Filename: "taskkill.exe"; \
  Parameters: "/F /IM {#MyAppExeName}"; \
  Flags: runhidden nowait

[UninstallDelete]
Type: filesandordirs; Name: "{app}"
Type: filesandordirs; Name: "{userappdata}\JARVIS"
Type: filesandordirs; Name: "{userappdata}\Bohdan99py\JARVIS"

; =====================================================================
;  Pascal Code — Dark JARVIS theme, branding, custom colors
;
;  Inno Setup 6.x Pascal API reference:
;    TWizardForm     — main form type (NOT TSetupForm)
;    TNewProgressBar — has Position/Min/Max/Style/State, NO BackColor
;    Bevel/Bevel1    — may not exist with WizardStyle=modern
; =====================================================================
[Code]

const
  CLR_BG_DEEP    = $100A0A;   // #0A0A10 (BGR for Inno)
  CLR_BG_PANEL   = $1E140C;   // #0C141E
  CLR_ACCENT     = $F1FC66;   // #66FCF1 (cyan)
  CLR_TEXT_MAIN  = $E0E8EC;   // #ECE8E0
  CLR_TEXT_DIM   = $A4928A;   // #8A92A4
  CLR_BTN_TEXT   = $FFD400;   // #00D4FF (BGR)

procedure ApplyDarkTheme();
begin
  // Main form background
  WizardForm.Color := CLR_BG_DEEP;

  // Notebook panels
  WizardForm.OuterNotebook.Color := CLR_BG_DEEP;
  WizardForm.InnerNotebook.Color := CLR_BG_DEEP;

  // Top header panel
  WizardForm.MainPanel.Color := CLR_BG_PANEL;

  // Page caption and description
  WizardForm.PageNameLabel.Font.Color := CLR_ACCENT;
  WizardForm.PageNameLabel.Font.Size := 14;
  WizardForm.PageNameLabel.Font.Style := [fsBold];
  WizardForm.PageDescriptionLabel.Font.Color := CLR_TEXT_DIM;
  WizardForm.PageDescriptionLabel.Font.Size := 9;

  // Welcome/Finish labels
  WizardForm.WelcomeLabel1.Font.Color := CLR_ACCENT;
  WizardForm.WelcomeLabel1.Font.Size := 20;
  WizardForm.WelcomeLabel1.Font.Style := [fsBold];
  WizardForm.WelcomeLabel2.Font.Color := CLR_TEXT_MAIN;
  WizardForm.WelcomeLabel2.Font.Size := 10;

  WizardForm.FinishedHeadingLabel.Font.Color := CLR_ACCENT;
  WizardForm.FinishedHeadingLabel.Font.Size := 20;
  WizardForm.FinishedHeadingLabel.Font.Style := [fsBold];
  WizardForm.FinishedLabel.Font.Color := CLR_TEXT_MAIN;

  // License page
  WizardForm.LicenseAcceptedRadio.Font.Color := CLR_ACCENT;
  WizardForm.LicenseNotAcceptedRadio.Font.Color := CLR_TEXT_DIM;
  WizardForm.LicenseMemo.Color := CLR_BG_PANEL;
  WizardForm.LicenseMemo.Font.Color := CLR_TEXT_MAIN;
  WizardForm.LicenseMemo.Font.Name := 'Consolas';
  WizardForm.LicenseMemo.Font.Size := 9;
  WizardForm.LicenseLabel1.Font.Color := CLR_TEXT_MAIN;

  // Directory page
  WizardForm.DirEdit.Color := CLR_BG_PANEL;
  WizardForm.DirEdit.Font.Color := CLR_TEXT_MAIN;
  WizardForm.SelectDirLabel.Font.Color := CLR_TEXT_MAIN;
  WizardForm.SelectDirBrowseLabel.Font.Color := CLR_TEXT_DIM;
  WizardForm.DiskSpaceLabel.Font.Color := CLR_TEXT_DIM;

  // Tasks page (checkboxes)
  WizardForm.TasksList.Color := CLR_BG_DEEP;
  WizardForm.TasksList.Font.Color := CLR_TEXT_MAIN;

  // Progress bar page — labels only (TNewProgressBar has no color API)
  WizardForm.StatusLabel.Font.Color := CLR_TEXT_MAIN;
  WizardForm.FilenameLabel.Font.Color := CLR_TEXT_DIM;

  // Buttons
  WizardForm.BackButton.Font.Color := CLR_TEXT_DIM;
  WizardForm.NextButton.Font.Color := CLR_BTN_TEXT;
  WizardForm.CancelButton.Font.Color := CLR_TEXT_DIM;

  // Run list (post-install checkboxes)
  WizardForm.RunList.Color := CLR_BG_DEEP;
  WizardForm.RunList.Font.Color := CLR_TEXT_MAIN;

  // Hide bottom separator if present
  WizardForm.Bevel.Visible := False;
end;

procedure InitializeWizard();
var
  VersionLabel: TNewStaticText;
begin
  ApplyDarkTheme();

  // Version tag in bottom-left corner
  VersionLabel := TNewStaticText.Create(WizardForm);
  VersionLabel.Parent := WizardForm;
  VersionLabel.Caption := 'J.A.R.V.I.S. v{#MyAppVersion}';
  VersionLabel.Font.Color := CLR_TEXT_DIM;
  VersionLabel.Font.Size := 8;
  VersionLabel.Left := ScaleX(12);
  VersionLabel.Top := WizardForm.CancelButton.Top +
                      (WizardForm.CancelButton.Height div 2) -
                      (VersionLabel.Height div 2);

  // Custom welcome text
  WizardForm.WelcomeLabel1.Caption := 'J.A.R.V.I.S.';
  WizardForm.WelcomeLabel2.Caption :=
    'Just A Rather Very Intelligent System' + #13#10 + #13#10 +
    'Version {#MyAppVersion}' + #13#10 + #13#10 +
    'Your personal AI desktop assistant with voice control, ' +
    'offline learning, and Telegram integration.' + #13#10 + #13#10 +
    'Click Next to continue.';

  // Custom finish text
  WizardForm.FinishedHeadingLabel.Caption := 'System Online.';
  WizardForm.FinishedLabel.Caption :=
    'J.A.R.V.I.S. has been installed on your computer.' + #13#10 + #13#10 +
    'On first launch, JARVIS will offer to download a voice model ' +
    'for offline speech recognition.' + #13#10 + #13#10 +
    'Say "Jarvis" to activate. Good luck, sir.';
end;

procedure CurPageChanged(CurPageID: Integer);
begin
  ApplyDarkTheme();

  if CurPageID = wpLicense then begin
    WizardForm.LicenseAcceptedRadio.Checked := True;
  end;
end;
