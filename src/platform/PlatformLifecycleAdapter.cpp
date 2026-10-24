#include "PlatformLifecycleAdapter.hpp"
#include <sstream>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#elif defined(__APPLE__)
#include <TargetConditionals.h>
#if TARGET_OS_OSX
#define EFD_MACOS_DESKTOP 1
#include <cerrno>
#include <climits>
#include <cstdlib>
#include <fstream>
#include <mach-o/dyld.h>
#include <sys/stat.h>
#include <unistd.h>
#endif
#endif

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

namespace {
#ifdef _WIN32
const wchar_t* kRunKeyPath = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
const wchar_t* kRunValueName = L"EFD_FatigueMonitor";

// 舊版同時建立「啟動」資料夾捷徑與登錄檔，開機會啟動兩次；現在只用登錄檔，捷徑一律清除
std::wstring legacyStartupShortcutPath() {
    wchar_t appDataPath[MAX_PATH];
    if (GetEnvironmentVariableW(L"APPDATA", appDataPath, MAX_PATH) > 0) {
        return std::wstring(appDataPath) + L"\\Microsoft\\Windows\\Start Menu\\Programs\\Startup\\EFD_FatigueMonitor.lnk";
    }
    return L"";
}
#elif defined(EFD_MACOS_DESKTOP)
const char* kLaunchAgentLabel = "com.efd.EyeFatigueDetection";

std::string launchAgentPlistPath() {
    const char* home = std::getenv("HOME");
    if (!home || !*home) return "";
    return std::string(home) + "/Library/LaunchAgents/" + kLaunchAgentLabel + ".plist";
}

std::string xmlEscape(const std::string& s) {
    std::string out;
    for (char c : s) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            default:  out += c; break;
        }
    }
    return out;
}
#endif
} // anonymous namespace

bool PlatformLifecycleAdapter::isAutoStartEnabled() const {
#ifdef _WIN32
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKeyPath, 0, KEY_READ, &hKey) != ERROR_SUCCESS) {
        return false;
    }
    DWORD type = 0;
    LONG result = RegQueryValueExW(hKey, kRunValueName, nullptr, &type, nullptr, nullptr);
    RegCloseKey(hKey);
    return (result == ERROR_SUCCESS);
#elif defined(EFD_MACOS_DESKTOP)
    std::string path = launchAgentPlistPath();
    struct stat st{};
    return !path.empty() && ::stat(path.c_str(), &st) == 0;
#else
    // iOS / Android 不支援由 App 設定開機自動啟動
    return false;
#endif
}

bool PlatformLifecycleAdapter::setAutoStartEnabled(bool enable) {
#ifdef _WIN32
    std::wstring lnkPath = legacyStartupShortcutPath();
    if (!lnkPath.empty()) {
        DeleteFileW(lnkPath.c_str());
    }

    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKeyPath, 0, KEY_SET_VALUE, &hKey) != ERROR_SUCCESS) {
        return false;
    }
    LONG result = ERROR_SUCCESS;
    if (enable) {
        wchar_t exePath[MAX_PATH];
        DWORD len = GetModuleFileNameW(nullptr, exePath, MAX_PATH);
        if (len == 0 || len >= MAX_PATH) {
            RegCloseKey(hKey);
            return false;
        }
        // main_gui 啟動時會把工作目錄切回執行檔所在目錄，因此不需另外指定 WorkingDirectory
        std::wstring quoted = L"\"" + std::wstring(exePath) + L"\" --background";
        result = RegSetValueExW(hKey, kRunValueName, 0, REG_SZ,
                                reinterpret_cast<const BYTE*>(quoted.c_str()),
                                static_cast<DWORD>((quoted.length() + 1) * sizeof(wchar_t)));
    } else {
        result = RegDeleteValueW(hKey, kRunValueName);
        if (result == ERROR_FILE_NOT_FOUND) result = ERROR_SUCCESS;
    }
    RegCloseKey(hKey);
    return result == ERROR_SUCCESS;
#elif defined(EFD_MACOS_DESKTOP)
    std::string plistPath = launchAgentPlistPath();
    if (plistPath.empty()) return false;

    if (!enable) {
        return ::unlink(plistPath.c_str()) == 0 || errno == ENOENT;
    }

    char exeBuf[PATH_MAX];
    uint32_t size = sizeof(exeBuf);
    if (_NSGetExecutablePath(exeBuf, &size) != 0) return false;
    char resolved[PATH_MAX];
    std::string exePath = ::realpath(exeBuf, resolved) ? resolved : exeBuf;

    // 確保 ~/Library/LaunchAgents 存在
    std::string dir = plistPath.substr(0, plistPath.find_last_of('/'));
    ::mkdir(dir.c_str(), 0755);

    std::ofstream out(plistPath, std::ios::trunc);
    if (!out.is_open()) return false;
    out << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        << "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
        << "<plist version=\"1.0\">\n<dict>\n"
        << "    <key>Label</key>\n    <string>" << kLaunchAgentLabel << "</string>\n"
        << "    <key>ProgramArguments</key>\n    <array>\n"
        << "        <string>" << xmlEscape(exePath) << "</string>\n"
        << "        <string>--background</string>\n"
        << "    </array>\n"
        << "    <key>RunAtLoad</key>\n    <true/>\n"
        << "    <key>ProcessType</key>\n    <string>Interactive</string>\n"
        << "</dict>\n</plist>\n";
    return out.good();
#else
    (void)enable;
    return false;
#endif
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

