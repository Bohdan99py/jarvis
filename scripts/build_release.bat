@echo off
REM -------------------------------------------------------
REM build_release.bat — Полная сборка релиза J.A.R.V.I.S.
REM
REM Делает ВСЁ:
REM   1. Собирает Release через CMake
REM   2. Копирует Qt DLL через windeployqt
REM   3. Скачивает VC++ Redistributable
REM   4. (опционально) Собирает установщик через Inno Setup
REM
REM Использование:
REM   scripts\build_release.bat
REM   scripts\build_release.bat "C:\Qt\6.8.0\msvc2022_64"
REM   scripts\build_release.bat "C:\Qt\6.11.0\msvc2022_64" --installer
REM -------------------------------------------------------

setlocal enabledelayedexpansion

set QT_DIR=%~1
if "%QT_DIR%"=="" (
    REM Пробуем найти Qt автоматически
    if exist "C:\Qt\6.11.0\msvc2022_64" (
        set QT_DIR=C:\Qt\6.11.0\msvc2022_64
    ) else if exist "C:\Qt\6.8.0\msvc2022_64" (
        set QT_DIR=C:\Qt\6.8.0\msvc2022_64
    ) else if exist "C:\Qt\6.7.0\msvc2022_64" (
        set QT_DIR=C:\Qt\6.7.0\msvc2022_64
    ) else (
        echo [ERROR] Qt not found! Specify path: scripts\build_release.bat "C:\Qt\6.x.x\msvc2022_64"
        exit /b 1
    )
)

set BUILD_INSTALLER=%2
REM Отдельный каталог от повседневной сборки. Раньше здесь стоял тот же
REM "build", что использует CLion/Ninja, и CMake отказывался работать:
REM "generator Visual Studio 17 2022 does not match the generator used
REM previously: Ninja" — то есть релиз нельзя было собрать, не снеся
REM рабочую сборку, а после релиза она пересобиралась с нуля.
set BUILD_DIR=build_release
set RELEASE_DIR=%BUILD_DIR%\release_package
set REDIST_DIR=redist

echo.
echo ============================================================
echo   J.A.R.V.I.S. Release Builder
echo ============================================================
echo   Qt:    %QT_DIR%
echo   Build: %BUILD_DIR%
echo   Out:   %RELEASE_DIR%
echo ============================================================
echo.

REM ========================================
REM Шаг 1: CMake + сборка
REM ========================================
echo [1/5] Configuring CMake...

if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"

