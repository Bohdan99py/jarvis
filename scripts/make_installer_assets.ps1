# -------------------------------------------------------
# make_installer_assets.ps1 — оформление установщика JARVIS
#
# Рисует баннеры мастера и генерирует звуки. Файлы складываются в
# assets\installer\ и попадают в installer.iss.
#
# Скрипт, а не готовые картинки в репозитории, по двум причинам:
# палитра берётся из src\common\jarvis_theme.h (поменяется тема —
# перерисуем одной командой), а звуки повторяют те же формулы, по
# которым AudioManager синтезирует звуки внутри приложения, — чтобы
# установщик звучал ровно как сам JARVIS.
#
# Запуск:  powershell -ExecutionPolicy Bypass -File scripts\make_installer_assets.ps1
# -------------------------------------------------------

Add-Type -AssemblyName System.Drawing

$ErrorActionPreference = 'Stop'

$root   = Split-Path -Parent $PSScriptRoot
$outDir = Join-Path $root 'assets\installer'
$logoPath = Join-Path $root 'assets\jarvis.png'

if (-not (Test-Path $outDir)) { New-Item -ItemType Directory -Path $outDir | Out-Null }

# --- Палитра (src\common\jarvis_theme.h) ---
$C = @{
    bg            = [System.Drawing.Color]::FromArgb(0x08, 0x0A, 0x0F)
    surface1      = [System.Drawing.Color]::FromArgb(0x0F, 0x13, 0x1A)
    surface2      = [System.Drawing.Color]::FromArgb(0x16, 0x1B, 0x24)
    accent        = [System.Drawing.Color]::FromArgb(0x66, 0xFC, 0xF1)
    accentMuted   = [System.Drawing.Color]::FromArgb(0x3F, 0xBF, 0xB6)
    onSurface     = [System.Drawing.Color]::FromArgb(0xE7, 0xEA, 0xF0)
    onSurfaceDim  = [System.Drawing.Color]::FromArgb(0x68, 0x70, 0x7E)
}

# ============================================================
#  Баннер мастера
# ============================================================

