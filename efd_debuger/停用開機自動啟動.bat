@echo off
setlocal
chcp 65001 >nul
cd /d "%~dp0"

echo [1/2] Removing Windows Startup Shortcut...
powershell -NoProfile -ExecutionPolicy Bypass -Command "$startup = [Environment]::GetFolderPath('Startup'); $lnk = [System.IO.Path]::Combine($startup, 'EFD_FatigueMonitor.lnk'); if (Test-Path $lnk) { Remove-Item -Force $lnk }"

echo [2/2] Removing Registry Backup...
reg delete "HKCU\Software\Microsoft\Windows\CurrentVersion\Run" /v "EFD_FatigueMonitor" /f >nul 2>&1

echo Done!
exit /b 0
