#include <iostream>
#include "NativeWelcomeWindow.hpp"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

int main() {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif

    std::cout << "====================================================\n";
    std::cout << "  EFD 視覺校準與疲勞監控圖形介面系統\n";
    std::cout << "  - 階段一: 歡迎介面 (Logo + 感謝協助測試EFD)\n";
    std::cout << "  - 階段二: 眼動數據提取說明 (中央黃點)\n";
    std::cout << "  - 階段三: 眼動數據動態提取 (多點平滑移動採樣)\n";
    std::cout << "  - 階段四: 即時疲勞監控儀表板\n";
    std::cout << "====================================================\n";

    efd::NativeWelcomeWindow window(960, 640);
    return window.run();
}

