#pragma once

#include "efd/types.hpp"
#include <string>
#include <functional>
#include <memory>

namespace efd {

enum class MacMenuAction {
    ShowMainWindow,
    ToggleFloatingIndicator,
    Recalibrate,
    EndStudyGate,
    ExitApp
};

class MacStatusItemManager {
public:
    using ActionCallback = std::function<void(MacMenuAction action)>;

    MacStatusItemManager();
    ~MacStatusItemManager();

    // 初始化 macOS 頂部選單列圖示 (Status Item)
    bool initialize(const std::string& tooltip = "EFD 眼睛疲勞即時監測系統");

    // 更新狀態列圖示與提示 (依疲勞程度切換綠/黃/紅圖示狀態)
    void updateStatus(FatigueLevel level, float fatigueScore, const std::string& statusMsg);

    // 發送 macOS 原生系統通知 (僅在嚴重警告時調用)
    void showNotification(const std::string& title, const std::string& message, FatigueLevel level);

    // 註冊選單點擊回呼
    void setActionCallback(ActionCallback callback);

    // 移除選單列圖示
    void removeStatusItem();

    bool isInitialized() const;

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace efd

