#include "FatigueFloatingIndicator.hpp"
#include <cmath>
#include <sstream>
#include <iomanip>

#ifdef _WIN32

namespace efd {

FatigueFloatingIndicator::FatigueFloatingIndicator(int width, int height)
    : m_width(width), m_height(height) {
}

FatigueFloatingIndicator::~FatigueFloatingIndicator() {
    if (m_hwnd) {
        DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
    }
}

bool FatigueFloatingIndicator::create(HWND parentHwnd) {
    WNDCLASSEXW wc = { sizeof(WNDCLASSEXW) };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.lpszClassName = L"EFD_FloatingIndicatorHUD";
    wc.hCursor = LoadCursor(NULL, IDC_HAND);
    wc.style = CS_HREDRAW | CS_VREDRAW;

    RegisterClassExW(&wc);

    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int posX = screenW - m_width - 30;
    int posY = 60; // 螢幕右上角

    m_hwnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED,
        wc.lpszClassName,
        L"EFD Floating Indicator",
        WS_POPUP,
        posX, posY, m_width, m_height,
        parentHwnd, NULL, wc.hInstance, this
    );

    if (!m_hwnd) return false;

    // 設定半透明度 (Alpha = 230 / 255)
    SetLayeredWindowAttributes(m_hwnd, 0, 230, LWA_ALPHA);

    // 啟動 30 FPS 動畫計時器
    SetTimer(m_hwnd, 1, 33, NULL);

    return true;
}

void FatigueFloatingIndicator::show() {
    if (!m_hwnd) create();
    if (m_hwnd) {
        ShowWindow(m_hwnd, SW_SHOWNOACTIVATE);
        UpdateWindow(m_hwnd);
        m_visible = true;
    }
}

void FatigueFloatingIndicator::hide() {
    if (m_hwnd) {
        ShowWindow(m_hwnd, SW_HIDE);
        m_visible = false;
    }
}

void FatigueFloatingIndicator::toggle() {
    if (m_visible) hide();
    else show();
}

bool FatigueFloatingIndicator::isVisible() const {
    return m_visible;
}

void FatigueFloatingIndicator::updateMetrics(float ear, float fatigueScore, FatigueLevel level, const std::string& statusMsg) {
    m_ear = ear;
    m_fatigueScore = fatigueScore;
    m_level = level;
    m_statusMsg = statusMsg;

    if (m_hwnd && m_visible) {
        InvalidateRect(m_hwnd, NULL, FALSE);
    }
}

void FatigueFloatingIndicator::setRestoreCallback(RestoreCallback callback) {
    m_restoreCallback = std::move(callback);
}

void FatigueFloatingIndicator::onTimerTick() {
    m_pulseAnim += 0.08f;
    if (m_pulseAnim > 6.2831853f) {
        m_pulseAnim -= 6.2831853f;
    }
    if (m_hwnd && m_visible) {
        InvalidateRect(m_hwnd, NULL, FALSE);
    }
}

