@echo off
chcp 65001 >nul
title EFD 眼睛特徵提取與即時疲勞監控研究系統
cd /d "%~dp0"

echo ==================================================================
echo   EFD 眼睛特徵提取與即時疲勞監控研究系統 (Windows 桌面端)
echo ==================================================================
echo 正在啟動系統，請稍候...

if not exist "EFD.exe" (
    echo [錯誤] 找不到 EFD.exe，請確認檔案未被解壓縮軟體分離。
    pause
    exit /b 1
)

start "" "%~dp0EFD.exe"
exit
