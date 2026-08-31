; ============================================================
;  JARVIS — установщик
;
;  Оформление под само приложение: тёмный фон, бирюзовый акцент,
;  логотип и те же звуки, которыми JARVIS отвечает в интерфейсе.
;  Картинки и WAV рисует scripts\make_installer_assets.ps1 —
;  палитра там взята из src\common\jarvis_theme.h, поэтому смена
;  темы приложения перерисовывает установщик одной командой.
;
;  Что Inno перекрасить НЕ даёт: системные кнопки (Далее, Отмена)
;  рисует Windows, и цвет им можно задать только собственной
;  отрисовкой — от неё отказались, слишком хрупко для установщика.
;  Всё остальное — фон, тексты, списки, поля, полоса прогресса —
;  перекрашено в [Code] ниже.
; ============================================================

#define AppVer "3.8.6"

[Setup]
AppId={{JARVIS-CORE-REPLACE-WITH-YOUR-GUID}}
AppName=JARVIS
AppVersion={#AppVer}
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
OutputBaseFilename=Jarvis_Setup_v{#AppVer}
SetupIconFile=..\assets\jarvis.ico

; --- Оформление ---
; Несколько размеров каждой картинки: Inno сам выбирает подходящий
; под масштаб экрана, иначе на 150% баннер выглядит мыльным.
WizardImageFile=..\assets\installer\wizard.bmp,..\assets\installer\wizard@125.bmp,..\assets\installer\wizard@150.bmp,..\assets\installer\wizard@200.bmp
WizardSmallImageFile=..\assets\installer\wizard_small.bmp,..\assets\installer\wizard_small@125.bmp,..\assets\installer\wizard_small@150.bmp,..\assets\installer\wizard_small@200.bmp
; Баннер занимает страницу целиком — без белой полосы под ним.
WizardImageStretch=yes
; Тёмная тема тут ни при чём: это цвет служебных элементов Inno.
WindowResizable=no
ShowLanguageDialog=auto
DisableWelcomePage=no
AppCopyright=JARVIS
UninstallDisplayIcon={app}\jarvis.exe
UninstallDisplayName=JARVIS {#AppVer}

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "russian"; MessagesFile: "compiler:Languages\Russian.isl"

[Messages]
; Свои формулировки вместо казённых. Установщик — первое, что видит
; человек от JARVIS, и говорить он должен так же, как приложение.
english.WelcomeLabel1=JARVIS
english.WelcomeLabel2=An offline-first assistant that lives on your machine: voice, memory, and skills that keep working without the network.%n%nVersion {#AppVer}
english.FinishedHeadingLabel=JARVIS is ready
english.FinishedLabel=Everything is in place. Models and voices download themselves on first run, so the first launch may take a minute.
english.SelectDirBrowseLabel=JARVIS will be installed here. Choose another folder if you like.
english.ClickNext=
russian.WelcomeLabel1=JARVIS
russian.WelcomeLabel2=Офлайн-ассистент, который живёт на твоей машине: голос, память и навыки продолжают работать без сети.%n%nВерсия {#AppVer}
russian.FinishedHeadingLabel=JARVIS готов
russian.FinishedLabel=Всё на месте. Модели и голоса докачиваются сами при первом запуске, поэтому первый старт может занять минуту.
russian.SelectDirBrowseLabel=JARVIS установится сюда. Можно выбрать другую папку.
russian.ClickNext=

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

; Inno копирует файлы, но НЕ удаляет то, чего в новой сборке уже нет.
; После урезания языков Tesseract со 124 до 4 установщик похудел, а
; установленная папка осталась на 1.2 ГБ: 120 лишних моделей просто
; лежали от прошлых установок. Чистим tessdata перед копированием —
; нужные пять моделей кладутся следом в [Files].
;
; Такой же случай возможен с любым файлом, выпавшим из сборки, поэтому
; правило стоит именно на каталоге, а не на списке имён.
[InstallDelete]
Type: files; Name: "{app}\Tesseract-OCR\tessdata\*.traineddata"

[Files]
; Всё содержимое пакета одной строкой — вместо перечисления каталогов и
; масок по типам файлов.
;
; Перечисление ломалось трижды подряд, каждый раз по-новому: не было
; строки для qml\ (все QML-экраны открывались пустыми и не работали
; уведомления); маска *.dll не подхватывала QtWebEngineProcess.exe —
; это .exe, а не .dll — и приложение падало с FATAL при запуске; следом
; недоставало resources\ и translations\, нужных тому же WebEngine.
;
; Список того, что кладёт windeployqt, зависит от версии Qt и включённых
; модулей и меняется без предупреждения, поэтому поимённый список here
; обречён отставать. Копируем каталог целиком: лишний файл безвреден,
; недостающий — это FATAL при старте у пользователя.
;
; Excludes оставляет skills\ выборочным — они ниже раздаются по
; Components, и пользователь может их не ставить.
Source: "release_package\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs; Excludes: "skills\*"
Source: "release_package\skills\README.md"; DestDir: "{app}\skills"; Flags: ignoreversion skipifsourcedoesntexist; Components: skills
Source: "release_package\skills\coding\*"; DestDir: "{app}\skills\coding"; Flags: ignoreversion recursesubdirs createallsubdirs skipifsourcedoesntexist; Components: skills\coding
Source: "release_package\skills\electronics\*"; DestDir: "{app}\skills\electronics"; Flags: ignoreversion recursesubdirs createallsubdirs skipifsourcedoesntexist; Components: skills\electronics
Source: "release_package\skills\philosopher\*"; DestDir: "{app}\skills\philosopher"; Flags: ignoreversion recursesubdirs createallsubdirs skipifsourcedoesntexist; Components: skills\philosopher

; Звуки не устанавливаются — они нужны только мастеру, поэтому
; dontcopy: файл лежит внутри setup.exe и распаковывается во временную
; папку по требованию (см. PlayJarvisSound).
Source: "..\assets\installer\snd_welcome.wav"; Flags: dontcopy
Source: "..\assets\installer\snd_done.wav";    Flags: dontcopy
Source: "..\assets\installer\snd_cancel.wav";  Flags: dontcopy

[Icons]
Name: "{autoprograms}\JARVIS"; Filename: "{app}\jarvis.exe"
Name: "{autodesktop}\JARVIS"; Filename: "{app}\jarvis.exe"; Tasks: desktopicon
Name: "{userstartup}\JARVIS"; Filename: "{app}\jarvis.exe"; Tasks: autostart

[Run]
Filename: "{app}\jarvis.exe"; Description: "{cm:LaunchProgram,JARVIS}"; Flags: nowait postinstall skipifsilent

; ============================================================
;  Оформление и звук
; ============================================================
[Code]

const
  // Палитра приложения (src\common\jarvis_theme.h). Pascal Script
  // хранит цвет как BGR, поэтому #66FCF1 записывается как $F1FC66.
  clBg            = $0F0A08;   // #080A0F
  clSurface1      = $1A130F;   // #0F131A
  clSurface2      = $241B16;   // #161B24
  clSurface3      = $2F241E;   // #1E242F
  clAccent        = $F1FC66;   // #66FCF1
  clAccentMuted   = $B6BF3F;   // #3FBFB6
  clOnSurface     = $F0EAE7;   // #E7EAF0
  clOnSurfaceVar  = $B2A39A;   // #9AA3B2
  clOnSurfaceDim  = $7E7068;   // #68707E

  // Сообщения полосе прогресса. Работают только после того, как с
  // элемента снята тема Windows — см. UnthemeControl.
  PBM_SETBARCOLOR = $409;
  PBM_SETBKCOLOR  = $2001;

  SND_FILENAME = $20000;
  SND_ASYNC    = $0001;
  SND_NODEFAULT = $0002;

function PlaySound(pszSound: string; hmod: THandle; fdwSound: LongWord): Boolean;
  external 'PlaySoundW@winmm.dll stdcall';

// Снимает с элемента визуальную тему Windows. Без этого элемент рисует
// себя сам по системной теме и любые заданные цвета игнорирует.
function SetWindowTheme(hwnd: HWND; pszSubAppName: string; pszSubIdList: string): Integer;
  external 'SetWindowTheme@uxtheme.dll stdcall';

function SendMessage(hWnd: HWND; Msg: LongWord; wParam: Longint; lParam: Longint): Longint;
  external 'SendMessageW@user32.dll stdcall';

var
  SoundsReady: Boolean;
  AccentStrip: TPanel;

// ============================================================
//  Звук
// ============================================================

// Звуки распаковываются один раз и только когда действительно нужны:
// при тихой установке (/SILENT) мастера нет и звучать нечему.
procedure EnsureSounds;
begin
  if SoundsReady then Exit;
  ExtractTemporaryFile('snd_welcome.wav');
  ExtractTemporaryFile('snd_done.wav');
  ExtractTemporaryFile('snd_cancel.wav');
  SoundsReady := True;
end;

procedure PlayJarvisSound(const FileName: string);
begin
  if WizardSilent then Exit;
  try
    EnsureSounds;
    // Асинхронно: мастер не должен ждать, пока доиграет звук.
    PlaySound(ExpandConstant('{tmp}\') + FileName, 0,
              SND_FILENAME or SND_ASYNC or SND_NODEFAULT);
  except
    // Нет звуковой карты, занято устройство, не распаковалось — не та
    // причина, по которой установка должна прерваться.
  end;
end;

// ============================================================
//  Перекраска
// ============================================================

procedure StyleLabel(L: TNewStaticText; AColor: TColor);
begin
  if L = nil then Exit;
  L.Font.Color := AColor;
  // Color у TNewStaticText не трогаем — подложкой служит панель.
end;

procedure StyleEdit(E: TEdit);
begin
  if E = nil then Exit;
  E.Color := clSurface2;
  E.Font.Color := clOnSurface;
end;

// Проходит по всем детям панели и красит то, что умеет краситься.
// Перечислять элементы поимённо бессмысленно: Inno добавляет и
// переименовывает их между версиями, а пропущенный элемент — это
// белое пятно посреди тёмного окна.
procedure StyleContainer(Parent: TWinControl);
var
  I: Integer;
  Child: TControl;
begin
  if Parent = nil then Exit;

  for I := 0 to Parent.ControlCount - 1 do
  begin
    Child := Parent.Controls[I];

    if Child is TNewStaticText then
      StyleLabel(TNewStaticText(Child), clOnSurfaceVar)
    else if Child is TNewCheckListBox then
    begin
      TNewCheckListBox(Child).Color := clSurface2;
      TNewCheckListBox(Child).Font.Color := clOnSurface;
      // Без этого список рисует свои строки по системной теме и
      // остаётся светлым островом.
      SetWindowTheme(TNewCheckListBox(Child).Handle, '', '');
    end
    else if Child is TNewEdit then
      StyleEdit(TNewEdit(Child))
    else if Child is TNewMemo then
    begin
      TNewMemo(Child).Color := clSurface2;
      TNewMemo(Child).Font.Color := clOnSurface;
    end
    else if Child is TNewListBox then
    begin
      TNewListBox(Child).Color := clSurface2;
      TNewListBox(Child).Font.Color := clOnSurface;
    end
    else if Child is TNewCheckBox then
    begin
      TNewCheckBox(Child).Color := clSurface1;
      TNewCheckBox(Child).Font.Color := clOnSurface;
    end
    else if Child is TNewRadioButton then
    begin
      TNewRadioButton(Child).Color := clSurface1;
      TNewRadioButton(Child).Font.Color := clOnSurface;
    end
    else if Child is TPanel then
    begin
      TPanel(Child).Color := clSurface1;
      TPanel(Child).Font.Color := clOnSurface;
      StyleContainer(TPanel(Child));
    end
    else if Child is TWinControl then
      StyleContainer(TWinControl(Child));
  end;
end;

procedure StyleProgressBar;
begin
  if WizardForm.ProgressGauge = nil then Exit;
  // Сначала снять тему, потом красить: под темой Windows сообщения
  // PBM_SET*COLOR игнорируются молча.
  SetWindowTheme(WizardForm.ProgressGauge.Handle, '', '');
  SendMessage(WizardForm.ProgressGauge.Handle, PBM_SETBARCOLOR, 0, clAccent);
  SendMessage(WizardForm.ProgressGauge.Handle, PBM_SETBKCOLOR, 0, clSurface2);
end;

procedure StyleWizard;
begin
  WizardForm.Color := clSurface1;
  WizardForm.Font.Name := 'Segoe UI';

  // Крупные заголовки страниц — акцентом, как заголовки экранов в
  // приложении; подзаголовок тише.
  WizardForm.PageNameLabel.Font.Color := clAccent;
  WizardForm.PageNameLabel.Color := clSurface1;
  WizardForm.PageDescriptionLabel.Font.Color := clOnSurfaceDim;
  WizardForm.PageDescriptionLabel.Color := clSurface1;

  WizardForm.MainPanel.Color := clSurface1;
  WizardForm.Bevel.Visible := False;      // светлая линейка от системной темы
  WizardForm.Bevel1.Visible := False;

  WizardForm.WelcomeLabel1.Font.Color := clAccent;
  WizardForm.WelcomeLabel1.Font.Size := 20;
  WizardForm.WelcomeLabel2.Font.Color := clOnSurfaceVar;
  WizardForm.FinishedHeadingLabel.Font.Color := clAccent;
  WizardForm.FinishedLabel.Font.Color := clOnSurfaceVar;

  // У TNewNotebook в скрипте нет свойства Color: фон страниц берётся
  // от формы, а она уже тёмная.
  StyleContainer(WizardForm.InnerNotebook);
  StyleContainer(WizardForm.OuterNotebook);

  // Тексты, которые должны читаться в полную силу.
  StyleLabel(WizardForm.SelectDirLabel, clOnSurface);
  StyleLabel(WizardForm.DiskSpaceLabel, clOnSurfaceDim);
  StyleLabel(WizardForm.StatusLabel, clOnSurfaceVar);
  StyleLabel(WizardForm.FilenameLabel, clOnSurfaceDim);
  StyleEdit(WizardForm.DirEdit);

  StyleProgressBar;

  // Бирюзовая полоса над кнопками — тот же приём, что в шапках окон
  // приложения. Заодно она закрывает светлый шов системной панели.
  AccentStrip := TPanel.Create(WizardForm);
  AccentStrip.Parent := WizardForm;
  AccentStrip.BevelOuter := bvNone;
  AccentStrip.Color := clAccentMuted;
  AccentStrip.Left := 0;
  AccentStrip.Width := WizardForm.ClientWidth;
  AccentStrip.Height := ScaleY(2);
  AccentStrip.Top := WizardForm.NextButton.Top - ScaleY(14);
  AccentStrip.Anchors := [akLeft, akRight, akBottom];
end;

// ============================================================
//  События мастера
// ============================================================

procedure InitializeWizard;
begin
  StyleWizard;
  PlayJarvisSound('snd_welcome.wav');
end;

procedure CurPageChanged(CurPageID: Integer);
begin
  // Страницы Inno создаёт по мере показа, поэтому одной покраски на
  // старте не хватает: то, что появилось позже, останется светлым.
  StyleContainer(WizardForm.InnerNotebook);
  StyleProgressBar;

  if CurPageID = wpFinished then
    PlayJarvisSound('snd_done.wav');
end;

procedure CancelButtonClick(CurPageID: Integer; var Cancel, Confirm: Boolean);
begin
  if CurPageID <> wpFinished then
    PlayJarvisSound('snd_cancel.wav');
end;
