@echo off
setlocal
cd /d "%~dp0"
echo ==================================================================
echo   [EFD] Windows 桌面端分發套件自動化打包 (v2.0.0)
echo ==================================================================
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0package_windows.ps1"
if %ERRORLEVEL% neq 0 (
    echo [打包失敗] 錯誤代碼: %ERRORLEVEL%
    pause
    exit /b %ERRORLEVEL%
)
pause
