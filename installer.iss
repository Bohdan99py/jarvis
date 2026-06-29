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
Name: "autostart"; Description: "Launch JARVIS on Windows startup"; GroupDescription: "Startup options"; Flags: unchecked

[Files]
Source: "release_package\jarvis.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "release_package\*.dll"; DestDir: "{app}"; Flags: ignoreversion skipifsourcedoesntexist
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

[Icons]
Name: "{autoprograms}\JARVIS"; Filename: "{app}\jarvis.exe"
Name: "{autodesktop}\JARVIS"; Filename: "{app}\jarvis.exe"; Tasks: desktopicon
Name: "{userstartup}\JARVIS"; Filename: "{app}\jarvis.exe"; Tasks: autostart

[Run]
Filename: "{app}\jarvis.exe"; Description: "{cm:LaunchProgram,JARVIS}"; Flags: nowait postinstall skipifsilent