void FatigueFloatingIndicator::onPaint(HWND hwnd) {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);

    RECT clientRect;
    GetClientRect(hwnd, &clientRect);
    int width = clientRect.right - clientRect.left;
    int height = clientRect.bottom - clientRect.top;

    if (width <= 0 || height <= 0) {
        EndPaint(hwnd, &ps);
        return;
    }

    HDC memDC = CreateCompatibleDC(hdc);
    HBITMAP memBitmap = CreateCompatibleBitmap(hdc, width, height);
    HGDIOBJ oldBitmap = SelectObject(memDC, memBitmap);

    Gdiplus::Graphics g(memDC);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    g.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);

    // 依疲勞狀態決定色彩
    Gdiplus::Color stateColor(255, 30, 177, 138);  // 綠
    const wchar_t* stateStr = L"清醒 (Relaxed)";
    if (m_level == FatigueLevel::Attention) {
        stateColor = Gdiplus::Color(255, 247, 227, 175); // 黃
        stateStr = L"注意 (Attention)";
    } else if (m_level == FatigueLevel::SevereWarning) {
        stateColor = Gdiplus::Color(255, 235, 87, 87);    // 紅
        stateStr = L"疲勞 (Severe)";
    }

    // 繪製圓角膠囊背景 (#25291C)
    Gdiplus::SolidBrush bgBrush(Gdiplus::Color(255, 37, 41, 28));
    Gdiplus::GraphicsPath path;
    int r = 18;
    path.AddArc(0, 0, r, r, 180, 90);
    path.AddArc(width - r - 1, 0, r, r, 270, 90);
    path.AddArc(width - r - 1, height - r - 1, r, r, 0, 90);
    path.AddArc(0, height - r - 1, r, r, 90, 90);
    path.CloseFigure();
    g.FillPath(&bgBrush, &path);

    // 外邊框光暈
    Gdiplus::Pen borderPen(stateColor, 1.8f);
    g.DrawPath(&borderPen, &path);

    // 呼吸狀態 LED 圓點
    float pulse = 1.0f + 0.2f * std::sin(m_pulseAnim);
    int dotR = static_cast<int>(6 * pulse);
    int dotX = 18;
    int dotY = height / 2;
    Gdiplus::SolidBrush dotBrush(stateColor);
    g.FillEllipse(&dotBrush, dotX - dotR, dotY - dotR, dotR * 2, dotR * 2);

    // 文字渲染
    Gdiplus::FontFamily fontFamily(L"Microsoft JhengHei");
    Gdiplus::StringFormat format;
    format.SetAlignment(Gdiplus::StringAlignmentNear);
    format.SetLineAlignment(Gdiplus::StringAlignmentCenter);

    // 第一行: 狀態標題
    Gdiplus::Font titleFont(&fontFamily, 11, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
    Gdiplus::SolidBrush titleBrush(stateColor);
    Gdiplus::RectF titleRect(36.0f, 6.0f, static_cast<float>(width - 40), 20.0f);
    g.DrawString(stateStr, -1, &titleFont, titleRect, &format, &titleBrush);

    // 第二行: 即時數值
    wchar_t metricsBuffer[64];
    swprintf_s(metricsBuffer, 64, L"EAR: %.3f | 分數: %.1f", static_cast<double>(m_ear), static_cast<double>(m_fatigueScore));
    Gdiplus::Font subFont(&fontFamily, 10, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
    Gdiplus::SolidBrush subBrush(Gdiplus::Color(255, 200, 200, 200));
    Gdiplus::RectF subRect(36.0f, 26.0f, static_cast<float>(width - 40), 20.0f);
    g.DrawString(metricsBuffer, -1, &subFont, subRect, &format, &subBrush);

    BitBlt(hdc, 0, 0, width, height, memDC, 0, 0, SRCCOPY);

    SelectObject(memDC, oldBitmap);
    DeleteObject(memBitmap);
    DeleteDC(memDC);

    EndPaint(hwnd, &ps);
}

LRESULT CALLBACK FatigueFloatingIndicator::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    FatigueFloatingIndicator* pThis = nullptr;
    if (msg == WM_NCCREATE) {
        CREATESTRUCT* pCreate = reinterpret_cast<CREATESTRUCT*>(lParam);
        pThis = reinterpret_cast<FatigueFloatingIndicator*>(pCreate->lpCreateParams);
        SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pThis));
    } else {
        pThis = reinterpret_cast<FatigueFloatingIndicator*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
    }

    if (!pThis) {
        return DefWindowProc(hwnd, msg, wParam, lParam);
    }

    switch (msg) {
    case WM_PAINT:
        pThis->onPaint(hwnd);
        return 0;

    case WM_TIMER:
        pThis->onTimerTick();
        return 0;

    case WM_LBUTTONDOWN:
        // 支援滑鼠拖曳整個無邊框視窗
        ReleaseCapture();
        SendMessage(hwnd, WM_NCLBUTTONDOWN, HTCAPTION, 0);
        return 0;

    case WM_LBUTTONDBLCLK:
        // 雙擊還原主視窗
        if (pThis->m_restoreCallback) {
            pThis->m_restoreCallback();
        }
        return 0;

    case WM_RBUTTONUP:
        // 右鍵點擊還原主視窗
        if (pThis->m_restoreCallback) {
            pThis->m_restoreCallback();
        }
        return 0;

    case WM_DESTROY:
        KillTimer(hwnd, 1);
        return 0;
    }

    return DefWindowProc(hwnd, msg, wParam, lParam);
}

} // namespace efd

#endif