function New-Banner {
    param([int]$W, [int]$H, [string]$Path)

    # 24 бита намеренно: Inno местами показывает 32-битные BMP с мусором
    # в альфе, а прозрачность здесь всё равно не нужна.
    $bmp = New-Object System.Drawing.Bitmap($W, $H, [System.Drawing.Imaging.PixelFormat]::Format24bppRgb)
    $g   = [System.Drawing.Graphics]::FromImage($bmp)
    $g.SmoothingMode     = 'AntiAlias'
    $g.TextRenderingHint = 'ClearTypeGridFit'
    $g.InterpolationMode = 'HighQualityBicubic'

    $s = $W / 164.0   # всё остальное считается от базовых 164x314

    # Фон: вертикальный градиент, как у окна приложения
    $rect = New-Object System.Drawing.Rectangle(0, 0, $W, $H)
    $grad = New-Object System.Drawing.Drawing2D.LinearGradientBrush($rect, $C.surface1, $C.bg, 90.0)
    $g.FillRectangle($grad, $rect)
    $grad.Dispose()

    # Сетка HUD — очень тихая, её задача создать фактуру, а не рисунок
    $gridPen = New-Object System.Drawing.Pen([System.Drawing.Color]::FromArgb(10, 255, 255, 255), [float](1 * $s))
    for ($y = 0; $y -lt $H; $y += [int](18 * $s)) { $g.DrawLine($gridPen, 0, $y, $W, $y) }
    for ($x = 0; $x -lt $W; $x += [int](18 * $s)) { $g.DrawLine($gridPen, $x, 0, $x, $H) }
    $gridPen.Dispose()

    # Свечение за логотипом
    $glowR = [int](78 * $s)
    $cx = [int]($W / 2)
    $cy = [int](96 * $s)
    $glowPath = New-Object System.Drawing.Drawing2D.GraphicsPath
    $glowPath.AddEllipse($cx - $glowR, $cy - $glowR, $glowR * 2, $glowR * 2)
    $glow = New-Object System.Drawing.Drawing2D.PathGradientBrush($glowPath)
    $glow.CenterColor = [System.Drawing.Color]::FromArgb(78, 0x66, 0xFC, 0xF1)
    $glow.SurroundColors = @([System.Drawing.Color]::FromArgb(0, 0x66, 0xFC, 0xF1))
    $g.FillPath($glow, $glowPath)
    $glow.Dispose(); $glowPath.Dispose()

    # Логотип
    if (Test-Path $logoPath) {
        $logo = [System.Drawing.Image]::FromFile($logoPath)
        $ls = [int](104 * $s)
        $g.DrawImage($logo, [int]($cx - $ls / 2), [int]($cy - $ls / 2), $ls, $ls)
        $logo.Dispose()
    }

    # Слово JARVIS с разрядкой: рисуем посимвольно, GDI+ межбуквенный
    # интервал не умеет, а именно он даёт «приборную» строгость.
    $fontSize = [float](15 * $s)
    $font  = New-Object System.Drawing.Font('Segoe UI', $fontSize, [System.Drawing.FontStyle]::Bold, [System.Drawing.GraphicsUnit]::Pixel)
    $brush = New-Object System.Drawing.SolidBrush($C.accent)
    $word  = 'JARVIS'
    $track = [float](5 * $s)

    $total = 0.0
    foreach ($ch in $word.ToCharArray()) {
        $total += $g.MeasureString([string]$ch, $font).Width - ($fontSize * 0.28) + $track
    }
    $x = ($W - $total) / 2
    $wordY = [float](168 * $s)
    foreach ($ch in $word.ToCharArray()) {
        $g.DrawString([string]$ch, $font, $brush, $x, $wordY)
        $x += $g.MeasureString([string]$ch, $font).Width - ($fontSize * 0.28) + $track
    }
    $brush.Dispose(); $font.Dispose()

    # Подпись — тоже с разрядкой и заметно тише слова JARVIS, иначе
    # две строки спорят друг с другом за внимание.
    $small = New-Object System.Drawing.Font('Segoe UI', [float](7 * $s), [System.Drawing.FontStyle]::Regular, [System.Drawing.GraphicsUnit]::Pixel)
    $dim   = New-Object System.Drawing.SolidBrush($C.onSurfaceDim)
    $tag   = 'OFFLINE-FIRST ASSISTANT'
    $tagTrack = [float](1.6 * $s)

    $tagTotal = 0.0
    foreach ($ch in $tag.ToCharArray()) {
        $tagTotal += $g.MeasureString([string]$ch, $small).Width - (7 * $s * 0.30) + $tagTrack
    }
    $tx = ($W - $tagTotal) / 2
    $tagY = [float](194 * $s)
    foreach ($ch in $tag.ToCharArray()) {
        $g.DrawString([string]$ch, $small, $dim, $tx, $tagY)
        $tx += $g.MeasureString([string]$ch, $small).Width - (7 * $s * 0.30) + $tagTrack
    }
    $small.Dispose(); $dim.Dispose()

    # Акцентная черта у нижнего края — тот же приём, что в шапках окон
    $lineY = [int]($H - 34 * $s)
    $lineW = [int](54 * $s)
    $linePen = New-Object System.Drawing.Pen($C.accentMuted, [float](2 * $s))
    $g.DrawLine($linePen, [int](($W - $lineW) / 2), $lineY, [int](($W + $lineW) / 2), $lineY)
    $linePen.Dispose()

    $g.Dispose()
    $bmp.Save($Path, [System.Drawing.Imaging.ImageFormat]::Bmp)
    $bmp.Dispose()
    Write-Host "  $([System.IO.Path]::GetFileName($Path))  ${W}x${H}"
}

# ============================================================
#  Маленькая картинка в шапке страниц
# ============================================================

function New-SmallImage {
    param([int]$Size, [string]$Path)

    $bmp = New-Object System.Drawing.Bitmap($Size, $Size, [System.Drawing.Imaging.PixelFormat]::Format24bppRgb)
    $g   = [System.Drawing.Graphics]::FromImage($bmp)
    $g.SmoothingMode     = 'AntiAlias'
    $g.InterpolationMode = 'HighQualityBicubic'

    # Фон совпадает с фоном шапки страницы — иначе картинка будет
    # висеть тёмным квадратом на светлом поле.
    $bg = New-Object System.Drawing.SolidBrush($C.surface1)
    $g.FillRectangle($bg, 0, 0, $Size, $Size)
    $bg.Dispose()

    if (Test-Path $logoPath) {
        $logo = [System.Drawing.Image]::FromFile($logoPath)
        $pad  = [int]($Size * 0.06)
        $g.DrawImage($logo, $pad, $pad, $Size - 2 * $pad, $Size - 2 * $pad)
        $logo.Dispose()
    }

    $g.Dispose()
    $bmp.Save($Path, [System.Drawing.Imaging.ImageFormat]::Bmp)
    $bmp.Dispose()
    Write-Host "  $([System.IO.Path]::GetFileName($Path))  ${Size}x${Size}"
}

