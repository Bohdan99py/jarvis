; -------------------------------------------------------
; installer.iss — Inno Setup скрипт для J.A.R.V.I.S.
;
; Создаёт установщик со ВСЕМИ зависимостями:
;   - Jarvis.exe + Qt DLL (через windeployqt)
;   - Visual C++ Redistributable (vcredist)
;   - Ярлыки, автозагрузка, деинсталляция
;
; Использование:
;   1. Собери: scripts\build_release.bat
;   2. Скомпилируй: "C:\Program Files (x86)\Inno Setup 6\ISCC.exe" installer.iss
;   Или GitHub Actions сделает всё автоматически.
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

; Иконка (если есть)
; SetupIconFile=assets\icon.ico
; WizardImageFile=assets\wizard_image.bmp
; WizardSmallImageFile=assets\wizard_small.bmp

; Разрешаем закрытие работающего JARVIS при обновлении
CloseApplications=yes
CloseApplicationsFilter=Jarvis.exe

[Languages]
Name: "russian"; MessagesFile: "compiler:Languages\Russian.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"
Name: "autostart"; Description: "Запускать при старте Windows / Start with Windows"; GroupDescription: "Дополнительно / Additional:"

[Files]
; === Основной exe ===
Source: "{#MyAppBuildDir}\Jarvis.exe"; DestDir: "{app}"; Flags: ignoreversion

; === Qt DLL и зависимости (после windeployqt) ===
Source: "{#MyAppBuildDir}\*.dll"; DestDir: "{app}"; Flags: ignoreversion

; === Qt плагины (платформы, стили, TLS) ===
Source: "{#MyAppBuildDir}\platforms\*"; DestDir: "{app}\platforms"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#MyAppBuildDir}\styles\*"; DestDir: "{app}\styles"; Flags: ignoreversion recursesubdirs createallsubdirs; Check: DirExists(ExpandConstant('{#MyAppBuildDir}\styles'))
Source: "{#MyAppBuildDir}\tls\*"; DestDir: "{app}\tls"; Flags: ignoreversion recursesubdirs createallsubdirs; Check: DirExists(ExpandConstant('{#MyAppBuildDir}\tls'))
Source: "{#MyAppBuildDir}\networkinformation\*"; DestDir: "{app}\networkinformation"; Flags: ignoreversion recursesubdirs createallsubdirs; Check: DirExists(ExpandConstant('{#MyAppBuildDir}\networkinformation'))

; === Visual C++ Redistributable ===
; Скачай vc_redist.x64.exe с https://aka.ms/vs/17/release/vc_redist.x64.exe
; и положи в папку redist\
Source: "redist\vc_redist.x64.exe"; DestDir: "{tmp}"; Flags: deleteafterinstall; Check: not VCRedistInstalled

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
Name: "{group}\{cm:UninstallProgram,{#MyAppName}}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Registry]
; Автозагрузка
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; \
    ValueType: string; ValueName: "JARVIS"; \
    ValueData: """{app}\{#MyAppExeName}"""; \
    Flags: uninsdeletevalue; Tasks: autostart

[Run]
; Устанавливаем VC++ Redistributable если нужно (тихо)
Filename: "{tmp}\vc_redist.x64.exe"; Parameters: "/install /quiet /norestart"; \
    StatusMsg: "Установка Visual C++ Runtime..."; \
    Flags: waituntilterminated; Check: not VCRedistInstalled

; Запускаем JARVIS после установки
Filename: "{app}\{#MyAppExeName}"; \
    Description: "{cm:LaunchProgram,{#MyAppName}}"; \
    Flags: nowait postinstall skipifsilent

[UninstallDelete]
; Очищаем пользовательские данные при полном удалении
Type: filesandordirs; Name: "{localappdata}\JARVIS Project"

[Code]
// Проверяем наличие Visual C++ Redistributable 2015-2022
function VCRedistInstalled(): Boolean;
var
    Version: String;
begin
    Result := False;
    // MSVC 2015-2022 x64
    if RegQueryStringValue(HKLM, 'SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\x64',
                           'Version', Version) then
    begin
        Result := True;
    end;
    // Альтернативный ключ
    if not Result then
    begin
        if RegKeyExists(HKLM,
            'SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\x64') then
            Result := True;
    end;
    // Ещё один вариант через Wow6432Node
    if not Result then
    begin
        if RegKeyExists(HKLM,
            'SOFTWARE\WOW6432Node\Microsoft\VisualStudio\14.0\VC\Runtimes\x64') then
            Result := True;
    end;
end;

// Проверка существования директории
function DirExists(Dir: String): Boolean;
begin
    Result := DirExists(Dir);
end;
