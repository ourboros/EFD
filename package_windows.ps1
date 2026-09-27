# ==============================================================================
# EFD Windows Native Release Packaging Script (v2.0.0)
# ==============================================================================
$ErrorActionPreference = "Stop"

$ProjectRoot = $PSScriptRoot
$BuildDir = Join-Path $ProjectRoot "build\vs-x64\Release"
$DistDir = Join-Path $ProjectRoot "build\package_windows\EFD-v2.0.0-Windows"
$ZipRootOutput = Join-Path $ProjectRoot "EFD-v2.0.0-Windows.zip"
$ZipBuildOutput = Join-Path $ProjectRoot "build\EFD-v2.0.0-Windows.zip"

Write-Host "==================================================================" -ForegroundColor Cyan
Write-Host "  [EFD] Packaging Windows Desktop Distribution (v2.0.0)..." -ForegroundColor Cyan
Write-Host "==================================================================" -ForegroundColor Cyan

# 1. Ensure Release binary exists
$ExeSource = Join-Path $BuildDir "efd_gui.exe"
if (-not (Test-Path $ExeSource)) {
    Write-Host " [Build] efd_gui.exe not found. Building Release via CMake..." -ForegroundColor Yellow
    $cmakePath = "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
    & $cmakePath --build (Join-Path $ProjectRoot "build\vs-x64") --config Release --target efd_gui
}

# 2. Clean and create distribution directory
if (Test-Path $DistDir) {
    Remove-Item -Recurse -Force $DistDir
}
New-Item -ItemType Directory -Path $DistDir -Force | Out-Null
New-Item -ItemType Directory -Path (Join-Path $DistDir "assets") -Force | Out-Null
New-Item -ItemType Directory -Path (Join-Path $DistDir "design\1x") -Force | Out-Null

# 3. Copy main executable
Write-Host " -> Copying executable: EFD.exe" -ForegroundColor Green
Copy-Item $ExeSource (Join-Path $DistDir "EFD.exe") -Force

# 4. Copy MSVC runtime DLLs
Write-Host " -> Copying MSVC Runtime DLLs..." -ForegroundColor Green
$redistDir = "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Redist\MSVC\14.51.36231\x64\Microsoft.VC145.CRT"
if (Test-Path $redistDir) {
    Copy-Item (Join-Path $redistDir "msvcp140.dll") $DistDir -Force
    Copy-Item (Join-Path $redistDir "vcruntime140.dll") $DistDir -Force
    Copy-Item (Join-Path $redistDir "vcruntime140_1.dll") $DistDir -Force
}

# 5. Copy graphic assets
Write-Host " -> Copying graphic assets..." -ForegroundColor Green
Copy-Item (Join-Path $ProjectRoot "assets\*") (Join-Path $DistDir "assets") -Recurse -Force
Copy-Item (Join-Path $ProjectRoot "design\*") (Join-Path $DistDir "design") -Recurse -Force

# 6. Create startup batch files
Write-Host " -> Creating startup batch files..." -ForegroundColor Green

$startBatLines = @(
    '@echo off',
    'chcp 65001 >nul',
    'title EFD Eye Fatigue Detection System',
    'cd /d "%~dp0"',
    '',
    'echo ==================================================================',
    'echo   EFD Eye Fatigue Detection System (v2.0.0)',
    'echo ==================================================================',
    'echo Launching application...',
    '',
    'if not exist "EFD.exe" (',
    '    echo [Error] EFD.exe not found in current directory.',
    '    pause',
    '    exit /b 1',
    ')',
    '',
    'start "" "%~dp0EFD.exe"',
    'exit'
)
$startBatPath = Join-Path $DistDir "Launch-EFD.bat"
Set-Content -Path $startBatPath -Value $startBatLines -Encoding UTF8

$shortcutBatLines = @(
    '@echo off',
    'chcp 65001 >nul',
    'cd /d "%~dp0"',
    'echo Creating desktop shortcut for EFD...',
    'powershell -NoProfile -ExecutionPolicy Bypass -Command "$ws = New-Object -ComObject WScript.Shell; $s = $ws.CreateShortcut([System.IO.Path]::Combine([Environment]::GetFolderPath(''Desktop''), ''EFD Fatigue Detection.lnk'')); $s.TargetPath = ''%~dp0EFD.exe''; $s.WorkingDirectory = ''%~dp0''; $s.Description = ''EFD Eye Fatigue Detection System''; if (Test-Path ''%~dp0assets\app.ico'') { $s.IconLocation = ''%~dp0assets\app.ico''; } else { $s.IconLocation = ''%~dp0EFD.exe,0''; }; $s.Save();"',
    'if %ERRORLEVEL% equ 0 ( echo [Success] Desktop shortcut created successfully! ) else ( echo [Notice] Direct shortcut creation skipped. )',
    'timeout /t 3 >nul',
    'exit'
)
$shortcutBatPath = Join-Path $DistDir "Create-Desktop-Shortcut.bat"
Set-Content -Path $shortcutBatPath -Value $shortcutBatLines -Encoding UTF8

# 7. Copy documentation
Copy-Item (Join-Path $ProjectRoot "README_Windows.md") (Join-Path $DistDir "README_Windows.md") -Force
Copy-Item (Join-Path $ProjectRoot "README_Windows.md") (Join-Path $DistDir "README.txt") -Force

# 8. Compress to ZIP package
Write-Host " -> Packaging into EFD-v2.0.0-Windows.zip..." -ForegroundColor Green
if (Test-Path $ZipRootOutput) { Remove-Item -Force $ZipRootOutput }
Compress-Archive -Path "$DistDir\*" -DestinationPath $ZipRootOutput -Force
Copy-Item $ZipRootOutput $ZipBuildOutput -Force

$zipItem = Get-Item $ZipRootOutput
Write-Host "==================================================================" -ForegroundColor Cyan
Write-Host "  [SUCCESS] Windows Distribution Package Created Successfully!" -ForegroundColor Green
Write-Host "  Path: $($zipItem.FullName) ($([Math]::Round($zipItem.Length / 1MB, 2)) MB)" -ForegroundColor Green
Write-Host "==================================================================" -ForegroundColor Cyan
