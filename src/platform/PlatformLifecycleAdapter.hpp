#pragma once

#include "efd/types.hpp"
#include <string>
#include <functional>
#include <chrono>
#include <atomic>
#include <mutex>

namespace efd {

enum class PlatformType : uint8_t {
    Windows,
    macOS,
    Android,
    iOS,
    Generic
};

enum class AppLifecycleState : uint8_t {
    Active,      // 前景活躍運行中
    Background,  // 切換至背景 (Android 後台服務 / iOS 背景狀態)
    Suspended,   // 系統休眠或鎖屏掛起
    Resumed,     // 從休眠或背景中喚醒
    Terminating  // 應用程式關閉前夕
};

struct LifecycleEvent {
    AppLifecycleState previousState;
    AppLifecycleState currentState;
    int64_t elapsedBackgroundSeconds = 0;
    std::string details;
};

class PlatformLifecycleAdapter {
public:
    using LifecycleCallback = std::function<void(const LifecycleEvent& event)>;

    explicit PlatformLifecycleAdapter(PlatformType platform = PlatformType::Windows);
    ~PlatformLifecycleAdapter();

    // 取得當前運行的平台類型
    PlatformType getPlatformType() const;
    std::string getPlatformName() const;

    // 取得當前應用程式生命週期狀態
    AppLifecycleState getCurrentState() const;

    // 註冊生命週期變更回呼
    void setLifecycleCallback(LifecycleCallback callback);

    // 觸發生命週期轉換 (由各平台原生系統事件呼叫，如 Android Activity / iOS AppDelegate / Windows WndProc)
    void transitionTo(AppLifecycleState newState, const std::string& details = "");

    // 模擬或通知系統睡眠 / 進入背景
    void notifySystemSleep();

    // 模擬或通知系統喚醒 / 回到前景 (Hot Resume)
    void notifySystemWake();

    // 平台特定功能檢查
    bool supportsForegroundService() const;
    bool supportsSystemTray() const;
    bool supportsHotResume() const;

    // 格式化當前平台狀態摘要
    std::string getStatusSummary() const;

private:
    PlatformType m_platform;
    std::atomic<AppLifecycleState> m_currentState{AppLifecycleState::Active};
    std::chrono::system_clock::time_point m_backgroundStartTime;
    int64_t m_lastBackgroundDurationSec = 0;

    mutable std::mutex m_callbackMutex;
    LifecycleCallback m_callback;
};

} // namespace efd

