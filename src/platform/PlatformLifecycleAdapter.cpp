#include "PlatformLifecycleAdapter.hpp"
#include <sstream>

namespace efd {

PlatformLifecycleAdapter::PlatformLifecycleAdapter(PlatformType platform)
    : m_platform(platform),
      m_currentState(AppLifecycleState::Active),
      m_backgroundStartTime(std::chrono::system_clock::now()) {
}

PlatformLifecycleAdapter::~PlatformLifecycleAdapter() = default;

PlatformType PlatformLifecycleAdapter::getPlatformType() const {
    return m_platform;
}

std::string PlatformLifecycleAdapter::getPlatformName() const {
    switch (m_platform) {
        case PlatformType::Windows: return "Windows (x64/ARM64)";
        case PlatformType::macOS:   return "macOS (Apple Silicon/Intel)";
        case PlatformType::Android: return "Android (NDK/Java)";
        case PlatformType::iOS:     return "iOS (Obj-C++/Metal)";
        default:                    return "Generic C++ Platform";
    }
}

AppLifecycleState PlatformLifecycleAdapter::getCurrentState() const {
    return m_currentState.load();
}

void PlatformLifecycleAdapter::setLifecycleCallback(LifecycleCallback callback) {
    std::lock_guard<std::mutex> lock(m_callbackMutex);
    m_callback = std::move(callback);
}

void PlatformLifecycleAdapter::transitionTo(AppLifecycleState newState, const std::string& details) {
    AppLifecycleState prevState = m_currentState.exchange(newState);
    if (prevState == newState) {
        return; // 狀態未改變
    }

    int64_t backgroundElapsed = 0;
    auto now = std::chrono::system_clock::now();

    // 處理進入與離開背景的時間統計
    if (newState == AppLifecycleState::Background || newState == AppLifecycleState::Suspended) {
        m_backgroundStartTime = now;
    } else if (prevState == AppLifecycleState::Background || prevState == AppLifecycleState::Suspended) {
        backgroundElapsed = std::chrono::duration_cast<std::chrono::seconds>(now - m_backgroundStartTime).count();
        m_lastBackgroundDurationSec = backgroundElapsed;
    }

    LifecycleEvent event;
    event.previousState = prevState;
    event.currentState = newState;
    event.elapsedBackgroundSeconds = backgroundElapsed;
    event.details = details.empty() ? ("Transition to " + std::to_string(static_cast<int>(newState))) : details;

    std::lock_guard<std::mutex> lock(m_callbackMutex);
    if (m_callback) {
        m_callback(event);
    }
}

void PlatformLifecycleAdapter::notifySystemSleep() {
    transitionTo(AppLifecycleState::Suspended, "System sleep / screen locked event");
}

void PlatformLifecycleAdapter::notifySystemWake() {
    transitionTo(AppLifecycleState::Resumed, "System woke up / screen unlocked (Hot Resume)");
}

bool PlatformLifecycleAdapter::supportsForegroundService() const {
    return (m_platform == PlatformType::Android);
}

bool PlatformLifecycleAdapter::supportsSystemTray() const {
    return (m_platform == PlatformType::Windows || m_platform == PlatformType::macOS);
}

bool PlatformLifecycleAdapter::supportsHotResume() const {
    return (m_platform == PlatformType::iOS || m_platform == PlatformType::Android);
}

std::string PlatformLifecycleAdapter::getStatusSummary() const {
    std::ostringstream oss;
    oss << "[" << getPlatformName() << "] State: ";
    switch (m_currentState.load()) {
        case AppLifecycleState::Active:      oss << "Active (Foreground 60FPS)"; break;
        case AppLifecycleState::Background:  oss << "Background Service"; break;
        case AppLifecycleState::Suspended:   oss << "Suspended (Sleep)"; break;
        case AppLifecycleState::Resumed:     oss << "Resumed (Hot-Resume)"; break;
        case AppLifecycleState::Terminating: oss << "Terminating"; break;
    }
    if (m_lastBackgroundDurationSec > 0) {
        oss << " | Last Background: " << m_lastBackgroundDurationSec << "s";
    }
    return oss.str();
}

} // namespace efd

