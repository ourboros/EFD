@echo off
chcp 65001 >nul
cd /d "%~dp0"

echo 正在建立 EFD 桌面捷徑...

powershell -NoProfile -ExecutionPolicy Bypass -Command "$ws = New-Object -ComObject WScript.Shell; $s = $ws.CreateShortcut([System.IO.Path]::Combine([Environment]::GetFolderPath('Desktop'), 'EFD 眼睛疲勞監測.lnk')); $s.TargetPath = '%~dp0EFD.exe'; $s.WorkingDirectory = '%~dp0'; $s.Description = 'EFD 眼睛特徵提取與即時疲勞監控研究系統'; if (Test-Path '%~dp0assets\app.ico') { $s.IconLocation = '%~dp0assets\app.ico'; } else { $s.IconLocation = '%~dp0EFD.exe,0'; }; $s.Save();"

if %ERRORLEVEL% equ 0 (
    echo [成功] 桌面捷徑已建立完成！
) else (
    echo [提示] 捷徑建立失敗，您可以直接雙擊 EFD.exe 執行。
)
timeout /t 3 >nul
exit