# ============================================================
#  Звуки — те же формулы, что в AudioManager::generateSounds()
# ============================================================

function Write-Wav {
    param([int16[]]$Samples, [int]$SampleRate, [string]$Path)

    $fs = [System.IO.File]::Create($Path)
    $bw = New-Object System.IO.BinaryWriter($fs)
    $dataBytes = $Samples.Length * 2

    $bw.Write([char[]]'RIFF'); $bw.Write([uint32](36 + $dataBytes))
    $bw.Write([char[]]'WAVE')
    $bw.Write([char[]]'fmt '); $bw.Write([uint32]16)
    $bw.Write([uint16]1); $bw.Write([uint16]1)
    $bw.Write([uint32]$SampleRate); $bw.Write([uint32]($SampleRate * 2))
    $bw.Write([uint16]2); $bw.Write([uint16]16)
    $bw.Write([char[]]'data'); $bw.Write([uint32]$dataBytes)
    foreach ($s in $Samples) { $bw.Write([int16]$s) }

    $bw.Close(); $fs.Close()
    Write-Host "  $([System.IO.Path]::GetFileName($Path))  $([math]::Round($Samples.Length / $SampleRate * 1000))ms"
}

$SR = 22050

# «Слушаю» — короткий яркий пинг G5. Встречает на первой странице.
$ping = New-Object System.Collections.Generic.List[int16]
$len = [int]($SR * 120 / 1000)
for ($i = 0; $i -lt $len; $i++) {
    $t = $i / $SR
    $env = 1.0 - $i / $len
    $env = $env * $env
    $ping.Add([int16]($env * 10000 * [math]::Sin(2 * [math]::PI * 783.99 * $t)))
}
Write-Wav -Samples $ping.ToArray() -SampleRate $SR -Path (Join-Path $outDir 'snd_welcome.wav')

# «Готово» — двухнотный подъём C5→E5. Звучит на последней странице.
$chime = New-Object System.Collections.Generic.List[int16]
$len1 = [int]($SR * 100 / 1000)
$len2 = [int]($SR * 120 / 1000)
for ($i = 0; $i -lt $len1; $i++) {
    $t = $i / $SR
    $env = 1.0 - $i / $len1
    $chime.Add([int16]($env * 12000 * [math]::Sin(2 * [math]::PI * 523.25 * $t)))
}
for ($i = 0; $i -lt $len2; $i++) {
    $t = $i / $SR
    $env = 1.0 - $i / $len2
    $chime.Add([int16]($env * 12000 * [math]::Sin(2 * [math]::PI * 659.25 * $t)))
}
Write-Wav -Samples $chime.ToArray() -SampleRate $SR -Path (Join-Path $outDir 'snd_done.wav')

# «Предупреждение» — низкий гудок A3. Звучит при отмене установки.
$buzz = New-Object System.Collections.Generic.List[int16]
$len = [int]($SR * 180 / 1000)
for ($i = 0; $i -lt $len; $i++) {
    $t = $i / $SR
    $env = 1.0 - $i / $len
    $sig = [math]::Sin(2 * [math]::PI * 220.0 * $t) + 0.3 * [math]::Sin(2 * [math]::PI * 440.0 * $t)
    $buzz.Add([int16]($env * 8000 * $sig))
}
Write-Wav -Samples $buzz.ToArray() -SampleRate $SR -Path (Join-Path $outDir 'snd_cancel.wav')

# ============================================================

Write-Host ''
Write-Host 'Баннеры:'
# Набор размеров, который Inno выбирает по масштабу экрана.
New-Banner -W 164 -H 314 -Path (Join-Path $outDir 'wizard.bmp')
New-Banner -W 192 -H 386 -Path (Join-Path $outDir 'wizard@125.bmp')
New-Banner -W 246 -H 459 -Path (Join-Path $outDir 'wizard@150.bmp')
New-Banner -W 328 -H 628 -Path (Join-Path $outDir 'wizard@200.bmp')

Write-Host ''
Write-Host 'Иконки страниц:'
New-SmallImage -Size 55  -Path (Join-Path $outDir 'wizard_small.bmp')
New-SmallImage -Size 64  -Path (Join-Path $outDir 'wizard_small@125.bmp')
New-SmallImage -Size 83  -Path (Join-Path $outDir 'wizard_small@150.bmp')
New-SmallImage -Size 110 -Path (Join-Path $outDir 'wizard_small@200.bmp')

Write-Host ''
Write-Host "Готово: $outDir"
