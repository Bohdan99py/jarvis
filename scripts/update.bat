@echo off
REM -------------------------------------------------------
REM update.bat — Применение обновлений J.A.R.V.I.S.
REM
REM Заменяет DLL-файлы из папки updates/ без пересборки.
REM Старые файлы сохраняются в backup/
REM
REM Использование:
REM   update.bat              — применить все обновления
REM   update.bat rollback     — откатить последнее обновление
REM -------------------------------------------------------

setlocal

set BIN_DIR=%~dp0build\bin
set UPDATES_DIR=%~dp0build\bin\updates
set BACKUP_DIR=%~dp0build\bin\backup

if "%1"=="rollback" goto :rollback

REM === Применение обновлений ===

if not exist "%UPDATES_DIR%" (
    echo [JARVIS] No updates found in %UPDATES_DIR%
    exit /b 0
)

REM Проверяем наличие обновлений
set UPDATE_COUNT=0
for %%f in ("%UPDATES_DIR%\*.dll") do set /a UPDATE_COUNT+=1

if %UPDATE_COUNT%==0 (
    echo [JARVIS] No DLL updates found.
    exit /b 0
)

echo [JARVIS] Found %UPDATE_COUNT% update(s).

REM Создаём бэкап
if not exist "%BACKUP_DIR%" mkdir "%BACKUP_DIR%"

REM Бэкап с timestamp
set TIMESTAMP=%date:~-4%%date:~3,2%%date:~0,2%_%time:~0,2%%time:~3,2%
set TIMESTAMP=%TIMESTAMP: =0%
set BACKUP_SUBDIR=%BACKUP_DIR%\%TIMESTAMP%
mkdir "%BACKUP_SUBDIR%"

echo [JARVIS] Backing up current files to %BACKUP_SUBDIR%...

REM Копируем текущие DLL в бэкап
for %%f in ("%UPDATES_DIR%\*.dll") do (
    set FNAME=%%~nxf
    if exist "%BIN_DIR%\%%~nxf" (
        copy "%BIN_DIR%\%%~nxf" "%BACKUP_SUBDIR%\" >nul
        echo   Backed up: %%~nxf
    )
)

REM Закрываем JARVIS если запущен
tasklist /FI "IMAGENAME eq JarvisApp.exe" 2>nul | find /I "JarvisApp.exe" >nul
if not errorlevel 1 (
    echo [JARVIS] Stopping JARVIS...
    taskkill /F /IM JarvisApp.exe >nul 2>&1
    timeout /t 2 /nobreak >nul
)

REM Применяем обновления
echo [JARVIS] Applying updates...
for %%f in ("%UPDATES_DIR%\*.dll") do (
    copy /Y "%%f" "%BIN_DIR%\" >nul
    echo   Updated: %%~nxf
)

REM Очищаем папку обновлений
del /Q "%UPDATES_DIR%\*.dll" 2>nul

echo.
echo [JARVIS] Update complete! Start JARVIS with:
echo   %BIN_DIR%\JarvisApp.exe
echo.

exit /b 0

:rollback
REM === Откат последнего обновления ===

if not exist "%BACKUP_DIR%" (
    echo [JARVIS] No backups found.
    exit /b 1
)

REM Находим последний бэкап
set LATEST_BACKUP=
for /d %%d in ("%BACKUP_DIR%\*") do set LATEST_BACKUP=%%d

if "%LATEST_BACKUP%"=="" (
    echo [JARVIS] No backups found.
    exit /b 1
)

echo [JARVIS] Rolling back to: %LATEST_BACKUP%

REM Закрываем JARVIS
tasklist /FI "IMAGENAME eq JarvisApp.exe" 2>nul | find /I "JarvisApp.exe" >nul
if not errorlevel 1 (
    echo [JARVIS] Stopping JARVIS...
    taskkill /F /IM JarvisApp.exe >nul 2>&1
    timeout /t 2 /nobreak >nul
)

REM Восстанавливаем из бэкапа
for %%f in ("%LATEST_BACKUP%\*.dll") do (
    copy /Y "%%f" "%BIN_DIR%\" >nul
    echo   Restored: %%~nxf
)

REM Удаляем использованный бэкап
rmdir /s /q "%LATEST_BACKUP%"

echo.
echo [JARVIS] Rollback complete!

endlocal
