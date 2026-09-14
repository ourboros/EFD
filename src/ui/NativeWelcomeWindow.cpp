#include "NativeWelcomeWindow.hpp"
#include <iostream>
#include <cmath>
#include <sstream>
#include <iomanip>
#include <algorithm>

#ifdef _WIN32
#pragma comment(lib, "gdiplus.lib")

namespace efd {

NativeWelcomeWindow::NativeWelcomeWindow(int width, int height)
    : m_width(width), m_height(height), m_engine(PlatformType::Windows) {
    
    // 初始化 GDI+
    Gdiplus::GdiplusStartupInput gdiplusStartupInput;
    Gdiplus::GdiplusStartup(&m_gdiplusToken, &gdiplusStartupInput, NULL);

    // 嘗試多路徑載入 Logo (design/1x/資產 4.png 或 assets/logo.png)
    const wchar_t* possiblePaths[] = {
        L"assets/logo.png",
        L"assets/資產 4.png",
        L"design/1x/資產 4.png",
        L"../assets/logo.png",
        L"../design/1x/資產 4.png"
    };

    for (const wchar_t* p : possiblePaths) {
        auto img = std::make_unique<Gdiplus::Image>(p);
        if (img && img->GetLastStatus() == Gdiplus::Ok) {
            m_logoImage = std::move(img);
            break;
        }
    }

    // 綁定五執行緒引擎遙測事件
    m_engine.setTelemetryCallback([this](const EngineTelemetry& t) {
        this->m_latestTelemetry = t;
        if (this->m_hwnd && this->m_currentStage == UIStage::MainDashboard) {
            InvalidateRect(this->m_hwnd, NULL, FALSE);
        }
    });

    m_engine.setAlertCallback([this](FatigueLevel level, float score, const std::string& msg) {
        if (level == FatigueLevel::Attention || level == FatigueLevel::SevereWarning) {
            std::ostringstream oss;
            oss << "[警告] 疲勞指數 " << std::fixed << std::setprecision(1) << score << " - " << msg;
            this->m_dashboardMessage = oss.str();
        }
    });
}

NativeWelcomeWindow::~NativeWelcomeWindow() {
    m_engine.stop();
    m_logoImage.reset();
    if (m_gdiplusToken) {
        Gdiplus::GdiplusShutdown(m_gdiplusToken);
    }
}

void NativeWelcomeWindow::handleMouseClick(int x, int y) {
    POINT pt = { x, y };

    if (m_currentStage == UIStage::Welcome) {
        if (PtInRect(&m_startBtnRect, pt)) {
            // 從歡迎介面進入第二階段 (眼動數據提取說明)
            m_currentStage = UIStage::CalibrationGuide;
            m_animTimeSec = 0.0f;
            m_calibrationProgress = 0.0f;
            m_calibrationPhase = 0;
            
            // 啟動五執行緒分析管線
            m_engine.start();

            if (m_hwnd) InvalidateRect(m_hwnd, NULL, FALSE);
        }
    } else if (m_currentStage == UIStage::MainDashboard) {
        if (PtInRect(&m_recalibBtnRect, pt)) {
            // 重新校準
            m_currentStage = UIStage::CalibrationGuide;
            m_animTimeSec = 0.0f;
            m_calibrationProgress = 0.0f;
            m_calibrationPhase = 0;
            if (m_hwnd) InvalidateRect(m_hwnd, NULL, FALSE);
        }
    }
}

void NativeWelcomeWindow::onTimerTick() {
    constexpr float dt = 0.016f; // 60 FPS (約 16ms)
    m_animTimeSec += dt;

    if (m_currentStage == UIStage::CalibrationGuide) {
        // 第一階段說明與中央靜態凝視採樣 (持續 2.5 秒)
        m_calibrationProgress = std::clamp(m_animTimeSec / 2.5f, 0.0f, 1.0f);
        if (m_animTimeSec >= 2.5f) {
            // 轉入第三階段：動態多點眼動提取
            m_currentStage = UIStage::ActiveCalibration;
            m_animTimeSec = 0.0f;
            m_calibrationProgress = 0.0f;
            m_calibrationPhase = 0;
        }
        if (m_hwnd) InvalidateRect(m_hwnd, NULL, FALSE);
    } else if (m_currentStage == UIStage::ActiveCalibration) {
        // 第二階段動態眼動採樣：黃點平滑巡迴 4 個頂點與中心
        // 5 個錨點: 0.中心 -> 1.左上 -> 2.右上 -> 3.右下 -> 4.左下 -> 5.中心
        struct PointF { float x, y; };
        const PointF points[] = {
            { 0.50f, 0.50f }, // 初始中心
            { 0.12f, 0.15f }, // 左上 (圖二位置)
            { 0.88f, 0.15f }, // 右上 (圖二位置)
            { 0.88f, 0.80f }, // 右下
            { 0.12f, 0.80f }, // 左下
            { 0.50f, 0.50f }  // 回歸中心
        };

        float phaseDuration = 1.0f; // 每個區間 1 秒
        int currentSegment = static_cast<int>(m_animTimeSec / phaseDuration);

        if (currentSegment < 5) {
            float segT = (m_animTimeSec - currentSegment * phaseDuration) / phaseDuration;
            // 平滑餘弦插值 (Smooth Cosine Interpolation)
            float smoothT = 0.5f * (1.0f - std::cos(segT * 3.14159265f));

            PointF p0 = points[currentSegment];
            PointF p1 = points[currentSegment + 1];

            m_targetDotX = p0.x + (p1.x - p0.x) * smoothT;
            m_targetDotY = p0.y + (p1.y - p0.y) * smoothT;
            m_calibrationProgress = std::clamp((m_animTimeSec / 5.0f), 0.0f, 1.0f);
        } else {
            // 校準全部完成，進入即時監控儀表板
            m_engine.calibrate(3.0f);
            m_currentStage = UIStage::MainDashboard;
        }

        if (m_hwnd) InvalidateRect(m_hwnd, NULL, FALSE);
    }
}

LRESULT CALLBACK NativeWelcomeWindow::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    NativeWelcomeWindow* pThis = nullptr;
    if (msg == WM_NCCREATE) {
        CREATESTRUCT* pCreate = reinterpret_cast<CREATESTRUCT*>(lParam);
        pThis = reinterpret_cast<NativeWelcomeWindow*>(pCreate->lpCreateParams);
        SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pThis));
    } else {
        pThis = reinterpret_cast<NativeWelcomeWindow*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
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

    case WM_MOUSEMOVE: {
        int x = LOWORD(lParam);
        int y = HIWORD(lParam);
        POINT pt = { x, y };

        bool inStart = PtInRect(&pThis->m_startBtnRect, pt);
        bool inRecalib = PtInRect(&pThis->m_recalibBtnRect, pt);

        if (inStart != pThis->m_isHoveringStartBtn || inRecalib != pThis->m_isHoveringRecalibBtn) {
            pThis->m_isHoveringStartBtn = inStart;
            pThis->m_isHoveringRecalibBtn = inRecalib;
            SetCursor(LoadCursor(NULL, (inStart || inRecalib) ? IDC_HAND : IDC_ARROW));
            InvalidateRect(hwnd, NULL, FALSE);
        }

        TRACKMOUSEEVENT tme = { sizeof(TRACKMOUSEEVENT), TME_LEAVE, hwnd, 0 };
        TrackMouseEvent(&tme);
        return 0;
    }

    case WM_LBUTTONUP: {
        int x = LOWORD(lParam);
        int y = HIWORD(lParam);
        pThis->handleMouseClick(x, y);
        return 0;
    }

    case WM_DESTROY:
        KillTimer(hwnd, 1);
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProc(hwnd, msg, wParam, lParam);
}

