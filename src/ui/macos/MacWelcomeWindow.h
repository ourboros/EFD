#pragma once

#include "engine/AsyncPipelineEngine.hpp"
#include <memory>
#include <string>

namespace efd {

class MacWelcomeWindow {
public:
    explicit MacWelcomeWindow(int width = 960, int height = 640);
    ~MacWelcomeWindow();

    // 啟動 macOS Cocoa 原生應用程式事件循環
    int run();

    // 取得管線引擎參照
    AsyncPipelineEngine& getEngine();

    // 視窗與流程控制
    void showWindow();
    void hideToBackground();
    void toggleFloatingHUD();

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace efd
