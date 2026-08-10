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
; Пути ниже (release_package\...) относительны SourceDir, а не корня
; проекта. Без явного SourceDir они разрешались в <корень>\release_package,
; которого не существует — сборка лежит в каталоге релизной сборки.
SourceDir=build_release
OutputDir=installer
OutputBaseFilename=Jarvis_Setup_v3.6.3
SetupIconFile=..\assets\jarvis.ico

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "russian"; MessagesFile: "compiler:Languages\Russian.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked
Name: "autostart"; Description: "Launch JARVIS on Windows startup"; GroupDescription: "Startup options"; Flags: unchecked

; Модульные скиллы — лего-блоки знаний JARVIS. Пользователь при установке
; выбирает, какие ставить; остальные можно доустановить позже (меню
; Настройки -> Скиллы JARVIS -> Импортировать, или просто положить папку
; в "Документы\Jarvis Data\skills").
[Components]
Name: "core"; Description: "JARVIS core"; Types: full compact custom; Flags: fixed
Name: "skills"; Description: "Skills (knowledge modules)"; Types: full
Name: "skills\coding"; Description: "Programmer / IDE agent (writes code into your project files)"; Types: full
Name: "skills\electronics"; Description: "Electronics engineer (KiCad, circuits, embedded firmware)"; Types: full
Name: "skills\philosopher"; Description: "Philosopher (deep conversations, ethics, logic)"; Types: full

[Files]
Source: "release_package\jarvis.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "release_package\*.dll"; DestDir: "{app}"; Flags: ignoreversion skipifsourcedoesntexist
Source: "release_package\haarcascade_frontalface_default.xml"; DestDir: "{app}"; Flags: ignoreversion skipifsourcedoesntexist
Source: "release_package\*.txt"; DestDir: "{app}"; Flags: ignoreversion skipifsourcedoesntexist
; QML-модули. Без этой строки qml\ не попадал в установку, даже когда
; windeployqt его создавал, и все QML-экраны открывались пустыми —
; см. комментарий про --qmldir в scripts\build_release.bat.
Source: "release_package\qml\*"; DestDir: "{app}\qml"; Flags: ignoreversion recursesubdirs createallsubdirs skipifsourcedoesntexist
Source: "release_package\platforms\*"; DestDir: "{app}\platforms"; Flags: ignoreversion recursesubdirs createallsubdirs skipifsourcedoesntexist
Source: "release_package\styles\*"; DestDir: "{app}\styles"; Flags: ignoreversion recursesubdirs createallsubdirs skipifsourcedoesntexist
Source: "release_package\imageformats\*"; DestDir: "{app}\imageformats"; Flags: ignoreversion recursesubdirs createallsubdirs skipifsourcedoesntexist
Source: "release_package\iconengines\*"; DestDir: "{app}\iconengines"; Flags: ignoreversion recursesubdirs createallsubdirs skipifsourcedoesntexist
Source: "release_package\networkinformation\*"; DestDir: "{app}\networkinformation"; Flags: ignoreversion recursesubdirs createallsubdirs skipifsourcedoesntexist
Source: "release_package\tls\*"; DestDir: "{app}\tls"; Flags: ignoreversion recursesubdirs createallsubdirs skipifsourcedoesntexist
Source: "release_package\generic\*"; DestDir: "{app}\generic"; Flags: ignoreversion recursesubdirs createallsubdirs skipifsourcedoesntexist
Source: "release_package\sqldrivers\*"; DestDir: "{app}\sqldrivers"; Flags: ignoreversion recursesubdirs createallsubdirs skipifsourcedoesntexist
Source: "release_package\multimedia\*"; DestDir: "{app}\multimedia"; Flags: ignoreversion recursesubdirs createallsubdirs skipifsourcedoesntexist
Source: "release_package\audio\*"; DestDir: "{app}\audio"; Flags: ignoreversion recursesubdirs createallsubdirs skipifsourcedoesntexist
Source: "release_package\Tesseract-OCR\*"; DestDir: "{app}\Tesseract-OCR"; Flags: ignoreversion recursesubdirs createallsubdirs skipifsourcedoesntexist
Source: "release_package\plugins\*"; DestDir: "{app}\plugins"; Flags: ignoreversion recursesubdirs createallsubdirs skipifsourcedoesntexist
Source: "release_package\skills\README.md"; DestDir: "{app}\skills"; Flags: ignoreversion skipifsourcedoesntexist; Components: skills
Source: "release_package\skills\coding\*"; DestDir: "{app}\skills\coding"; Flags: ignoreversion recursesubdirs createallsubdirs skipifsourcedoesntexist; Components: skills\coding
Source: "release_package\skills\electronics\*"; DestDir: "{app}\skills\electronics"; Flags: ignoreversion recursesubdirs createallsubdirs skipifsourcedoesntexist; Components: skills\electronics
Source: "release_package\skills\philosopher\*"; DestDir: "{app}\skills\philosopher"; Flags: ignoreversion recursesubdirs createallsubdirs skipifsourcedoesntexist; Components: skills\philosopher

[Icons]
Name: "{autoprograms}\JARVIS"; Filename: "{app}\jarvis.exe"
Name: "{autodesktop}\JARVIS"; Filename: "{app}\jarvis.exe"; Tasks: desktopicon
Name: "{userstartup}\JARVIS"; Filename: "{app}\jarvis.exe"; Tasks: autostart

[Run]
Filename: "{app}\jarvis.exe"; Description: "{cm:LaunchProgram,JARVIS}"; Flags: nowait postinstall skipifsilent