void NativeWelcomeWindow::onPaint(HWND hwnd) {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);

    RECT clientRect;
    GetClientRect(hwnd, &clientRect);
    int width = clientRect.right - clientRect.left;
    int height = clientRect.bottom - clientRect.top;

    // 雙緩衝繪製 (Double Buffering) 杜絕閃爍
    HDC memDC = CreateCompatibleDC(hdc);
    HBITMAP memBitmap = CreateCompatibleBitmap(hdc, width, height);
    HGDIOBJ oldBitmap = SelectObject(memDC, memBitmap);

    Gdiplus::Graphics g(memDC);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    g.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);

    // 依據當前階段分發繪製
    switch (m_currentStage) {
    case UIStage::Welcome:
        drawWelcomeScreen(g, width, height);
        break;
    case UIStage::CalibrationGuide:
        drawCalibrationGuide(g, width, height);
        break;
    case UIStage::ActiveCalibration:
        drawActiveCalibration(g, width, height);
        break;
    case UIStage::MainDashboard:
        drawMainDashboard(g, width, height);
        break;
    }

    BitBlt(hdc, 0, 0, width, height, memDC, 0, 0, SRCCOPY);

    SelectObject(memDC, oldBitmap);
    DeleteObject(memBitmap);
    DeleteDC(memDC);

    EndPaint(hwnd, &ps);
}

