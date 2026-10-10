@echo off
setlocal
chcp 65001 >nul
cd /d "%~dp0"

set "TARGET_EXE=%~dp0EFD.exe"
set "WORK_DIR=%~dp0"

echo [1/2] Setting up Windows Startup Shortcut...
powershell -NoProfile -ExecutionPolicy Bypass -Command "$startup = [Environment]::GetFolderPath('Startup'); $ws = New-Object -ComObject WScript.Shell; $s = $ws.CreateShortcut([System.IO.Path]::Combine($startup, 'EFD_FatigueMonitor.lnk')); $s.TargetPath = '%TARGET_EXE%'; $s.Arguments = '--background'; $s.WorkingDirectory = '%WORK_DIR%'; $s.Description = 'EFD Fatigue Monitor AutoStart'; $s.Save();"

echo [2/2] Setting up Registry Backup...
reg add "HKCU\Software\Microsoft\Windows\CurrentVersion\Run" /v "EFD_FatigueMonitor" /t REG_SZ /d "\"%TARGET_EXE%\" --background" /f >nul 2>&1

echo Done!
timeout /t 2 >nul
exit /b 0
