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
    std::cout << "  EFD 視覺校準、疲勞監控與科研後測圖形介面系統\n";
    std::cout << "  - 階段 1: 系統歡迎介面 (Logo + 感謝協助測試EFD)\n";
    std::cout << "  - 階段 2: 提取說明與演示預覽 (動態DEMO + 相機狀態)\n";
    std::cout << "  - 階段 3: 3 秒倒數計時等待 (3 -> 2 -> 1)\n";
    std::cout << "  - 階段 4: 多點動態眼動特徵提取 (5秒多點採樣)\n";
    std::cout << "  - 階段 5: EFD 即時疲勞監控中心 (動態跳動數值)\n";
    std::cout << "  - 階段 6: 施測結束門禁介面 (資產 5.png)\n";
    std::cout << "  - 階段 7: 後測問卷填寫完成介面 (資產 6.png)\n";
    std::cout << "====================================================\n";

    efd::NativeWelcomeWindow window(960, 640);
    return window.run();
}