// -----------------------------------------------------------------------------
// 階段 1：系統歡迎介面 (Logo + 感謝協助測試EFD + 開始按鈕)
// -----------------------------------------------------------------------------
void NativeWelcomeWindow::drawWelcomeScreen(Gdiplus::Graphics& g, int w, int h) {
    // 滿版薄荷綠背景 (#1EB18A)
    Gdiplus::SolidBrush bgBrush(Gdiplus::Color(255, 30, 177, 138));
    g.FillRectangle(&bgBrush, 0, 0, w, h);

    // 1. 繪製 Logo (design/1x/資產 4.png)
    int logoSize = 130;
    int logoX = (w - logoSize) / 2;
    int logoY = h / 2 - 170;

    if (m_logoImage && m_logoImage->GetLastStatus() == Gdiplus::Ok) {
        g.DrawImage(m_logoImage.get(), logoX, logoY, logoSize, logoSize);
    } else {
        // 若找不到圖檔則繪製高質感備援 Logo 標章
        Gdiplus::SolidBrush logoBg(Gdiplus::Color(255, 255, 255, 255));
        g.FillEllipse(&logoBg, logoX, logoY, logoSize, logoSize);
        Gdiplus::SolidBrush logoInner(Gdiplus::Color(255, 30, 177, 138));
        g.FillEllipse(&logoInner, logoX + 15, logoY + 15, logoSize - 30, logoSize - 30);
    }

    // 2. 繪製標題文字: "感謝協助測試EFD" (純白 #FFFFFF)
    Gdiplus::FontFamily fontFamily(L"Microsoft JhengHei");
    Gdiplus::Font titleFont(&fontFamily, 32, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
    Gdiplus::SolidBrush textBrush(Gdiplus::Color(255, 255, 255, 255));

    Gdiplus::StringFormat format;
    format.SetAlignment(Gdiplus::StringAlignmentCenter);
    format.SetLineAlignment(Gdiplus::StringAlignmentCenter);

    Gdiplus::RectF titleRect(0.0f, static_cast<float>(h / 2 - 20), static_cast<float>(w), 50.0f);
    g.DrawString(L"感謝協助測試EFD", -1, &titleFont, titleRect, &format, &textBrush);

    // 3. 繪製開始按鈕: "開始進入系統"
    int btnWidth = 220;
    int btnHeight = 52;
    int btnX = (w - btnWidth) / 2;
    int btnY = h / 2 + 60;
    m_startBtnRect = { btnX, btnY, btnX + btnWidth, btnY + btnHeight };

    Gdiplus::Color btnColor = m_isHoveringStartBtn ? Gdiplus::Color(255, 245, 245, 245) : Gdiplus::Color(255, 255, 255, 255);
    Gdiplus::SolidBrush btnBrush(btnColor);

    // 繪製圓角矩形按鈕
    Gdiplus::GraphicsPath path;
    int r = 16;
    path.AddArc(btnX, btnY, r, r, 180, 90);
    path.AddArc(btnX + btnWidth - r, btnY, r, r, 270, 90);
    path.AddArc(btnX + btnWidth - r, btnY + btnHeight - r, r, r, 0, 90);
    path.AddArc(btnX, btnY + btnHeight - r, r, r, 90, 90);
    path.CloseFigure();
    g.FillPath(&btnBrush, &path);

    // 按鈕文字: 薄荷綠 (#1EB18A)
    Gdiplus::Font btnFont(&fontFamily, 18, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
    Gdiplus::SolidBrush btnTextBrush(Gdiplus::Color(255, 30, 177, 138));
    Gdiplus::RectF btnTextRect(static_cast<float>(btnX), static_cast<float>(btnY), static_cast<float>(btnWidth), static_cast<float>(btnHeight));
    g.DrawString(L"開始進入系統", -1, &btnFont, btnTextRect, &format, &btnTextBrush);
}

// -----------------------------------------------------------------------------
// 階段 2：眼動數據提取說明介面 (中央黃點 + 底部導引文字)
// -----------------------------------------------------------------------------
void NativeWelcomeWindow::drawCalibrationGuide(Gdiplus::Graphics& g, int w, int h) {
    Gdiplus::SolidBrush bgBrush(Gdiplus::Color(255, 30, 177, 138));
    g.FillRectangle(&bgBrush, 0, 0, w, h);

    // 1. 中央暖黃色圓點標靶 (#F7E3AF)
    int dotDiameter = 48;
    int dotX = (w - dotDiameter) / 2;
    int dotY = (h - dotDiameter) / 2;

    Gdiplus::SolidBrush yellowDotBrush(Gdiplus::Color(255, 247, 227, 175));
    g.FillEllipse(&yellowDotBrush, dotX, dotY, dotDiameter, dotDiameter);

    // 外圈微光波紋動畫
    float pulse = 1.0f + 0.15f * std::sin(m_animTimeSec * 6.0f);
    int pulseSize = static_cast<int>(dotDiameter * pulse);
    int pulseX = (w - pulseSize) / 2;
    int pulseY = (h - pulseSize) / 2;
    Gdiplus::Pen pulsePen(Gdiplus::Color(100, 247, 227, 175), 2.0f);
    g.DrawEllipse(&pulsePen, pulseX, pulseY, pulseSize, pulseSize);

    // 2. 底部說明文字: "請凝視畫面上的黃點並跟隨他移動" (純白 #FFFFFF)
    Gdiplus::FontFamily fontFamily(L"Microsoft JhengHei");
    Gdiplus::Font guideFont(&fontFamily, 26, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
    Gdiplus::SolidBrush textBrush(Gdiplus::Color(255, 255, 255, 255));

    Gdiplus::StringFormat format;
    format.SetAlignment(Gdiplus::StringAlignmentCenter);
    format.SetLineAlignment(Gdiplus::StringAlignmentCenter);

    Gdiplus::RectF textRect(0.0f, static_cast<float>(h - 100), static_cast<float>(w), 50.0f);
    g.DrawString(L"請凝視畫面上的黃點並跟隨他移動", -1, &guideFont, textRect, &format, &textBrush);
}

// -----------------------------------------------------------------------------
// 階段 3：眼動數據動態提取介面 (動態移動黃點採樣)
// -----------------------------------------------------------------------------
void NativeWelcomeWindow::drawActiveCalibration(Gdiplus::Graphics& g, int w, int h) {
    Gdiplus::SolidBrush bgBrush(Gdiplus::Color(255, 30, 177, 138));
    g.FillRectangle(&bgBrush, 0, 0, w, h);

    // 繪製動態移動的暖黃色標靶點 (#F7E3AF)
    int dotDiameter = 48;
    int posX = static_cast<int>(m_targetDotX * static_cast<float>(w)) - dotDiameter / 2;
    int posY = static_cast<int>(m_targetDotY * static_cast<float>(h)) - dotDiameter / 2;

    Gdiplus::SolidBrush yellowDotBrush(Gdiplus::Color(255, 247, 227, 175));
    g.FillEllipse(&yellowDotBrush, posX, posY, dotDiameter, dotDiameter);

    // 底部即時進度提示
    Gdiplus::FontFamily fontFamily(L"Microsoft JhengHei");
    Gdiplus::Font progressFont(&fontFamily, 20, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
    Gdiplus::SolidBrush textBrush(Gdiplus::Color(220, 255, 255, 255));

    Gdiplus::StringFormat format;
    format.SetAlignment(Gdiplus::StringAlignmentCenter);
    format.SetLineAlignment(Gdiplus::StringAlignmentCenter);

    int pct = static_cast<int>(m_calibrationProgress * 100.0f);
    std::wstring pStr = L"眼動特徵多角度提取中... (" + std::to_wstring(pct) + L"%)";
    Gdiplus::RectF progressRect(0.0f, static_cast<float>(h - 70), static_cast<float>(w), 40.0f);
    g.DrawString(pStr.c_str(), -1, &progressFont, progressRect, &format, &textBrush);
}

// -----------------------------------------------------------------------------
// 階段 4：即時疲勞監控儀表板 (Dashboard)
// -----------------------------------------------------------------------------
void NativeWelcomeWindow::drawMainDashboard(Gdiplus::Graphics& g, int w, int h) {
    // 深黑背景 (#25291C)
    Gdiplus::SolidBrush bgBrush(Gdiplus::Color(255, 37, 41, 28));
    g.FillRectangle(&bgBrush, 0, 0, w, h);

    Gdiplus::FontFamily fontFamily(L"Microsoft JhengHei");
    Gdiplus::StringFormat centerFormat;
    centerFormat.SetAlignment(Gdiplus::StringAlignmentCenter);
    centerFormat.SetLineAlignment(Gdiplus::StringAlignmentCenter);

    // 1. 頂部標題
    Gdiplus::Font headerFont(&fontFamily, 24, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
    Gdiplus::SolidBrush whiteBrush(Gdiplus::Color(255, 255, 255, 255));
    Gdiplus::RectF headerRect(0.0f, 25.0f, static_cast<float>(w), 40.0f);
    g.DrawString(L"EFD 即時眼睛疲勞監控中心", -1, &headerFont, headerRect, &centerFormat, &whiteBrush);

    // 2. 核心狀態大卡片
    int cardW = w - 120;
    int cardH = 140;
    int cardX = 60;
    int cardY = 85;

    Gdiplus::Color statusColor = Gdiplus::Color(255, 30, 177, 138); // 正常綠
    const wchar_t* statusText = L"生理狀態：正常清醒 (Relaxed)";

    if (m_latestTelemetry.systemState.fatigueLevel == FatigueLevel::Attention) {
        statusColor = Gdiplus::Color(255, 247, 227, 175); // 注意黃
        statusText = L"生理狀態：輕度用眼疲勞 (Attention)";
    } else if (m_latestTelemetry.systemState.fatigueLevel == FatigueLevel::SevereWarning) {
        statusColor = Gdiplus::Color(255, 235, 87, 87); // 警告紅
        statusText = L"生理狀態：重度疲勞！建議閉眼休息 (Severe)";
    }

    Gdiplus::SolidBrush cardBrush(statusColor);
    g.FillRectangle(&cardBrush, cardX, cardY, cardW, cardH);

    Gdiplus::Font cardFont(&fontFamily, 26, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
    Gdiplus::SolidBrush cardTextBrush(Gdiplus::Color(255, 37, 41, 28));
    Gdiplus::RectF cardTextRect(static_cast<float>(cardX), static_cast<float>(cardY + 20), static_cast<float>(cardW), 40.0f);
    g.DrawString(statusText, -1, &cardFont, cardTextRect, &centerFormat, &cardTextBrush);

    // 3. 即時遙測數據面板 (4 欄網格)
    int gridY = 250;
    int itemW = cardW / 4;
    
    auto drawMetric = [&](int col, const wchar_t* label, const std::wstring& val) {
        int ix = cardX + col * itemW;
        Gdiplus::SolidBrush boxBrush(Gdiplus::Color(255, 50, 56, 38));
        g.FillRectangle(&boxBrush, ix + 5, gridY, itemW - 10, 110);

        Gdiplus::Font lFont(&fontFamily, 14, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
        Gdiplus::SolidBrush lBrush(Gdiplus::Color(255, 180, 180, 180));
        Gdiplus::RectF lRect(static_cast<float>(ix), static_cast<float>(gridY + 15), static_cast<float>(itemW), 25.0f);
        g.DrawString(label, -1, &lFont, lRect, &centerFormat, &lBrush);

        Gdiplus::Font vFont(&fontFamily, 22, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
        Gdiplus::SolidBrush vBrush(Gdiplus::Color(255, 150, 197, 247)); // 科技藍 (#96C5F7)
        Gdiplus::RectF vRect(static_cast<float>(ix), static_cast<float>(gridY + 45), static_cast<float>(itemW), 40.0f);
        g.DrawString(val.c_str(), -1, &vFont, vRect, &centerFormat, &vBrush);
    };

    wchar_t b1[32], b2[32], b3[32], b4[32];
    swprintf_s(b1, L"%.3f", m_latestTelemetry.eyeMetrics.earAvg);
    swprintf_s(b2, L"%.1f%%", m_latestTelemetry.eyeMetrics.perclos * 100.0f);
    swprintf_s(b3, L"%.2f", m_latestTelemetry.complexityMetrics.complexityIndex);
    swprintf_s(b4, L"%.1f", m_latestTelemetry.systemState.currentFatigueScore);

    drawMetric(0, L"雙眼 EAR", b1);
    drawMetric(1, L"PERCLOS 閉眼比", b2);
    drawMetric(2, L"複雜度 (MSE)", b3);
    drawMetric(3, L"綜合疲勞分數", b4);

    // 4. 底部狀態條與重新校準按鈕
    int btnW = 160;
    int btnH = 44;
    int btnX = (w - btnW) / 2;
    int btnY = h - 90;
    m_recalibBtnRect = { btnX, btnY, btnX + btnW, btnY + btnH };

    Gdiplus::SolidBrush recBtnBrush(m_isHoveringRecalibBtn ? Gdiplus::Color(255, 45, 185, 145) : Gdiplus::Color(255, 30, 177, 138));
    g.FillRectangle(&recBtnBrush, btnX, btnY, btnW, btnH);

    Gdiplus::Font btnFont(&fontFamily, 16, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
    Gdiplus::RectF btnTextRect(static_cast<float>(btnX), static_cast<float>(btnY), static_cast<float>(btnW), static_cast<float>(btnH));
    g.DrawString(L"重新校準基準", -1, &btnFont, btnTextRect, &centerFormat, &whiteBrush);
}

int NativeWelcomeWindow::run() {
    WNDCLASSEXW wc = { sizeof(WNDCLASSEXW) };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.lpszClassName = L"EFD_FullNativeWindow";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.style = CS_HREDRAW | CS_VREDRAW;

    RegisterClassExW(&wc);

    int screenWidth = GetSystemMetrics(SM_CXSCREEN);
    int screenHeight = GetSystemMetrics(SM_CYSCREEN);
    int posX = (screenWidth - m_width) / 2;
    int posY = (screenHeight - m_height) / 2;

    m_hwnd = CreateWindowExW(
        0,
        wc.lpszClassName,
        L"Eye Fatigue Detection (EFD) - 視覺校準與疲勞監控系統",
        WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        posX, posY, m_width, m_height,
        NULL, NULL, wc.hInstance, this
    );

    if (!m_hwnd) {
        return -1;
    }

    // 啟動 60 FPS 畫面更新計時器
    SetTimer(m_hwnd, 1, 16, NULL);

    ShowWindow(m_hwnd, SW_SHOW);
    UpdateWindow(m_hwnd);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return static_cast<int>(msg.wParam);
}

} // namespace efd

#endif
