@echo off
setlocal
cd /d "%~dp0"
echo ==================================================================
echo   [EFD] Windows 桌面端分發套件自動化打包
echo ==================================================================
powershell -NoProfile -ExecutionPolicy Bypass -Command "& {
    $projectRoot = '%~dp0'.TrimEnd('\');
    $buildDir = Join-Path $projectRoot 'build\vs-x64\Release';
    $distDir = Join-Path $projectRoot 'build\package_windows\EFD-v1.0.0-Windows';
    $zipRoot = Join-Path $projectRoot 'EFD-v1.0.0-Windows.zip';
    $zipBuild = Join-Path $projectRoot 'build\EFD-v1.0.0-Windows.zip';
    $cmake = 'C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe';

    Write-Host ' -> [1/5] 編譯 Release 目標 (efd_gui.exe)...' -ForegroundColor Cyan;
    & $cmake --build (Join-Path $projectRoot 'build\vs-x64') --config Release --target efd_gui;

    Write-Host ' -> [2/5] 準備分發目錄結構...' -ForegroundColor Cyan;
    if (Test-Path $distDir) { Remove-Item -Recurse -Force $distDir }
    New-Item -ItemType Directory -Path (Join-Path $distDir 'assets') -Force | Out-Null;
    New-Item -ItemType Directory -Path (Join-Path $distDir 'design\1x') -Force | Out-Null;

    Write-Host ' -> [3/5] 複製二進制檔案與相依 DLL...' -ForegroundColor Cyan;
    Copy-Item (Join-Path $buildDir 'efd_gui.exe') (Join-Path $distDir 'EFD.exe') -Force;

    $redistDir = 'C:\Program Files\Microsoft Visual Studio\18\Community\VC\Redist\MSVC\14.51.36231\x64\Microsoft.VC145.CRT';
    if (Test-Path $redistDir) {
        Copy-Item (Join-Path $redistDir 'msvcp140.dll') $distDir -Force;
        Copy-Item (Join-Path $redistDir 'vcruntime140.dll') $distDir -Force;
        Copy-Item (Join-Path $redistDir 'vcruntime140_1.dll') $distDir -Force;
    }

    Write-Host ' -> [4/5] 複製圖形資產與說明文件...' -ForegroundColor Cyan;
    Copy-Item (Join-Path $projectRoot 'assets\*') (Join-Path $distDir 'assets') -Recurse -Force;
    Copy-Item (Join-Path $projectRoot 'design\*') (Join-Path $distDir 'design') -Recurse -Force;
    Copy-Item (Join-Path $projectRoot 'README_Windows.md') (Join-Path $distDir 'README_Windows.md') -Force;
    Copy-Item (Join-Path $projectRoot 'README_Windows.md') (Join-Path $distDir '使用說明.txt') -Force;

    $batContent = '@echo off' + [Environment]::NewLine + 'chcp 65001 >nul' + [Environment]::NewLine + 'title EFD 眼睛特徵提取與即時疲勞監控研究系統' + [Environment]::NewLine + 'cd /d ""%~dp0""' + [Environment]::NewLine + 'start """" ""%~dp0EFD.exe""' + [Environment]::NewLine + 'exit' + [Environment]::NewLine;
    [System.IO.File]::WriteAllText((Join-Path $distDir '啟動 EFD.bat'), $batContent, [System.Text.Encoding]::GetEncoding(65001));

    $scContent = '@echo off' + [Environment]::NewLine + 'chcp 65001 >nul' + [Environment]::NewLine + 'cd /d ""%~dp0""' + [Environment]::NewLine + 'powershell -NoProfile -ExecutionPolicy Bypass -Command ""$ws = New-Object -ComObject WScript.Shell; $s = $ws.CreateShortcut([System.IO.Path]::Combine([Environment]::GetFolderPath(''Desktop''), ''EFD 眼睛疲勞監測.lnk'')); $s.TargetPath = ''%~dp0EFD.exe''; $s.WorkingDirectory = ''%~dp0''; $s.Description = ''EFD 眼睛特徵提取與即時疲勞監控研究系統''; if (Test-Path ''%~dp0assets\app.ico'') { $s.IconLocation = ''%~dp0assets\app.ico''; } else { $s.IconLocation = ''%~dp0EFD.exe,0''; }; $s.Save();""' + [Environment]::NewLine + 'echo 桌面捷徑已建立完成！' + [Environment]::NewLine + 'timeout /t 3 >nul' + [Environment]::NewLine + 'exit' + [Environment]::NewLine;
    [System.IO.File]::WriteAllText((Join-Path $distDir '建立桌面捷徑.bat'), $scContent, [System.Text.Encoding]::GetEncoding(65001));

    Write-Host ' -> [5/5] 壓縮為 EFD-v1.0.0-Windows.zip...' -ForegroundColor Cyan;
    if (Test-Path $zipRoot) { Remove-Item -Force $zipRoot }
    Compress-Archive -Path (Join-Path $distDir '*') -DestinationPath $zipRoot -Force;
    Copy-Item $zipRoot $zipBuild -Force;

    $sizeMb = [Math]::Round((Get-Item $zipRoot).Length / 1MB, 2);
    Write-Host ""=================================================================="" -ForegroundColor Green;
    Write-Host ""  [SUCCESS] 打包完成！"" -ForegroundColor Green;
    Write-Host ""  檔案路徑: $zipRoot ($sizeMb MB)"" -ForegroundColor Green;
    Write-Host ""=================================================================="" -ForegroundColor Green;
}"