REM Ninja, а не "Visual Studio 17 2022": генератор с зашитым номером версии
REM отказывается работать на любой другой установленной студии ("could not
REM find any instance of Visual Studio"), а компилятор всё равно берётся из
REM окружения vcvars, которое этот скрипт требует. Ninja же одинаково
REM работает на VS 17/18 и совпадает с тем, чем собирается проект каждый
REM день, так что релиз не расходится со сборкой разработчика.
cmake -S . -B "%BUILD_DIR%" ^
    -G Ninja ^
    -DCMAKE_PREFIX_PATH="%QT_DIR%" ^
    -DCMAKE_BUILD_TYPE=Release

if errorlevel 1 (
    echo [ERROR] CMake configuration failed!
    echo.
    echo Make sure you have:
    echo   - Run this from a Developer Command Prompt ^(vcvars64^)
    echo   - Qt 6.x installed at %QT_DIR%
    exit /b 1
)

echo [2/5] Building Release...

cmake --build "%BUILD_DIR%" --parallel
if errorlevel 1 (
    echo [ERROR] Build failed!
    exit /b 1
)

echo   Build OK.
echo.

REM ========================================
REM Шаг 2: Подготовка release_package
REM ========================================
echo [3/5] Preparing release package...

if exist "%RELEASE_DIR%" rmdir /s /q "%RELEASE_DIR%"
mkdir "%RELEASE_DIR%"

REM Находим Jarvis.exe (может быть в Release\ или bin\)
set EXE_PATH=
if exist "%BUILD_DIR%\Release\Jarvis.exe" (
    set EXE_PATH=%BUILD_DIR%\Release\Jarvis.exe
) else if exist "%BUILD_DIR%\bin\Jarvis.exe" (
    set EXE_PATH=%BUILD_DIR%\bin\Jarvis.exe
) else if exist "%BUILD_DIR%\Jarvis.exe" (
    set EXE_PATH=%BUILD_DIR%\Jarvis.exe
) else (
    echo [ERROR] Jarvis.exe not found in build directory!
    echo Searched: %BUILD_DIR%\Release\, %BUILD_DIR%\bin\, %BUILD_DIR%\
    exit /b 1
)

echo   Found: %EXE_PATH%
copy "%EXE_PATH%" "%RELEASE_DIR%\" >nul

REM ========================================
REM Шаг 3: windeployqt — копирует все Qt DLL
REM ========================================
echo [4/5] Running windeployqt (copying Qt dependencies)...

set WINDEPLOYQT=%QT_DIR%\bin\windeployqt.exe
if not exist "%WINDEPLOYQT%" (
    set WINDEPLOYQT=%QT_DIR%\bin\windeployqt6.exe
)
if not exist "%WINDEPLOYQT%" (
    echo [WARNING] windeployqt not found at %QT_DIR%\bin\
    echo           Qt DLLs will NOT be included!
    echo           You need to copy them manually.
    goto :skip_deploy
)

REM --qmldir ОБЯЗАТЕЛЕН. Экраны JARVIS написаны на QML, но лежат внутри
REM .qrc и попадают прямо в бинарник — снаружи windeployqt их не видит и
REM без этого ключа не разворачивает НИ ОДНОГО QML-модуля. Qt6Quick.dll
REM при этом копируется как обычная зависимость, поэтому сборка выглядит
REM целой, а в установленной программе каталога qml\ просто нет: любой
REM `import QtQuick` падает, и каждое QML-окно (Work Modes, Training
REM Center, User Center, Organize, Task Board, Vision Center) открывается
REM пустым прямоугольником. Указываем исходники — по ним сканируются
REM импорты и подтягиваются QtQuick, QtQuick.Controls, QtQuick.Layouts,
REM QtQuick.Effects и их плагины.
"%WINDEPLOYQT%" "%RELEASE_DIR%\Jarvis.exe" ^
    --release ^
    --qmldir "%CD%\src" ^
    --no-translations ^
    --no-opengl-sw ^
    --no-system-d3d-compiler ^
    --no-compiler-runtime ^
    --dir "%RELEASE_DIR%"

if errorlevel 1 (
    echo [WARNING] windeployqt reported errors, but continuing...
)

:skip_deploy
echo.

REM ========================================
REM Шаг 3.5: внешние инструменты (OCR и PDF)
REM ========================================
REM windeployqt знает только про Qt — сторонние exe он не видит, поэтому
REM Tesseract и Poppler в пакет не попадали вовсе. Для пользователя это
REM выглядело как "чтение PDF не работает" (Vision Center честно показывал
REM Poppler красным), хотя оба инструмента лежат в redist\ репозитория и
REM в отладочной сборке находятся по относительному пути ..\redist\.
REM
REM Каталоги выбраны не произвольно: ровно эти пути перебирает
REM OcrExtractor::pdftoppmPath()/tesseractPath(), так что копия должна
REM лечь именно сюда, иначе она есть, но не находится.
echo [4.5/5] Copying OCR/PDF tools...

REM Tesseract копируется БЕЗ tessdata, а языковые модели — поштучно.
REM В redist лежат 124 языка на 668 МБ, и это был основной вес
REM установщика. Лишние модели не только занимают место: Tesseract
REM прогоняет страницу по каждой перечисленной модели, поэтому каждый
REM ненужный язык — это ещё и время распознавания.
REM
REM Список должен совпадать с `wanted` в OcrExtractor::buildLanguageString():
REM модель без записи в том списке не используется, а запись без модели
REM молча отбрасывается — расходятся они тихо, поэтому держим их рядом.
REM osd — не язык, а модель определения ориентации страницы; нужна
REM Tesseract'у при автоповороте, весит мало.
if exist "%REDIST_DIR%\Tesseract-OCR\tesseract.exe" (
    if not exist "%RELEASE_DIR%\Tesseract-OCR\tessdata" mkdir "%RELEASE_DIR%\Tesseract-OCR\tessdata"
    xcopy /E /I /Y /Q /EXCLUDE:scripts\tessdata_exclude.txt ^
        "%REDIST_DIR%\Tesseract-OCR" "%RELEASE_DIR%\Tesseract-OCR" >nul
    for %%L in (eng rus fra ron osd) do (
        if exist "%REDIST_DIR%\Tesseract-OCR\tessdata\%%L.traineddata" (
            copy /Y "%REDIST_DIR%\Tesseract-OCR\tessdata\%%L.traineddata" ^
                "%RELEASE_DIR%\Tesseract-OCR\tessdata\" >nul
        ) else (
            echo   [WARNING] tessdata\%%L.traineddata missing
        )
    )
    echo   Tesseract OCR: copied ^(eng, rus, fra, ron^)
) else (
    echo   [WARNING] Tesseract not found in %REDIST_DIR% — screen text reading will be off
)

set POPPLER_SRC=
for /d %%D in ("%REDIST_DIR%\poppler-*") do set POPPLER_SRC=%%D\Library\bin
if defined POPPLER_SRC (
    if exist "!POPPLER_SRC!\pdftoppm.exe" (
        if not exist "%RELEASE_DIR%\redist\poppler\bin" mkdir "%RELEASE_DIR%\redist\poppler\bin"
        xcopy /E /I /Y /Q "!POPPLER_SRC!" "%RELEASE_DIR%\redist\poppler\bin" >nul
        echo   Poppler: copied
    ) else (
        echo   [WARNING] Poppler binaries not found — PDF reading will be off
    )
) else (
    echo   [WARNING] No poppler-* folder in %REDIST_DIR% — PDF reading will be off
)
echo.

REM ========================================
REM Шаг 4: Visual C++ Redistributable
REM ========================================
echo [5/5] Checking VC++ Redistributable...

if not exist "%REDIST_DIR%" mkdir "%REDIST_DIR%"

if not exist "%REDIST_DIR%\vc_redist.x64.exe" (
    echo   Downloading VC++ Redistributable...
    echo   (from https://aka.ms/vs/17/release/vc_redist.x64.exe)

    REM Пробуем PowerShell
    powershell -Command "try { Invoke-WebRequest -Uri 'https://aka.ms/vs/17/release/vc_redist.x64.exe' -OutFile '%REDIST_DIR%\vc_redist.x64.exe' -UseBasicParsing } catch { Write-Host 'Download failed' }" 2>nul

    if not exist "%REDIST_DIR%\vc_redist.x64.exe" (
        REM Пробуем curl
        curl -L -o "%REDIST_DIR%\vc_redist.x64.exe" "https://aka.ms/vs/17/release/vc_redist.x64.exe" 2>nul
    )

    if exist "%REDIST_DIR%\vc_redist.x64.exe" (
        echo   Downloaded OK.
    ) else (
        echo   [WARNING] Failed to download. Installer will work but
        echo             users may need to install VC++ Runtime manually.
        echo   Download manually: https://aka.ms/vs/17/release/vc_redist.x64.exe
        echo   Place in: %REDIST_DIR%\vc_redist.x64.exe
    )
) else (
    echo   VC++ Redistributable already present.
)

echo.

REM ========================================
REM Результат
REM ========================================
echo ============================================================
echo   BUILD COMPLETE
echo ============================================================
echo.
echo   Release package: %RELEASE_DIR%\
echo.
echo   Files:
dir /b "%RELEASE_DIR%\*.exe" "%RELEASE_DIR%\*.dll" 2>nul | findstr /v "^$"
echo.

REM Считаем общий размер
for /f "tokens=3" %%a in ('dir /-c "%RELEASE_DIR%" /s ^| findstr /c:"File(s)"') do (
    echo   Total size: %%a bytes
)

echo.

REM ========================================
REM Опционально: сборка установщика
REM ========================================
if "%BUILD_INSTALLER%"=="--installer" (
    echo Building installer with Inno Setup...

    set ISCC="C:\Program Files (x86)\Inno Setup 6\ISCC.exe"
    if not exist !ISCC! (
        echo [ERROR] Inno Setup not found!
        echo Install from: https://jrsoftware.org/isdl.php
        goto :done
    )

    if not exist "%BUILD_DIR%\installer" mkdir "%BUILD_DIR%\installer"

    !ISCC! installer.iss
    if errorlevel 1 (
        echo [ERROR] Installer build failed!
    ) else (
        echo.
        echo   Installer: %BUILD_DIR%\installer\JARVIS-Setup-*.exe
    )
)

:done
echo.
echo Next steps:
echo   1. Test: %RELEASE_DIR%\Jarvis.exe
echo   2. Build installer: scripts\build_release.bat "%QT_DIR%" --installer
echo   3. Or: "C:\Program Files (x86)\Inno Setup 6\ISCC.exe" installer.iss
echo   4. For auto-release: git tag v2.0.0 ^&^& git push origin v2.0.0
echo.

endlocal
