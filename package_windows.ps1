# ==============================================================================
# EFD Windows 原生發行版自動化打包腳本
# ==============================================================================
$ErrorActionPreference = "Stop"

$ProjectRoot = $PSScriptRoot
$BuildDir = Join-Path $ProjectRoot "build\vs-x64\Release"
$DistDir = Join-Path $ProjectRoot "build\package_windows\EFD-v2.0.0-Windows"
$ZipRootOutput = Join-Path $ProjectRoot "EFD-v2.0.0-Windows.zip"
$ZipBuildOutput = Join-Path $ProjectRoot "build\EFD-v2.0.0-Windows.zip"

Write-Host "==================================================================" -ForegroundColor Cyan
Write-Host "  [EFD] 開始打包 Windows 桌面端分發套件..." -ForegroundColor Cyan
Write-Host "==================================================================" -ForegroundColor Cyan

# 1. 確保 Release 二進制檔案存在
$ExeSource = Join-Path $BuildDir "efd_gui.exe"
if (-not (Test-Path $ExeSource)) {
    Write-Host " [編譯] 找不到 Release efd_gui.exe，正在使用 CMake 執行 Release 建置..." -ForegroundColor Yellow
    $cmakePath = "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
    & $cmakePath --build (Join-Path $ProjectRoot "build\vs-x64") --config Release --target efd_gui
}

# 2. 清理並建立分發目錄
if (Test-Path $DistDir) {
    Remove-Item -Recurse -Force $DistDir
}
New-Item -ItemType Directory -Path $DistDir -Force | Out-Null
New-Item -ItemType Directory -Path (Join-Path $DistDir "assets") -Force | Out-Null
New-Item -ItemType Directory -Path (Join-Path $DistDir "design\1x") -Force | Out-Null

# 3. 複製主執行檔 (命名為乾淨的 EFD.exe)
Write-Host " -> 複製主程式: EFD.exe" -ForegroundColor Green
Copy-Item $ExeSource (Join-Path $DistDir "EFD.exe") -Force

# 4. 複製 MSVC 執行庫 DLL (保證在任何乾淨 Windows 系統上隨插即用免安裝)
Write-Host " -> 複製獨立執行庫 DLL (msvcp140.dll, vcruntime140.dll, vcruntime140_1.dll)..." -ForegroundColor Green
$redistDir = "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Redist\MSVC\14.51.36231\x64\Microsoft.VC145.CRT"
if (Test-Path $redistDir) {
    Copy-Item (Join-Path $redistDir "msvcp140.dll") $DistDir -Force
    Copy-Item (Join-Path $redistDir "vcruntime140.dll") $DistDir -Force
    Copy-Item (Join-Path $redistDir "vcruntime140_1.dll") $DistDir -Force
}

# 5. 複製資源檔案
Write-Host " -> 複製圖形資產 (assets / design)..." -ForegroundColor Green
Copy-Item (Join-Path $ProjectRoot "assets\*") (Join-Path $DistDir "assets") -Recurse -Force
Copy-Item (Join-Path $ProjectRoot "design\*") (Join-Path $DistDir "design") -Recurse -Force

# 6. 建立一鍵啟動腳本與捷徑建立腳本
Write-Host " -> 建立「啟動 EFD.bat」與「建立桌面捷徑.bat」..." -ForegroundColor Green

$startBatLines = @(
    '@echo off',
    'chcp 65001 >nul',
    'title EFD 眼睛特徵提取與即時疲勞監控研究系統',
    'cd /d "%~dp0"',
    '',
    'echo ==================================================================',
    'echo   EFD 眼睛特徵提取與即時疲勞監控研究系統 (Windows 桌面端)',
    'echo ==================================================================',
    'echo 正在啟動系統，請稍候...',
    '',
    'if not exist "EFD.exe" (',
    '    echo [錯誤] 找不到 EFD.exe，請確認檔案未被解壓縮軟體分離。',
    '    pause',
    '    exit /b 1',
    ')',
    '',
    'start "" "%~dp0EFD.exe"',
    'exit'
)
[System.IO.File]::WriteAllLines((Join-Path $DistDir "啟動 EFD.bat"), $startBatLines, [System.Text.Encoding]::GetEncoding(65001))

$shortcutBatLines = @(
    '@echo off',
    'chcp 65001 >nul',
    'cd /d "%~dp0"',
    '',
    'echo 正在建立 EFD 桌面捷徑...',
    '',
    'powershell -NoProfile -ExecutionPolicy Bypass -Command "$ws = New-Object -ComObject WScript.Shell; $s = $ws.CreateShortcut([System.IO.Path]::Combine([Environment]::GetFolderPath(''Desktop''), ''EFD 眼睛疲勞監測.lnk'')); $s.TargetPath = ''%~dp0EFD.exe''; $s.WorkingDirectory = ''%~dp0''; $s.Description = ''EFD 眼睛特徵提取與即時疲勞監控研究系統''; if (Test-Path ''%~dp0assets\app.ico'') { $s.IconLocation = ''%~dp0assets\app.ico''; } else { $s.IconLocation = ''%~dp0EFD.exe,0''; }; $s.Save();"',
    '',
    'if %ERRORLEVEL% equ 0 (',
    '    echo [成功] 桌面捷徑已建立完成！',
    ') else (',
    '    echo [提示] 捷徑建立失敗，您可以直接雙擊 EFD.exe 執行。',
    ')',
    'timeout /t 3 >nul',
    'exit'
)
[System.IO.File]::WriteAllLines((Join-Path $DistDir "建立桌面捷徑.bat"), $shortcutBatLines, [System.Text.Encoding]::GetEncoding(65001))

# 7. 複製使用說明文檔
Copy-Item (Join-Path $ProjectRoot "README_Windows.md") (Join-Path $DistDir "README_Windows.md") -Force
Copy-Item (Join-Path $ProjectRoot "README_Windows.md") (Join-Path $DistDir "使用說明.txt") -Force

# 8. 壓縮打包為 ZIP
Write-Host " -> 正在壓縮封裝為 EFD-v2.0.0-Windows.zip..." -ForegroundColor Green
if (Test-Path $ZipRootOutput) { Remove-Item -Force $ZipRootOutput }
Compress-Archive -Path "$DistDir\*" -DestinationPath $ZipRootOutput -Force
Copy-Item $ZipRootOutput $ZipBuildOutput -Force

$zipItem = Get-Item $ZipRootOutput
Write-Host "==================================================================" -ForegroundColor Cyan
Write-Host "  [SUCCESS] Windows 桌面端分發套件打包完成！" -ForegroundColor Green
Write-Host "  輸出路徑: $($zipItem.FullName) ($([Math]::Round($zipItem.Length / 1MB, 2)) MB)" -ForegroundColor Green
Write-Host "==================================================================" -ForegroundColor Cyan

