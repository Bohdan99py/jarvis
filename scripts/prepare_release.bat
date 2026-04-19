@echo off
REM -------------------------------------------------------
REM prepare_release.bat — Подготовка пакета для установщика
REM
REM 1. Собирает Release
REM 2. Копирует Qt DLL через windeployqt
REM 3. Создаёт папку release_package/ готовую для Inno Setup
REM
REM Использование:
REM   scripts\prepare_release.bat
REM   scripts\prepare_release.bat "C:\Qt\6.8.0\msvc2022_64"
REM -------------------------------------------------------

setlocal

set QT_DIR=%~1
if "%QT_DIR%"=="" set QT_DIR=C:\Qt\6.11.0\msvc2022_64

set BUILD_DIR=build
set RELEASE_DIR=%BUILD_DIR%\release_package
set BIN_DIR=%BUILD_DIR%\bin

echo ============================================================
echo  J.A.R.V.I.S. Release Preparation
echo ============================================================
echo.

REM === Шаг 1: Сборка Release ===
echo [1/4] Building Release...
if not exist %BUILD_DIR% mkdir %BUILD_DIR%

cmake -S . -B %BUILD_DIR% -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH="%QT_DIR%"
if errorlevel 1 (
    echo [ERROR] CMake configuration failed!
    exit /b 1
)

cmake --build %BUILD_DIR% --config Release --parallel
if errorlevel 1 (
    echo [ERROR] Build failed!
    exit /b 1
)

echo   Build OK.
echo.

REM === Шаг 2: Создаём release_package ===
echo [2/4] Preparing release package...

if exist "%RELEASE_DIR%" rmdir /s /q "%RELEASE_DIR%"
mkdir "%RELEASE_DIR%"
mkdir "%RELEASE_DIR%\plugins"

REM Копируем наши бинарники
copy "%BIN_DIR%\JarvisApp.exe" "%RELEASE_DIR%\" >nul
copy "%BIN_DIR%\JarvisCore.dll" "%RELEASE_DIR%\" >nul

REM Копируем плагины если есть
for %%f in ("%BIN_DIR%\JarvisPlugin_*.dll") do (
    copy "%%f" "%RELEASE_DIR%\plugins\" >nul
    echo   Plugin: %%~nxf
)

REM Копируем plugins.json
copy "plugins\plugins.json" "%RELEASE_DIR%\plugins\" >nul

echo   Core files copied.
echo.

REM === Шаг 3: Qt DLL через windeployqt ===
echo [3/4] Running windeployqt...

set WINDEPLOYQT=%QT_DIR%\bin\windeployqt.exe

if not exist "%WINDEPLOYQT%" (
    echo [WARNING] windeployqt not found at %WINDEPLOYQT%
    echo   Trying PATH...
    set WINDEPLOYQT=windeployqt
)

"%WINDEPLOYQT%" ^
    --release ^
    --no-translations ^
    --no-opengl-sw ^
    --no-system-d3d-compiler ^
    --no-compiler-runtime ^
    "%RELEASE_DIR%\JarvisApp.exe"

if errorlevel 1 (
    echo [WARNING] windeployqt had issues, but continuing...
)

echo   Qt DLLs deployed.
echo.

REM === Шаг 4: Проверяем VC Runtime ===
echo [4/4] Checking Visual C++ Runtime...

REM Копируем VC runtime если есть
set VC_REDIST_DIR=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Redist\MSVC
if exist "%VC_REDIST_DIR%" (
    for /d %%d in ("%VC_REDIST_DIR%\*") do (
        if exist "%%d\x64\Microsoft.VC*.CRT" (
            copy "%%d\x64\Microsoft.VC*.CRT\*.dll" "%RELEASE_DIR%\" >nul 2>&1
            echo   VC Runtime copied.
        )
    )
) else (
    echo   [INFO] VC Runtime not found locally.
    echo   Users will need Visual C++ Redistributable installed.
)

echo.
echo ============================================================
echo  Release package ready: %RELEASE_DIR%
echo ============================================================
echo.

REM Показываем содержимое
echo Files:
dir /b "%RELEASE_DIR%\*.exe" "%RELEASE_DIR%\*.dll" 2>nul

echo.
echo Plugins:
dir /b "%RELEASE_DIR%\plugins\*" 2>nul

echo.
echo Total size:
for /f "tokens=3" %%a in ('dir /-c "%RELEASE_DIR%" ^| findstr "File(s)"') do echo   %%a bytes

echo.
echo Next steps:
echo   1. Test: %RELEASE_DIR%\JarvisApp.exe
echo   2. Build installer: "C:\Program Files (x86)\Inno Setup 6\ISCC.exe" installer.iss
echo   3. Or zip: 7z a JARVIS-v2.0.0-win64.zip %RELEASE_DIR%\*
echo.

endlocal
