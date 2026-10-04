@echo off
chcp 65001 >nul
cd /d "%~dp0"
echo ==================================================================
echo   正在啟用 EFD 眼睛疲勞監測系統 - 開機自動啟動...
echo ==================================================================
reg add "HKCU\Software\Microsoft\Windows\CurrentVersion\Run" /v "EFD_FatigueMonitor" /t REG_SZ /d "\"%~dp0EFD.exe\"" /f >nul
if %ERRORLEVEL% equ 0 (
    echo [成功] 已成功加入 Windows 開機自動啟動！系統將於開機登入時自動在背景守護。
) else (
    echo [失敗] 設定失敗，請嘗試手動在軟體「設定」介面中開啟。
)
echo 視窗將在 3 秒後自動關閉...
timeout /t 3 >nul
exit
