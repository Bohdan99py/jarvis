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
set BUILD_DIR=build
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

cmake -S . -B "%BUILD_DIR%" ^
    -G "Visual Studio 17 2022" -A x64 ^
    -DCMAKE_PREFIX_PATH="%QT_DIR%" ^
    -DCMAKE_BUILD_TYPE=Release

if errorlevel 1 (
    echo [ERROR] CMake configuration failed!
    echo.
    echo Make sure you have:
    echo   - Visual Studio 2022 with C++ workload
    echo   - Qt 6.x installed at %QT_DIR%
    exit /b 1
)

echo [2/5] Building Release...

cmake --build "%BUILD_DIR%" --config Release --parallel
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

"%WINDEPLOYQT%" "%RELEASE_DIR%\Jarvis.exe" ^
    --release ^
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
