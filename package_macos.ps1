# ==============================================================================
# EFD macOS 原生發行版自動化打包腳本
# ==============================================================================
$ErrorActionPreference = "Stop"

$ProjectRoot = $PSScriptRoot
$DistDir = Join-Path $ProjectRoot "build\package_macos\EFD-v1.0.0-macOS"
$ZipRootOutput = Join-Path $ProjectRoot "EFD-v1.0.0-macOS.zip"
$ZipBuildOutput = Join-Path $ProjectRoot "build\EFD-v1.0.0-macOS.zip"

Write-Host "==================================================================" -ForegroundColor Cyan
Write-Host "  [EFD] 開始打包 macOS 分發套件..." -ForegroundColor Cyan
Write-Host "==================================================================" -ForegroundColor Cyan

# 1. 清理並建立目錄
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

# 2. 複製 Info.plist 與 AppIcon.icns
Write-Host " -> 複製 macOS Bundle 詮釋資料與「資產 10」Retina 圖示 (AppIcon.icns)..." -ForegroundColor Green
Copy-Item (Join-Path $ProjectRoot "macos\Info.plist") $appContents -Force
Copy-Item (Join-Path $ProjectRoot "macos\AppIcon.icns") (Join-Path $appResources "AppIcon.icns") -Force

# 3. 複製資產至 Resources
Write-Host " -> 複製全套資產檔案至 EFD.app/Contents/Resources..." -ForegroundColor Green
Copy-Item (Join-Path $ProjectRoot "assets") $appResources -Recurse -Force
Copy-Item (Join-Path $ProjectRoot "design") $appResources -Recurse -Force
Copy-Item (Join-Path $ProjectRoot "design\1x\資產 10.png") (Join-Path $appResources "資產 10.png") -Force
Copy-Item (Join-Path $ProjectRoot "design\1x\資產 10.png") (Join-Path $appResources "logo10.png") -Force
Copy-Item (Join-Path $ProjectRoot "design\1x\資產 10.png") (Join-Path $appResources "logo.png") -Force

# 4. 複製專案原始碼與建置工具
Write-Host " -> 複製跨平台原始碼 (include, src, CMakeLists.txt, macos 腳本)..." -ForegroundColor Green
Copy-Item (Join-Path $ProjectRoot "include") $DistDir -Recurse -Force
Copy-Item (Join-Path $ProjectRoot "src") $DistDir -Recurse -Force
Copy-Item (Join-Path $ProjectRoot "macos") $DistDir -Recurse -Force
Copy-Item (Join-Path $ProjectRoot "assets") $DistDir -Recurse -Force
Copy-Item (Join-Path $ProjectRoot "design") $DistDir -Recurse -Force
Copy-Item (Join-Path $ProjectRoot "CMakeLists.txt") $DistDir -Force

# 5. 複製根目錄一鍵安裝與說明
if (Test-Path (Join-Path $ProjectRoot "macos\Install-EFD.command")) {
    Copy-Item (Join-Path $ProjectRoot "macos\Install-EFD.command") (Join-Path $DistDir "Install-EFD.command") -Force
}
if (Test-Path (Join-Path $ProjectRoot "macos\README_macOS.md")) {
    Copy-Item (Join-Path $ProjectRoot "macos\README_macOS.md") (Join-Path $DistDir "README_macOS.md") -Force
}

# 6. 壓縮打包為 ZIP
Write-Host " -> 正在壓縮封裝為 EFD-v1.0.0-macOS.zip..." -ForegroundColor Green
if (Test-Path $ZipRootOutput) { Remove-Item -Force $ZipRootOutput }
Compress-Archive -Path "$DistDir\*" -DestinationPath $ZipRootOutput -Force
Copy-Item $ZipRootOutput $ZipBuildOutput -Force

$zipItem = Get-Item $ZipRootOutput
Write-Host "==================================================================" -ForegroundColor Cyan
Write-Host "  [SUCCESS] macOS 分發套件打包完成！" -ForegroundColor Green
Write-Host "  輸出路徑: $($zipItem.FullName) ($([Math]::Round($zipItem.Length / 1MB, 2)) MB)" -ForegroundColor Green
Write-Host "==================================================================" -ForegroundColor Cyan
