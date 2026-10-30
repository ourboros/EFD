#include <iostream>
#include "NativeWelcomeWindow.hpp"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include "NativeWelcomeWindow.hpp"
#endif

#ifdef _WIN32
// 強制指定 Windows Subsystem 與 main 進入點 (避免彈出空命令列視窗)
#pragma comment(linker, "/SUBSYSTEM:windows /ENTRY:mainCRTStartup")
#endif

int main(int argc, char* argv[]) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);

    // 開啟系統日誌紀錄
    freopen("efd_system.log", "a", stdout);
    freopen("efd_system.log", "a", stderr);

    // 1. 將工作目錄固定鎖定為執行檔自身目錄，徹底解決 Windows 開機自啟動時工作目錄漂移至 C:\Windows\System32 導致找不到資料與重複視為首次啟動的問題
    WCHAR exePath[MAX_PATH] = { 0 };
    if (GetModuleFileNameW(NULL, exePath, MAX_PATH) > 0) {
        std::wstring exeDir = exePath;
        size_t lastSlash = exeDir.find_last_of(L"\\/");
        if (lastSlash != std::wstring::npos) {
            exeDir = exeDir.substr(0, lastSlash);
            SetCurrentDirectoryW(exeDir.c_str());
        }
    }

    // 2. 單一實例互斥鎖檢查 (Single Instance Mutex)：若背景已有程序在執行，僅喚醒介面並退出，杜絕重複開啟多個程序與搶佔攝影機
    HANDLE hSingleMutex = CreateMutexW(NULL, TRUE, L"Local\\EFD_FatigueMonitor_SingleInstance_Mutex");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND existingHwnd = FindWindowW(L"EFD_FullNativeWindow", NULL);
        if (existingHwnd) {
            UINT msgShowUI = RegisterWindowMessageW(L"WM_EFD_SHOW_UI_MSG");
            PostMessageW(existingHwnd, msgShowUI, 0, 0);
            ShowWindow(existingHwnd, SW_RESTORE);
            SetForegroundWindow(existingHwnd);
        }
        if (hSingleMutex) {
            CloseHandle(hSingleMutex);
        }
        std::cout << "[EFD 實例檢查] 偵測到 EFD 系統已在背景執行中，已喚醒現有視窗並退出。\n" << std::flush;
        return 0; // 新行程直接安靜退出
    }
#endif

    int winWidth = 960;
    int winHeight = 640;
    bool isMobileMode = false;
    bool isBackgroundMode = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--mobile" || arg == "-m" || arg == "--phone") {
            winWidth = 390;
            winHeight = 844; // iPhone 14 / 標準直立手機視窗解析度
            isMobileMode = true;
        } else if (arg == "--tablet" || arg == "-t") {
            winWidth = 768;
            winHeight = 1024;
        } else if (arg == "--background" || arg == "-b" || arg == "--autostart" || arg == "--silent") {
            isBackgroundMode = true;
        }
    }

    std::string modeStr = isMobileMode ? "智慧型手機模擬器 (390 x 844 直立視窗)" : "桌面寬螢幕模式 (960 x 640 橫向視窗)";
    std::string banner = 
        "====================================================\n"
        "  EFD 視覺校準、疲勞監控與科研後測系統\n"
        "  [模式] " + modeStr + "\n"
        "  - 階段一: 歡迎介面 (Logo + 感謝協助測試EFD)\n"
        "  - 階段二: 特徵提取說明 (DEMO 演示預覽 + 相機狀態)\n"
        "  - 階段三: 3 秒倒數計時等待 (3 -> 2 -> 1)\n"
        "  - 階段四: 多點動態眼動特徵提取 (動態黃點採樣)\n"
        "  - 階段五: 基準校準完成結果提示\n"
        "  - 階段六: 即時眼睛疲勞監控中心 (動態數據跳動)\n"
        "  - 階段七: 系統設定介面\n"
        "  - 階段八: 施測結束後測介面 (後測問卷引導)\n"
        "  - 階段九: 問卷提交完成 (科研憑證與解除鎖定)\n"
        "====================================================\n";
    std::cout << banner << std::flush;
#ifdef _WIN32
    efd::NativeWelcomeWindow window(winWidth, winHeight, isBackgroundMode);
    int exitCode = window.run();
    if (hSingleMutex) {
        ReleaseMutex(hSingleMutex);
        CloseHandle(hSingleMutex);
    }
    return exitCode;
#else
    std::cerr << "Unsupported platform for native GUI.\n";
    return 1;
#endif
}

