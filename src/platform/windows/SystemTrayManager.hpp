#pragma once

#include "efd/types.hpp"
#include <string>
#include <functional>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#endif

namespace efd {

enum class TrayMenuAction {
    ShowMainWindow,
    ToggleFloatingIndicator,
    Recalibrate,
    EndStudyGate,
    ExitApp
};

class SystemTrayManager {
public:
    using MenuActionCallback = std::function<void(TrayMenuAction action)>;

#ifdef _WIN32
    explicit SystemTrayManager(HWND hostHwnd = nullptr);
#else
    explicit SystemTrayManager();
#endif
    ~SystemTrayManager();

    // 初始化系統托盤圖示
#ifdef _WIN32
    bool initialize(HWND hostHwnd, const std::wstring& tipText = L"EFD 眼睛疲勞即時監測系統");
#endif

    // 更新托盤圖示狀態與提示文字 (依疲勞等級自動調整綠/黃/紅圖示狀態)
    void updateStatus(FatigueLevel level, float fatigueScore, const std::wstring& statusMsg);

    // 發送 Windows 原生氣泡/Toast 警報通知
    void showBalloonNotification(const std::wstring& title, const std::wstring& message, FatigueLevel level);

    // 處理視窗自訂托盤訊息 (WM_APP + 1)
#ifdef _WIN32
    void handleTrayMessage(LPARAM lParam);
#endif

    // 顯示右鍵快顯功能表
    void showContextMenu();

    // 註冊功能表回呼
    void setActionCallback(MenuActionCallback callback);

    // 移除托盤圖示
    void removeIcon();

    bool isInitialized() const { return m_initialized; }

#ifdef _WIN32
    static constexpr UINT WM_TRAY_NOTIFY = WM_APP + 101;
#endif

private:
#ifdef _WIN32
    HWND m_hwnd = nullptr;
    NOTIFYICONDATAW m_nid{};
#endif
    bool m_initialized = false;
#ifdef _WIN32
    HICON m_currentIcon = nullptr;
    MenuActionCallback m_actionCallback;

    HICON createColoredDotIcon(COLORREF color);
#endif
};

} // namespace efd

