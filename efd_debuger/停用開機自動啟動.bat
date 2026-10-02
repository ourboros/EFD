@echo off
chcp 65001 >nul
cd /d "%~dp0"
echo ==================================================================
echo   正在停用 EFD 眼睛疲勞監測系統 - 開機自動啟動...
echo ==================================================================
reg delete "HKCU\Software\Microsoft\Windows\CurrentVersion\Run" /v "EFD_FatigueMonitor" /f >nul 2>&1
echo [完成] 已移除開機自動啟動設定。
echo 視窗將在 3 秒後自動關閉...
timeout /t 3 >nul
exit
