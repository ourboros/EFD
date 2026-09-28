Add-Type -AssemblyName System.Drawing

$sourcePath = "E:\Project\EFD\design\1x\資產 9.png"
if (-not (Test-Path $sourcePath)) {
    Write-Host "File not found: $sourcePath"
    exit 1
}

$img = [System.Drawing.Image]::FromFile($sourcePath)
$w = $img.Width
$h = $img.Height
$aspect = $w / $h
Write-Host "Source Image Size: Width = $w, Height = $h, Aspect = $aspect"

# Ensure all asset directories have 資產 9.png as logo.png and 資產 9.png
$destDirs = @(
    "E:\Project\EFD\assets",
    "E:\Project\EFD\src\ui\assets",
    "E:\Project\EFD\build\vs-x64\Release\assets",
    "E:\Project\EFD\build\vs-x64\Release\design\1x"
)

foreach ($dir in $destDirs) {
    if (-not (Test-Path $dir)) {
        New-Item -ItemType Directory -Path $dir -Force | Out-Null
    }
    Copy-Item $sourcePath (Join-Path $dir "資產 9.png") -Force
    Copy-Item $sourcePath (Join-Path $dir "logo.png") -Force
    Copy-Item $sourcePath (Join-Path $dir "logo9.png") -Force
}

# Generate 256x256 icon with 100% preserved aspect ratio (transparent background letterboxed)
$sizes = @(16, 32, 48, 64, 128, 256)
$bitmaps = @()

foreach ($s in $sizes) {
    $canvas = New-Object System.Drawing.Bitmap $s, $s, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [System.Drawing.Graphics]::FromImage($canvas)
    $g.Clear([System.Drawing.Color]::Transparent)
    $g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
    $g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::HighQuality
    $g.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality

    # Fit keeping 100% exact aspect ratio
    if ($aspect -ge 1.0) {
        $drawW = $s
        $drawH = [int][Math]::Round($s / $aspect)
        $drawX = 0
        $drawY = [int][Math]::Round(($s - $drawH) / 2)
    } else {
        $drawH = $s
        $drawW = [int][Math]::Round($s * $aspect)
        $drawY = 0
        $drawX = [int][Math]::Round(($s - $drawW) / 2)
    }

    $g.DrawImage($img, $drawX, $drawY, $drawW, $drawH)
    $g.Dispose()
    $bitmaps += $canvas
}

# Save as PNG multi-resolution or primary 256 icon
$canvas256 = $bitmaps[-1]
$canvas256.Save("E:\Project\EFD\assets\app_256.png", [System.Drawing.Imaging.ImageFormat]::Png)

# Convert to ICO format
$icoStream = New-Object System.IO.FileStream("E:\Project\EFD\src\platform\windows\app.ico", [System.IO.FileMode]::Create)
$icon = [System.Drawing.Icon]::FromHandle($canvas256.GetHicon())
$icon.Save($icoStream)
$icoStream.Close()
$icon.Dispose()

Copy-Item "E:\Project\EFD\src\platform\windows\app.ico" "E:\Project\EFD\assets\app.ico" -Force
if (Test-Path "E:\Project\EFD\build\vs-x64\Release\assets") {
    Copy-Item "E:\Project\EFD\src\platform\windows\app.ico" "E:\Project\EFD\build\vs-x64\Release\assets\app.ico" -Force
}

$img.Dispose()
foreach ($b in $bitmaps) { $b.Dispose() }
Write-Host "Icon generated successfully with 100% aspect ratio!"
