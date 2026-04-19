; -------------------------------------------------------
; installer.iss — Inno Setup скрипт для J.A.R.V.I.S.
;
; Требования:
;   1. Inno Setup 6+ (https://jrsoftware.org/isinfo.php)
;   2. Собранный проект в build\bin\
;   3. Qt DLL рядом (через windeployqt)
;
; Использование:
;   1. Собери проект: build.bat Release
;   2. Подготовь Qt DLL: scripts\prepare_release.bat
;   3. Открой этот файл в Inno Setup Compiler → Compile
;   Или из командной строки:
;   "C:\Program Files (x86)\Inno Setup 6\ISCC.exe" installer.iss
; -------------------------------------------------------

#define MyAppName "J.A.R.V.I.S."
#define MyAppVersion "2.0.0"
#define MyAppPublisher "JARVIS Project"
#define MyAppURL "https://github.com/YOUR_GITHUB_USER/jarvis"
#define MyAppExeName "JarvisApp.exe"
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
LicenseFile=LICENSE
OutputDir=build\installer
OutputBaseFilename=JARVIS-Setup-{#MyAppVersion}
SetupIconFile=assets\icon.ico
Compression=lzma2/ultra64
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
UninstallDisplayIcon={app}\{#MyAppExeName}

; Красивый установщик
WizardImageFile=assets\wizard_image.bmp
WizardSmallImageFile=assets\wizard_small.bmp

[Languages]
Name: "russian"; MessagesFile: "compiler:Languages\Russian.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked
Name: "autostart"; Description: "Запускать при старте Windows"; GroupDescription: "Дополнительно:"

[Files]
; Основные файлы
Source: "{#MyAppBuildDir}\JarvisApp.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#MyAppBuildDir}\JarvisCore.dll"; DestDir: "{app}"; Flags: ignoreversion

; Qt DLL и зависимости (после windeployqt)
Source: "{#MyAppBuildDir}\*.dll"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs
Source: "{#MyAppBuildDir}\platforms\*"; DestDir: "{app}\platforms"; Flags: ignoreversion recursesubdirs
Source: "{#MyAppBuildDir}\styles\*"; DestDir: "{app}\styles"; Flags: ignoreversion recursesubdirs
Source: "{#MyAppBuildDir}\tls\*"; DestDir: "{app}\tls"; Flags: ignoreversion recursesubdirs

; Плагины (папка)
Source: "{#MyAppBuildDir}\plugins\*"; DestDir: "{app}\plugins"; Flags: ignoreversion recursesubdirs createallsubdirs

; Скрипт обновления
Source: "scripts\update.bat"; DestDir: "{app}\scripts"; Flags: ignoreversion

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
Name: "{group}\{cm:UninstallProgram,{#MyAppName}}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Registry]
; Автозагрузка (опционально)
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueType: string; ValueName: "JARVIS"; ValueData: """{app}\{#MyAppExeName}"""; Flags: uninsdeletevalue; Tasks: autostart

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "{cm:LaunchProgram,{#MyAppName}}"; Flags: nowait postinstall skipifsilent

[UninstallDelete]
; Очищаем папку обновлений и бэкапов при удалении
Type: filesandordirs; Name: "{app}\updates"
Type: filesandordirs; Name: "{app}\backup"

[Code]
// Проверяем MSVC Runtime при установке
function InitializeSetup(): Boolean;
begin
  Result := True;
  // Можно добавить проверку на наличие Visual C++ Redistributable
end;
