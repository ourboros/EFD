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
#elif defined(__APPLE__)
#include "macos/MacWelcomeWindow.h"
#endif

int main(int argc, char* argv[]) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);

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
        std::cout << "[EFD 實例檢查] 偵測到 EFD 系統已在背景執行中，正在喚醒系統操作介面...\n";
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

    std::cout << "====================================================\n";
    std::cout << "  EFD 七階段視覺校準、疲勞監控與科研後測系統\n";
    std::cout << "  EFD 視覺校準、疲勞監控與科研後測系統\n";
    if (isMobileMode) {
        std::cout << "  [模式] 智慧型手機模擬器 (390 x 844 直立視窗)\n";
    } else {
        std::cout << "  [模式] 桌面寬螢幕模式 (960 x 640 橫向視窗)\n";
    }
    std::cout << "  - 階段一: 歡迎介面 (Logo + 感謝協助測試EFD)\n";
    std::cout << "  - 階段二: 特徵提取說明 (DEMO 演示預覽 + 相機狀態)\n";
    std::cout << "  - 階段三: 3 秒倒數計時等待 (3 -> 2 -> 1)\n";
    std::cout << "  - 階段四: 多點動態眼動特徵提取 (動態黃點採樣)\n";
    std::cout << "  - 階段五: 即時眼睛疲勞監控中心 (動態數據跳動)\n";
    std::cout << "  - 階段六: 施測結束門禁 (資產 5: 後測問卷引導)\n";
    std::cout << "  - 階段七: 問卷提交完成 (資產 6: 解鎖授權與解除安裝)\n";
    std::cout << "  - 階段五: 基準校準完成結果提示\n";
    std::cout << "  - 階段六: 即時眼睛疲勞監控中心 (動態數據跳動)\n";
    std::cout << "  - 階段七: 系統設定介面\n";
    std::cout << "  - 階段八: 施測結束後測介面 (後測問卷引導)\n";
    std::cout << "  - 階段九: 問卷提交完成 (科研憑證與解除鎖定)\n";
    std::cout << "====================================================\n";

#ifdef _WIN32
    efd::NativeWelcomeWindow window(winWidth, winHeight, isBackgroundMode);
    int exitCode = window.run();
    if (hSingleMutex) {
        ReleaseMutex(hSingleMutex);
        CloseHandle(hSingleMutex);
    }
    return exitCode;
#elif defined(__APPLE__)
    efd::MacWelcomeWindow window(winWidth, winHeight);
    return window.run();
#else
    std::cerr << "Unsupported platform for native GUI.\n";
    return 1;
#endif
}

