@echo off
REM -------------------------------------------------------
REM build.bat — Сборка J.A.R.V.I.S. на Windows
REM
REM Использование:
REM   build.bat              — полная сборка (Debug)
REM   build.bat Release      — сборка Release
REM   build.bat clean        — очистить build/
REM   build.bat core         — пересобрать только ядро
REM   build.bat app          — пересобрать только приложение
REM -------------------------------------------------------

setlocal

set BUILD_DIR=build
set CONFIG=%1

if "%CONFIG%"=="" set CONFIG=Debug
if "%CONFIG%"=="clean" (
    echo Cleaning build directory...
    rmdir /s /q %BUILD_DIR% 2>nul
    echo Done.
    exit /b 0
)

REM Создаём build директорию
if not exist %BUILD_DIR% mkdir %BUILD_DIR%

REM Конфигурация
echo [JARVIS] Configuring CMake...
cmake -S . -B %BUILD_DIR% -G "Visual Studio 17 2022" -A x64

if errorlevel 1 (
    echo [ERROR] CMake configuration failed!
    echo.
    echo Make sure:
    echo   1. CMake is installed and in PATH
    echo   2. Visual Studio 2022 is installed
    echo   3. Qt6 path is correct in CMakeLists.txt
    echo      or pass -DCMAKE_PREFIX_PATH="C:/Qt/6.x.x/msvc2022_64"
    exit /b 1
)

REM Сборка
if "%CONFIG%"=="core" (
    echo [JARVIS] Building core only...
    cmake --build %BUILD_DIR% --target JarvisCore --config Debug --parallel
) else if "%CONFIG%"=="app" (
    echo [JARVIS] Building app only...
    cmake --build %BUILD_DIR% --target JarvisApp --config Debug --parallel
) else (
    echo [JARVIS] Building all (%CONFIG%)...
    cmake --build %BUILD_DIR% --config %CONFIG% --parallel
)

if errorlevel 1 (
    echo [ERROR] Build failed!
    exit /b 1
)

echo.
echo [JARVIS] Build successful!
echo   Output: %BUILD_DIR%\bin\
echo.

REM Показываем что собрано
dir /b %BUILD_DIR%\bin\*.exe %BUILD_DIR%\bin\*.dll 2>nul

endlocal
