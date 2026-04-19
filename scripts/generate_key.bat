@echo off
REM -------------------------------------------------------
REM generate_key.bat — Генератор зашифрованного API-ключа
REM
REM Использование:
REM   scripts\generate_key.bat sk-ant-api03-xxxxxxx
REM
REM Выведет C++ код для вставки в core/embedded_key.h
REM -------------------------------------------------------

if "%~1"=="" (
    echo Использование: generate_key.bat ^<api-key^>
    echo Пример: generate_key.bat sk-ant-api03-xxxxxxxxxxxx
    exit /b 1
)

echo.
echo Generating encrypted key...
echo.

REM Используем PowerShell для XOR-шифрования
powershell -NoProfile -Command ^
    "$key = '%~1'; " ^
    "$xorKey = @(0x4A, 0x41, 0x52, 0x56, 0x49, 0x53, 0xDE, 0xAD, 0xBE, 0xEF, 0xCA, 0xFE, 0x13, 0x37, 0x42, 0x69); " ^
    "$bytes = [System.Text.Encoding]::UTF8.GetBytes($key); " ^
    "$encrypted = @(); " ^
    "for ($i = 0; $i -lt $bytes.Length; $i++) { " ^
    "    $encrypted += ($bytes[$i] -bxor $xorKey[$i %% $xorKey.Length]); " ^
    "} " ^
    "Write-Host '// Вставь это в core/embedded_key.h:'; " ^
    "Write-Host ''; " ^
    "$hex = ($encrypted | ForEach-Object { '0x{0:X2}' -f $_ }) -join ', '; " ^
    "$lines = @(); " ^
    "for ($i = 0; $i -lt $encrypted.Length; $i += 12) { " ^
    "    $chunk = $encrypted[$i..[Math]::Min($i+11, $encrypted.Length-1)]; " ^
    "    $hexChunk = ($chunk | ForEach-Object { '0x{0:X2}' -f $_ }) -join ', '; " ^
    "    $lines += '    ' + $hexChunk + ','; " ^
    "} " ^
    "Write-Host 'static constexpr uint8_t ENCRYPTED_KEY_DATA[] = {'; " ^
    "foreach ($line in $lines) { Write-Host $line; } " ^
    "Write-Host '};'; " ^
    "Write-Host ('static constexpr int ENCRYPTED_KEY_SIZE = ' + $encrypted.Length + ';'); " ^
    "Write-Host ''; " ^
    "Write-Host ('// Key length: ' + $key.Length + ' chars');"

echo.
echo Done! Copy the output above into core/embedded_key.h
echo (replace ENCRYPTED_KEY_DATA and ENCRYPTED_KEY_SIZE)
echo.
pause
