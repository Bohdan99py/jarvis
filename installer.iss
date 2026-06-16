; =====================================================================
; Inno Setup script for J.A.R.V.I.S.
; Версия и MyAppBuildDir подставляются GitHub Actions при сборке.
;
; ИСПРАВЛЕНИЕ БАГА: EULA теперь показывается на языке установщика.
;   Русский интерфейс → EULA_JARVIS.txt (RU)
;   Английский интерфейс → JARVIS_EULA_EN.txt (EN)
;   Реализовано через Pascal-скрипт InitializeSetup + [CustomMessages].
; =====================================================================

#define MyAppName "JARVIS"
#define MyAppVersion "3.0.0"
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
; ИСПРАВЛЕНИЕ: LicenseFile убран из [Setup] — задаётся динамически через
; Pascal-скрипт InitializeSetup() ниже в зависимости от языка.
InfoBeforeFile=JARVIS_INSTALL_NOTES.txt

[Languages]
Name: "russian"; MessagesFile: "compiler:Languages\Russian.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"

; Путь к файлу лицензии для каждого языка
[CustomMessages]
russian.LicenseFilePath=EULA_JARVIS.txt
english.LicenseFilePath=JARVIS_EULA_EN.txt

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; \
  GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked
Name: "autostart"; \
  Description: "Запускать J.A.R.V.I.S. при старте Windows / Launch on startup"; \
  GroupDescription: "Параметры запуска / Startup"; \
  Flags: unchecked

[Files]
Source: "{#MyAppBuildDir}\{#MyAppExeName}"; DestDir: "{app}"; Flags: ignoreversion
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
Source: "{#MyAppBuildDir}\multimedia\*"; DestDir: "{app}\multimedia"; \
  Flags: ignoreversion recursesubdirs createallsubdirs skipifsourcedoesntexist
Source: "{#MyAppBuildDir}\audio\*"; DestDir: "{app}\audio"; \
  Flags: ignoreversion recursesubdirs createallsubdirs skipifsourcedoesntexist
Source: "{#MyAppBuildDir}\Tesseract-OCR\*"; DestDir: "{app}\Tesseract-OCR"; \
  Flags: ignoreversion recursesubdirs createallsubdirs skipifsourcedoesntexist
Source: "{#MyAppBuildDir}\plugins\*"; DestDir: "{app}\plugins"; \
  Flags: ignoreversion recursesubdirs createallsubdirs skipifsourcedoesntexist
Source: "EULA_JARVIS.txt"; DestDir: "{app}"; Flags: ignoreversion skipifsourcedoesntexist
Source: "JARVIS_EULA_EN.txt"; DestDir: "{app}"; Flags: ignoreversion skipifsourcedoesntexist
Source: "README.md"; DestDir: "{app}"; Flags: ignoreversion skipifsourcedoesntexist
Source: "JARVIS_INSTALL_NOTES.txt"; DestDir: "{app}"; Flags: ignoreversion skipifsourcedoesntexist

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
Filename: "{app}\{#MyAppExeName}"; \
  Description: "{cm:LaunchProgram,{#StringChange(MyAppName, '&', '&&')}}"; \
  Flags: nowait postinstall skipifsilent

[UninstallRun]
Filename: "taskkill.exe"; Parameters: "/F /IM {#MyAppExeName}"; \
  Flags: runhidden skipifdoesntexist

[UninstallDelete]
Type: filesandordirs; Name: "{userappdata}\JARVIS\logs"

; =====================================================================
; Pascal-скрипт: динамически выбираем файл лицензии по языку.
; Inno Setup не поддерживает LicenseFile= как [CustomMessages]-переменную
; напрямую — обходим через WizardForm.LicenseMemo.
; =====================================================================
[Code]
var
  LicensePage: TOutputMsgMemoWizardPage;

function GetLicensePath(): String;
var
  LicenseFile: String;
begin
  // ActiveLanguage() возвращает Name из [Languages]
  if ActiveLanguage() = 'russian' then
    LicenseFile := 'EULA_JARVIS.txt'
  else
    LicenseFile := 'JARVIS_EULA_EN.txt';

  Result := ExpandConstant('{src}\' + LicenseFile);

  // Fallback: если файл лежит рядом с .iss а не в {src}
  if not FileExists(Result) then
    Result := ExtractFilePath(ExpandConstant('{srcexe}')) + LicenseFile;
end;

procedure InitializeWizard();
var
  LicenseText: AnsiString;
  LicensePath: String;
begin
  // Создаём страницу с лицензией вручную (вместо статического LicenseFile=)
  LicensePath := GetLicensePath();

  if FileExists(LicensePath) then
  begin
    LoadStringFromFile(LicensePath, LicenseText);
    LicensePage := CreateOutputMsgMemoPage(
      wpWelcome,
      // Заголовок страницы
      IfThen(ActiveLanguage() = 'russian',
        'Лицензионное соглашение',
        'License Agreement'),
      // Подзаголовок
      IfThen(ActiveLanguage() = 'russian',
        'Прочитайте следующее лицензионное соглашение перед установкой J.A.R.V.I.S.',
        'Please read the following license agreement before installing J.A.R.V.I.S.'),
      // Текст лицензии
      String(LicenseText),
      // Метка под текстом
      IfThen(ActiveLanguage() = 'russian',
        'Нажимая "Далее", вы принимаете условия лицензионного соглашения.',
        'By clicking Next, you accept the terms of the license agreement.')
    );
  end;
end;
