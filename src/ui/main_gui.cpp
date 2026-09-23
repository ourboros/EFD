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
#endif

int main(int argc, char* argv[]) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif

    int winWidth = 960;
    int winHeight = 640;
    bool isMobileMode = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--mobile" || arg == "-m" || arg == "--phone") {
            winWidth = 390;
            winHeight = 844; // iPhone 14 / 標準直立手機視窗解析度
            isMobileMode = true;
        } else if (arg == "--tablet" || arg == "-t") {
            winWidth = 768;
            winHeight = 1024;
        }
    }

    std::cout << "====================================================\n";
    std::cout << "  EFD 七階段視覺校準、疲勞監控與科研後測系統\n";
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
    std::cout << "====================================================\n";

    efd::NativeWelcomeWindow window(winWidth, winHeight);
    return window.run();
}

