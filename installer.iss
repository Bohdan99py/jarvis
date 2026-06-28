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
; InfoBeforeFile removed — streamlined flow

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "russian"; MessagesFile: "compiler:Languages\Russian.isl"

[Tasks]
Name: "desktopicon"; \
  Description: "{cm:CreateDesktopIcon}"; \
  GroupDescription: "{cm:AdditionalIcons}"; \
  Flags: checked

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
; =====================================================================
[Code]

const
  // JARVIS color palette
  CLR_BG_DEEP    = $100A0A;   // #0A0A10 (BGR for Inno)
  CLR_BG_PANEL   = $1E140C;   // #0C141E
  CLR_ACCENT     = $F1FC66;   // #66FCF1 (cyan)
  CLR_TEXT_MAIN  = $E0E8EC;   // #ECE8E0
  CLR_TEXT_DIM   = $A4928A;   // #8A92A4
  CLR_BORDER     = $503A1A;   // #1A3A50
  CLR_BTN_BG     = $382F0F;   // #0F2F38
  CLR_BTN_TEXT   = $FFD400;   // #00D4FF (BGR)

procedure ApplyDarkTheme();
var
  Form: TSetupForm;
begin
  Form := WizardForm;

  // Main form background
  Form.Color := CLR_BG_DEEP;

  // Inner page panel
  Form.InnerPage.Color := CLR_BG_DEEP;
  Form.OuterNotebook.Color := CLR_BG_DEEP;
  Form.InnerNotebook.Color := CLR_BG_DEEP;

  // Main panel (top header area)
  Form.MainPanel.Color := CLR_BG_PANEL;

  // Page caption and description
  Form.PageNameLabel.Font.Color := CLR_ACCENT;
  Form.PageNameLabel.Font.Size := 14;
  Form.PageNameLabel.Font.Style := [fsBold];
  Form.PageDescriptionLabel.Font.Color := CLR_TEXT_DIM;
  Form.PageDescriptionLabel.Font.Size := 9;

  // Welcome/Finish labels
  Form.WelcomeLabel1.Font.Color := CLR_ACCENT;
  Form.WelcomeLabel1.Font.Size := 20;
  Form.WelcomeLabel1.Font.Style := [fsBold];
  Form.WelcomeLabel2.Font.Color := CLR_TEXT_MAIN;
  Form.WelcomeLabel2.Font.Size := 10;

  Form.FinishedHeadingLabel.Font.Color := CLR_ACCENT;
  Form.FinishedHeadingLabel.Font.Size := 20;
  Form.FinishedHeadingLabel.Font.Style := [fsBold];
  Form.FinishedLabel.Font.Color := CLR_TEXT_MAIN;

  // License page
  Form.LicenseAcceptedRadio.Font.Color := CLR_ACCENT;
  Form.LicenseNotAcceptedRadio.Font.Color := CLR_TEXT_DIM;
  Form.LicenseMemo.Color := CLR_BG_PANEL;
  Form.LicenseMemo.Font.Color := CLR_TEXT_MAIN;
  Form.LicenseMemo.Font.Name := 'Consolas';
  Form.LicenseMemo.Font.Size := 9;
  Form.LicenseLabel1.Font.Color := CLR_TEXT_MAIN;

  // Directory page
  Form.DirEdit.Color := CLR_BG_PANEL;
  Form.DirEdit.Font.Color := CLR_TEXT_MAIN;
  Form.SelectDirLabel.Font.Color := CLR_TEXT_MAIN;
  Form.SelectDirBrowseLabel.Font.Color := CLR_TEXT_DIM;
  Form.DiskSpaceLabel.Font.Color := CLR_TEXT_DIM;

  // Tasks page (checkboxes)
  Form.TasksList.Color := CLR_BG_DEEP;
  Form.TasksList.Font.Color := CLR_TEXT_MAIN;

  // Progress bar page
  Form.StatusLabel.Font.Color := CLR_TEXT_MAIN;
  Form.FilenameLabel.Font.Color := CLR_TEXT_DIM;
  Form.ProgressGauge.BackColor := CLR_BG_PANEL;

  // Buttons
  Form.BackButton.Font.Color := CLR_TEXT_DIM;
  Form.NextButton.Font.Color := CLR_BTN_TEXT;
  Form.CancelButton.Font.Color := CLR_TEXT_DIM;

  // Run list (post-install checkboxes)
  Form.RunList.Color := CLR_BG_DEEP;
  Form.RunList.Font.Color := CLR_TEXT_MAIN;

  // Bevel lines
  Form.Bevel.Visible := False;
  Form.Bevel1.Visible := False;
end;

procedure InitializeWizard();
var
  VersionLabel: TNewStaticText;
begin
  ApplyDarkTheme();

  // Add version tag in bottom-left corner
  VersionLabel := TNewStaticText.Create(WizardForm);
  VersionLabel.Parent := WizardForm;
  VersionLabel.Caption := 'J.A.R.V.I.S. v{#MyAppVersion}';
  VersionLabel.Font.Color := CLR_TEXT_DIM;
  VersionLabel.Font.Size := 8;
  VersionLabel.Left := ScaleX(12);
  VersionLabel.Top := WizardForm.CancelButton.Top +
                      (WizardForm.CancelButton.Height div 2) -
                      (VersionLabel.Height div 2);

  // Override welcome text
  WizardForm.WelcomeLabel1.Caption := 'J.A.R.V.I.S.';
  WizardForm.WelcomeLabel2.Caption :=
    'Just A Rather Very Intelligent System' + #13#10 + #13#10 +
    'Version {#MyAppVersion}' + #13#10 + #13#10 +
    'Your personal AI desktop assistant with voice control, ' +
    'offline learning, and Telegram integration.' + #13#10 + #13#10 +
    'Click Next to continue.';

  // Override finish text
  WizardForm.FinishedHeadingLabel.Caption := 'System Online.';
  WizardForm.FinishedLabel.Caption :=
    'J.A.R.V.I.S. has been installed on your computer.' + #13#10 + #13#10 +
    'On first launch, JARVIS will offer to download a voice model ' +
    'for offline speech recognition.' + #13#10 + #13#10 +
    'Say "Jarvis" to activate. Good luck, sir.';
end;

procedure CurPageChanged(CurPageID: Integer);
begin
  // Re-apply theme after page navigation (some controls reset)
  ApplyDarkTheme();

  // Auto-accept license (still shown, but pre-selected)
  if CurPageID = wpLicense then begin
    WizardForm.LicenseAcceptedRadio.Checked := True;
  end;
end;
