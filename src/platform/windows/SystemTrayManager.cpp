#include "SystemTrayManager.hpp"
#include <iostream>

#ifdef _WIN32

namespace efd {

namespace {
constexpr UINT ID_TRAY_SHOW_MAIN       = 2001;
constexpr UINT ID_TRAY_TOGGLE_FLOATING = 2002;
constexpr UINT ID_TRAY_RECALIBRATE     = 2003;
constexpr UINT ID_TRAY_END_STUDY       = 2004;
constexpr UINT ID_TRAY_EXIT            = 2005;
} // anonymous namespace

SystemTrayManager::SystemTrayManager(HWND hostHwnd)
    : m_hwnd(hostHwnd) {
}

SystemTrayManager::~SystemTrayManager() {
    removeIcon();
}

HICON SystemTrayManager::createColoredDotIcon(COLORREF color) {
    int iconW = GetSystemMetrics(SM_CXSMICON);
    int iconH = GetSystemMetrics(SM_CYSMICON);
    if (iconW <= 0) iconW = 16;
    if (iconH <= 0) iconH = 16;

    HDC screenDC = GetDC(NULL);
    HDC memDC = CreateCompatibleDC(screenDC);
    HBITMAP hBitmap = CreateCompatibleBitmap(screenDC, iconW, iconH);
    HBITMAP hMask = CreateBitmap(iconW, iconH, 1, 1, NULL);

    HGDIOBJ oldBmp = SelectObject(memDC, hBitmap);
    RECT rc = { 0, 0, iconW, iconH };
    
    // 背景深色
    HBRUSH bgBrush = CreateSolidBrush(RGB(37, 41, 28));
    FillRect(memDC, &rc, bgBrush);
    DeleteObject(bgBrush);

    // 圓形彩色實心指標
    HBRUSH dotBrush = CreateSolidBrush(color);
    HPEN dotPen = CreatePen(PS_SOLID, 1, RGB(255, 255, 255));
    HGDIOBJ oldBrush = SelectObject(memDC, dotBrush);
    HGDIOBJ oldPen = SelectObject(memDC, dotPen);

    Ellipse(memDC, 2, 2, iconW - 2, iconH - 2);

    SelectObject(memDC, oldBrush);
    SelectObject(memDC, oldPen);
    SelectObject(memDC, oldBmp);
    DeleteObject(dotBrush);
    DeleteObject(dotPen);
    DeleteDC(memDC);
    ReleaseDC(NULL, screenDC);

    ICONINFO ii{};
    ii.fIcon = TRUE;
    ii.hbmMask = hMask;
    ii.hbmColor = hBitmap;
    HICON hIcon = CreateIconIndirect(&ii);

    DeleteObject(hBitmap);
    DeleteObject(hMask);
    return hIcon;
}

bool SystemTrayManager::initialize(HWND hostHwnd, const std::wstring& tipText) {
    if (!hostHwnd) return false;
    m_hwnd = hostHwnd;

    m_currentIcon = createColoredDotIcon(RGB(30, 177, 138)); // 預設薄荷綠

    ZeroMemory(&m_nid, sizeof(NOTIFYICONDATAW));
    m_nid.cbSize = sizeof(NOTIFYICONDATAW);
    m_nid.hWnd = m_hwnd;
    m_nid.uID = 1001;
    m_nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_INFO;
    m_nid.uCallbackMessage = WM_TRAY_NOTIFY;
    m_nid.hIcon = m_currentIcon;
    wcsncpy_s(m_nid.szTip, tipText.c_str(), 127);

    if (Shell_NotifyIconW(NIM_ADD, &m_nid)) {
        m_nid.uVersion = NOTIFYICON_VERSION_4;
        Shell_NotifyIconW(NIM_SETVERSION, &m_nid);
        m_initialized = true;
        return true;
    }
    return false;
}

void SystemTrayManager::updateStatus(FatigueLevel level, float fatigueScore, const std::wstring& statusMsg) {
    if (!m_initialized) return;

    COLORREF iconColor = RGB(30, 177, 138); // 正常綠
    if (level == FatigueLevel::Attention) {
        iconColor = RGB(247, 227, 175);     // 注意黃
    } else if (level == FatigueLevel::SevereWarning) {
        iconColor = RGB(235, 87, 87);       // 警告紅
    }

    if (m_currentIcon) {
        DestroyIcon(m_currentIcon);
    }
    m_currentIcon = createColoredDotIcon(iconColor);
    m_nid.hIcon = m_currentIcon;
    m_nid.uFlags = NIF_ICON | NIF_TIP;

    wchar_t tipBuffer[128];
    swprintf_s(tipBuffer, 128, L"EFD 監控中 (疲勞分數: %.1f) - %s", static_cast<double>(fatigueScore), statusMsg.c_str());
    wcsncpy_s(m_nid.szTip, tipBuffer, 127);

    Shell_NotifyIconW(NIM_MODIFY, &m_nid);
}

void SystemTrayManager::showBalloonNotification(const std::wstring& title, const std::wstring& message, FatigueLevel level) {
    if (!m_initialized) return;

    m_nid.uFlags |= NIF_INFO;
    wcsncpy_s(m_nid.szInfoTitle, title.c_str(), 63);
    wcsncpy_s(m_nid.szInfo, message.c_str(), 255);

    if (level == FatigueLevel::SevereWarning) {
        m_nid.dwInfoFlags = NIIF_ERROR | NIIF_LARGE_ICON;
        MessageBeep(MB_ICONWARNING);
    } else if (level == FatigueLevel::Attention) {
        m_nid.dwInfoFlags = NIIF_WARNING | NIIF_LARGE_ICON;
        MessageBeep(MB_OK);
    } else {
        m_nid.dwInfoFlags = NIIF_INFO;
    }

    m_nid.uTimeout = 4000; // 4 秒
    m_nid.uTimeout = 5000; // 5 秒
    Shell_NotifyIconW(NIM_MODIFY, &m_nid);
}

void SystemTrayManager::handleTrayMessage(LPARAM lParam) {
    UINT uMsg = LOWORD(lParam);
    if (uMsg == WM_LBUTTONDBLCLK || uMsg == WM_LBUTTONUP) {
        if (m_actionCallback) {
            m_actionCallback(TrayMenuAction::ShowMainWindow);
        }
    } else if (uMsg == WM_RBUTTONUP || uMsg == WM_CONTEXTMENU) {
        showContextMenu();
    }
}

void SystemTrayManager::showContextMenu() {
    if (!m_hwnd) return;

    HMENU hMenu = CreatePopupMenu();
    if (!hMenu) return;

    AppendMenuW(hMenu, MF_STRING, ID_TRAY_SHOW_MAIN, L"🖥️ 顯示 EFD 監控中心");
    AppendMenuW(hMenu, MF_STRING, ID_TRAY_TOGGLE_FLOATING, L"🔘 切換桌面置頂懸浮指標");
    AppendMenuW(hMenu, MF_STRING, ID_TRAY_RECALIBRATE, L"🔄 重新校準眼睛基準");
    AppendMenuW(hMenu, MF_STRING, ID_TRAY_END_STUDY, L"📋 填寫後測問卷 (14天門禁)");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(hMenu, MF_STRING, ID_TRAY_EXIT, L"❌ 安全退出系統");

    POINT pt;
    GetCursorPos(&pt);
    SetForegroundWindow(m_hwnd);

    UINT cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, m_hwnd, NULL);
    DestroyMenu(hMenu);

    if (!m_actionCallback) return;

    switch (cmd) {
    case ID_TRAY_SHOW_MAIN:
        m_actionCallback(TrayMenuAction::ShowMainWindow);
        break;
    case ID_TRAY_TOGGLE_FLOATING:
        m_actionCallback(TrayMenuAction::ToggleFloatingIndicator);
        break;
    case ID_TRAY_RECALIBRATE:
        m_actionCallback(TrayMenuAction::Recalibrate);
        break;
    case ID_TRAY_END_STUDY:
        m_actionCallback(TrayMenuAction::EndStudyGate);
        break;
    case ID_TRAY_EXIT:
        m_actionCallback(TrayMenuAction::ExitApp);
        break;
    }
}

void SystemTrayManager::setActionCallback(MenuActionCallback callback) {
    m_actionCallback = std::move(callback);
}

void SystemTrayManager::removeIcon() {
    if (m_initialized) {
        Shell_NotifyIconW(NIM_DELETE, &m_nid);
        m_initialized = false;
    }
    if (m_currentIcon) {
        DestroyIcon(m_currentIcon);
        m_currentIcon = nullptr;
    }
}

} // namespace efd

#endif

