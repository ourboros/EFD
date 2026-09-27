# ==============================================================================
# EFD macOS Distribution Packaging Script (v2.0.0)
# ==============================================================================
$ErrorActionPreference = "Stop"

$ProjectRoot = $PSScriptRoot
$DistDir = Join-Path $ProjectRoot "build\package_macos\EFD-v2.0.0-macOS"
$ZipRootOutput = Join-Path $ProjectRoot "EFD-v2.0.0-macOS.zip"
$ZipBuildOutput = Join-Path $ProjectRoot "build\EFD-v2.0.0-macOS.zip"

Write-Host "==================================================================" -ForegroundColor Cyan
Write-Host "  [EFD] Packaging macOS Distribution (v2.0.0)..." -ForegroundColor Cyan
Write-Host "==================================================================" -ForegroundColor Cyan

# 1. Clean and prepare directories
if (Test-Path $DistDir) {
    Remove-Item -Recurse -Force $DistDir
}
New-Item -ItemType Directory -Path $DistDir -Force | Out-Null

$appBundle = Join-Path $DistDir "EFD.app"
$appContents = Join-Path $appBundle "Contents"
$appMacOS = Join-Path $appContents "MacOS"
$appResources = Join-Path $appContents "Resources"

New-Item -ItemType Directory -Path $appMacOS -Force | Out-Null
New-Item -ItemType Directory -Path $appResources -Force | Out-Null

# 2. Copy Info.plist and AppIcon.icns
Write-Host " -> Copying macOS Bundle metadata and AppIcon.icns..." -ForegroundColor Green
Copy-Item (Join-Path $ProjectRoot "macos\Info.plist") $appContents -Force
Copy-Item (Join-Path $ProjectRoot "macos\AppIcon.icns") (Join-Path $appResources "AppIcon.icns") -Force

# 3. Copy graphic assets to Resources
Write-Host " -> Copying assets to Resources..." -ForegroundColor Green
Copy-Item (Join-Path $ProjectRoot "assets") $appResources -Recurse -Force
Copy-Item (Join-Path $ProjectRoot "design") $appResources -Recurse -Force

# 4. Copy source code and build tools
Write-Host " -> Copying cross-platform sources..." -ForegroundColor Green
Copy-Item (Join-Path $ProjectRoot "include") $DistDir -Recurse -Force
Copy-Item (Join-Path $ProjectRoot "src") $DistDir -Recurse -Force
Copy-Item (Join-Path $ProjectRoot "macos") $DistDir -Recurse -Force
Copy-Item (Join-Path $ProjectRoot "assets") $DistDir -Recurse -Force
Copy-Item (Join-Path $ProjectRoot "design") $DistDir -Recurse -Force
Copy-Item (Join-Path $ProjectRoot "CMakeLists.txt") $DistDir -Force

# 5. Copy installation script and documentation
if (Test-Path (Join-Path $ProjectRoot "macos\Install-EFD.command")) {
    Copy-Item (Join-Path $ProjectRoot "macos\Install-EFD.command") (Join-Path $DistDir "Install-EFD.command") -Force
}
if (Test-Path (Join-Path $ProjectRoot "macos\README_macOS.md")) {
    Copy-Item (Join-Path $ProjectRoot "macos\README_macOS.md") (Join-Path $DistDir "README_macOS.md") -Force
}

# 6. Compress to ZIP package
Write-Host " -> Packaging into EFD-v2.0.0-macOS.zip..." -ForegroundColor Green
if (Test-Path $ZipRootOutput) { Remove-Item -Force $ZipRootOutput }
Compress-Archive -Path "$DistDir\*" -DestinationPath $ZipRootOutput -Force
Copy-Item $ZipRootOutput $ZipBuildOutput -Force

$zipItem = Get-Item $ZipRootOutput
$mbSize = [Math]::Round($zipItem.Length / 1MB, 2)
Write-Host "==================================================================" -ForegroundColor Cyan
Write-Host "  [SUCCESS] macOS Distribution Package Created Successfully!" -ForegroundColor Green
Write-Host "  Path: $($zipItem.FullName) ($mbSize MB)" -ForegroundColor Green
Write-Host "==================================================================" -ForegroundColor Cyan
