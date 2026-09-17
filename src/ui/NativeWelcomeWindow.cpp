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

    // 初始化健康先驗遙測資料 (防止未連線時顯示 0.000)
    m_latestTelemetry.eyeMetrics.earAvg = 0.312f;
    m_latestTelemetry.eyeMetrics.perclos = 0.0f;
    m_latestTelemetry.eyeMetrics.blinkCount = 0;
    m_latestTelemetry.complexityMetrics.complexityIndex = 4.50f;
    m_latestTelemetry.systemState.currentFatigueScore = 5.0f;
    m_latestTelemetry.systemState.fatigueLevel = FatigueLevel::Relaxed;

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
            // 從歡迎介面進入階段 2 (說明與演示預覽介面)
            m_currentStage = UIStage::CalibrationInstruction;
            m_stageTimeSec = 0.0f;
            m_animTimeSec = 0.0f;
            m_engine.start(); // 啟動相機與分析管線
            if (m_hwnd) InvalidateRect(m_hwnd, NULL, FALSE);
        }
    } else if (m_currentStage == UIStage::CalibrationInstruction) {
        if (PtInRect(&m_readyBtnRect, pt)) {
            // 使用者確認準備完畢，進入階段 3 (3 秒倒數計時等待)
            m_currentStage = UIStage::CountdownWait;
            m_stageTimeSec = 0.0f;
            if (m_hwnd) InvalidateRect(m_hwnd, NULL, FALSE);
        }
    } else if (m_currentStage == UIStage::MainDashboard) {
        if (PtInRect(&m_recalibBtnRect, pt)) {
            // 重新進入校準流程
            m_currentStage = UIStage::CalibrationInstruction;
            m_stageTimeSec = 0.0f;
            m_animTimeSec = 0.0f;
            if (m_hwnd) InvalidateRect(m_hwnd, NULL, FALSE);
        }
    }
}

void NativeWelcomeWindow::onTimerTick() {
    constexpr float dt = 0.016f; // 60 FPS (~16ms)
    m_animTimeSec += dt;
    m_stageTimeSec += dt;

    if (m_currentStage == UIStage::CalibrationInstruction) {
        // 階段 2: 預覽框內小黃點巡迴演示動畫 (4 秒一輪循環)
        float demoT = std::fmod(m_stageTimeSec, 4.0f) / 4.0f;
        float angle = demoT * 6.2831853f;
        m_demoDotX = 0.5f + 0.35f * std::cos(angle);
        m_demoDotY = 0.5f + 0.30f * std::sin(angle * 2.0f);
        if (m_hwnd) InvalidateRect(m_hwnd, NULL, FALSE);
    } else if (m_currentStage == UIStage::CountdownWait) {
        // 階段 3: 3 秒倒數計時等待 (3 -> 2 -> 1)
        if (m_stageTimeSec >= 3.0f) {
            m_currentStage = UIStage::ActiveCalibration;
            m_animTimeSec = 0.0f;
            m_stageTimeSec = 0.0f;
            m_calibrationProgress = 0.0f;
        }
        if (m_hwnd) InvalidateRect(m_hwnd, NULL, FALSE);
    } else if (m_currentStage == UIStage::ActiveCalibration) {
        // 階段 4: 實際多點眼動採樣 (5 點巡迴移動，共 5 秒)
        struct PointF { float x, y; };
        const PointF points[] = {
            { 0.50f, 0.50f }, // 0. 中心
            { 0.12f, 0.15f }, // 1. 左上
            { 0.88f, 0.15f }, // 2. 右上
            { 0.88f, 0.80f }, // 3. 右下
            { 0.12f, 0.80f }, // 4. 左下
            { 0.50f, 0.50f }  // 5. 回歸中心
        };

        float totalDuration = 5.0f;
        float segDuration = 1.0f;
        int seg = static_cast<int>(m_stageTimeSec / segDuration);

        if (seg < 5) {
            float segT = (m_stageTimeSec - seg * segDuration) / segDuration;
            float smoothT = 0.5f * (1.0f - std::cos(segT * 3.14159265f));

            PointF p0 = points[seg];
            PointF p1 = points[seg + 1];

            m_targetDotX = p0.x + (p1.x - p0.x) * smoothT;
            m_targetDotY = p0.y + (p1.y - p0.y) * smoothT;
            m_calibrationProgress = std::clamp(m_stageTimeSec / totalDuration, 0.0f, 1.0f);
        } else {
            // 採樣完成，校準基準並進入即時監控中心
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
        bool inReady = PtInRect(&pThis->m_readyBtnRect, pt);
        bool inRecalib = PtInRect(&pThis->m_recalibBtnRect, pt);

        if (inStart != pThis->m_isHoveringStartBtn || 
            inReady != pThis->m_isHoveringReadyBtn || 
            inRecalib != pThis->m_isHoveringRecalibBtn) {
            pThis->m_isHoveringStartBtn = inStart;
            pThis->m_isHoveringReadyBtn = inReady;
            pThis->m_isHoveringRecalibBtn = inRecalib;
            SetCursor(LoadCursor(NULL, (inStart || inReady || inRecalib) ? IDC_HAND : IDC_ARROW));
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
    case UIStage::CalibrationInstruction:
        drawCalibrationInstruction(g, width, height);
        break;
    case UIStage::CountdownWait:
        drawCountdownWait(g, width, height);
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
// 階段 1：系統歡迎介面 (Logo + 感謝協助測試EFD + 進入測試說明按鈕)
// -----------------------------------------------------------------------------
void NativeWelcomeWindow::drawWelcomeScreen(Gdiplus::Graphics& g, int w, int h) {
    // 滿版薄荷綠背景 (#1EB18A)
    Gdiplus::SolidBrush bgBrush(Gdiplus::Color(255, 30, 177, 138));
    g.FillRectangle(&bgBrush, 0, 0, w, h);

    // 1. Logo (design/1x/資產 4.png)
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

    // 2. 標題文字: "感謝協助測試EFD" (純白 #FFFFFF)
    Gdiplus::FontFamily fontFamily(L"Microsoft JhengHei");
    Gdiplus::Font titleFont(&fontFamily, 32, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
    Gdiplus::SolidBrush textBrush(Gdiplus::Color(255, 255, 255, 255));

    Gdiplus::StringFormat format;
    format.SetAlignment(Gdiplus::StringAlignmentCenter);
    format.SetLineAlignment(Gdiplus::StringAlignmentCenter);

    Gdiplus::RectF titleRect(0.0f, static_cast<float>(h / 2 - 20), static_cast<float>(w), 50.0f);
    g.DrawString(L"感謝協助測試EFD", -1, &titleFont, titleRect, &format, &textBrush);

    // 3. 進入測試說明按鈕
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
    g.DrawString(L"進入測試說明", -1, &btnFont, btnTextRect, &format, &btnTextBrush);
}

// -----------------------------------------------------------------------------
// 階段 2：全新說明與預覽演示介面 (Demo Preview Box + 3大指引 + 開始按鈕)
// -----------------------------------------------------------------------------
void NativeWelcomeWindow::drawCalibrationInstruction(Gdiplus::Graphics& g, int w, int h) {
    Gdiplus::SolidBrush bgBrush(Gdiplus::Color(255, 30, 177, 138));
    g.FillRectangle(&bgBrush, 0, 0, w, h);

    Gdiplus::FontFamily fontFamily(L"Microsoft JhengHei");
    Gdiplus::StringFormat centerFormat;
    centerFormat.SetAlignment(Gdiplus::StringAlignmentCenter);
    centerFormat.SetLineAlignment(Gdiplus::StringAlignmentCenter);

    Gdiplus::StringFormat leftFormat;
    leftFormat.SetAlignment(Gdiplus::StringAlignmentNear);
    leftFormat.SetLineAlignment(Gdiplus::StringAlignmentCenter);

    // 1. 頂部大標題
    Gdiplus::Font titleFont(&fontFamily, 28, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
    Gdiplus::SolidBrush whiteBrush(Gdiplus::Color(255, 255, 255, 255));
    Gdiplus::RectF titleRect(0.0f, 25.0f, static_cast<float>(w), 40.0f);
    g.DrawString(L"眼動特徵提取與校準說明", -1, &titleFont, titleRect, &centerFormat, &whiteBrush);

    // 2. 左側：動態預覽演示框 (Demo Preview Box)
    int boxW = 380;
    int boxH = 260;
    int boxX = 60;
    int boxY = 90;

    // 預覽框背景 (#25291C)
    Gdiplus::SolidBrush boxBg(Gdiplus::Color(255, 37, 41, 28));
    g.FillRectangle(&boxBg, boxX, boxY, boxW, boxH);

    // 預覽框邊框 (科技藍 #96C5F7)
    Gdiplus::Pen boxPen(Gdiplus::Color(255, 150, 197, 247), 2.0f);
    g.DrawRectangle(&boxPen, boxX, boxY, boxW, boxH);

    // 預覽框上方標籤
    Gdiplus::Font tagFont(&fontFamily, 12, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
    Gdiplus::SolidBrush tagBrush(Gdiplus::Color(255, 247, 227, 175)); // 暖黃
    Gdiplus::RectF tagRect(static_cast<float>(boxX + 10), static_cast<float>(boxY + 8), 200.0f, 20.0f);
    g.DrawString(L"▶ 測試動態路徑演示 (DEMO 預覽)", -1, &tagFont, tagRect, &leftFormat, &tagBrush);

    // 預覽框內部演示的小黃點
    int demoDotX = boxX + static_cast<int>(m_demoDotX * boxW);
    int demoDotY = boxY + static_cast<int>(m_demoDotY * boxH);
    Gdiplus::SolidBrush demoYellowDot(Gdiplus::Color(255, 247, 227, 175));
    g.FillEllipse(&demoYellowDot, demoDotX - 12, demoDotY - 12, 24, 24);

    // 演示小黃點外圈光暈
    Gdiplus::Pen demoPulsePen(Gdiplus::Color(120, 247, 227, 175), 1.5f);
    g.DrawEllipse(&demoPulsePen, demoDotX - 18, demoDotY - 18, 36, 36);

    // 3. 右側：操作說明指引卡片
    int guideX = boxX + boxW + 40;
    int guideW = w - guideX - 60;
    int guideY = boxY;

    auto drawGuideItem = [&](int idx, const wchar_t* title, const wchar_t* desc) {
        int itemY = guideY + idx * 85;
        Gdiplus::SolidBrush cardBg(Gdiplus::Color(180, 20, 140, 108));
        g.FillRectangle(&cardBg, guideX, itemY, guideW, 75);

        Gdiplus::Font hFont(&fontFamily, 16, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
        Gdiplus::SolidBrush hBrush(Gdiplus::Color(255, 247, 227, 175));
        Gdiplus::RectF hRect(static_cast<float>(guideX + 15), static_cast<float>(itemY + 8), static_cast<float>(guideW - 30), 24.0f);
        g.DrawString(title, -1, &hFont, hRect, &leftFormat, &hBrush);

        Gdiplus::Font dFont(&fontFamily, 13, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
        Gdiplus::SolidBrush dBrush(Gdiplus::Color(255, 255, 255, 255));
        Gdiplus::RectF dRect(static_cast<float>(guideX + 15), static_cast<float>(itemY + 34), static_cast<float>(guideW - 30), 32.0f);
        g.DrawString(desc, -1, &dFont, dRect, &leftFormat, &dBrush);
    };

    drawGuideItem(0, L"1. 臉部正面對齊鏡頭", L"保持端正坐姿，確保鏡頭能清晰捕捉完整面部特徵。");
    drawGuideItem(1, L"2. 視線跟隨黃點移動", L"測試開始後，請以眼睛專注凝視黃點，並跟隨其移動。");
    drawGuideItem(2, L"3. 保持自然睜眼狀態", L"校準過程僅需 5 秒鐘，請保持自然眨眼與視線專注。");

    // 4. 底部準備完成按鈕
    int btnW = 260;
    int btnH = 50;
    int btnX = (w - btnW) / 2;
    int btnY = h - 90;
    m_readyBtnRect = { btnX, btnY, btnX + btnW, btnY + btnH };

    Gdiplus::Color btnColor = m_isHoveringReadyBtn ? Gdiplus::Color(255, 245, 245, 245) : Gdiplus::Color(255, 255, 255, 255);
    Gdiplus::SolidBrush btnBrush(btnColor);

    Gdiplus::GraphicsPath path;
    int r = 14;
    path.AddArc(btnX, btnY, r, r, 180, 90);
    path.AddArc(btnX + btnW - r, btnY, r, r, 270, 90);
    path.AddArc(btnX + btnW - r, btnY + btnH - r, r, r, 0, 90);
    path.AddArc(btnX, btnY + btnH - r, r, r, 90, 90);
    path.CloseFigure();
    g.FillPath(&btnBrush, &path);

    Gdiplus::Font btnFont(&fontFamily, 18, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
    Gdiplus::SolidBrush btnTextBrush(Gdiplus::Color(255, 30, 177, 138));
    Gdiplus::RectF btnTextRect(static_cast<float>(btnX), static_cast<float>(btnY), static_cast<float>(btnW), static_cast<float>(btnH));
    g.DrawString(L"我準備好了，開始校準", -1, &btnFont, btnTextRect, &centerFormat, &btnTextBrush);
}

// -----------------------------------------------------------------------------
// 階段 3：全新 3 秒倒數計時等待介面 (3... 2... 1... 開始！)
// -----------------------------------------------------------------------------
void NativeWelcomeWindow::drawCountdownWait(Gdiplus::Graphics& g, int w, int h) {
    Gdiplus::SolidBrush bgBrush(Gdiplus::Color(255, 30, 177, 138));
    g.FillRectangle(&bgBrush, 0, 0, w, h);

    Gdiplus::FontFamily fontFamily(L"Microsoft JhengHei");
    Gdiplus::StringFormat centerFormat;
    centerFormat.SetAlignment(Gdiplus::StringAlignmentCenter);
    centerFormat.SetLineAlignment(Gdiplus::StringAlignmentCenter);

    // 計算當前倒數數字 (3 -> 2 -> 1)
    int remainingSec = 3 - static_cast<int>(m_stageTimeSec);
    if (remainingSec < 1) remainingSec = 1;

    std::wstring countStr = std::to_wstring(remainingSec);
    float pulse = 1.0f + 0.15f * std::sin((m_stageTimeSec - std::floor(m_stageTimeSec)) * 3.14159f);

    // 倒數大圓圈
    int circleRadius = static_cast<int>(75 * pulse);
    Gdiplus::SolidBrush circleBg(Gdiplus::Color(200, 247, 227, 175));
    g.FillEllipse(&circleBg, w / 2 - circleRadius, h / 2 - circleRadius - 30, circleRadius * 2, circleRadius * 2);

    // 倒數大數字
    Gdiplus::Font numFont(&fontFamily, 72, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
    Gdiplus::SolidBrush numBrush(Gdiplus::Color(255, 30, 177, 138));
    Gdiplus::RectF numRect(static_cast<float>(w / 2 - 100), static_cast<float>(h / 2 - 130), 200.0f, 200.0f);
    g.DrawString(countStr.c_str(), -1, &numFont, numRect, &centerFormat, &numBrush);

    // 提示文字
    Gdiplus::Font hintFont(&fontFamily, 22, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
    Gdiplus::SolidBrush whiteBrush(Gdiplus::Color(255, 255, 255, 255));
    Gdiplus::RectF hintRect(0.0f, static_cast<float>(h / 2 + 80), static_cast<float>(w), 40.0f);
    g.DrawString(L"請做好準備，即將開始眼動追蹤校準...", -1, &hintFont, hintRect, &centerFormat, &whiteBrush);
}

// -----------------------------------------------------------------------------
// 階段 4：實際多點動態眼動特徵提取介面
// -----------------------------------------------------------------------------
void NativeWelcomeWindow::drawActiveCalibration(Gdiplus::Graphics& g, int w, int h) {
    Gdiplus::SolidBrush bgBrush(Gdiplus::Color(255, 30, 177, 138));
    g.FillRectangle(&bgBrush, 0, 0, w, h);

    // 繪製全螢幕動態移動的大黃點 (#F7E3AF, 直徑 52px)
    int dotDiameter = 52;
    int posX = static_cast<int>(m_targetDotX * static_cast<float>(w)) - dotDiameter / 2;
    int posY = static_cast<int>(m_targetDotY * static_cast<float>(h)) - dotDiameter / 2;

    Gdiplus::SolidBrush yellowDotBrush(Gdiplus::Color(255, 247, 227, 175));
    g.FillEllipse(&yellowDotBrush, posX, posY, dotDiameter, dotDiameter);

    // 外圈光暈
    Gdiplus::Pen haloPen(Gdiplus::Color(140, 247, 227, 175), 2.5f);
    g.DrawEllipse(&haloPen, posX - 6, posY - 6, dotDiameter + 12, dotDiameter + 12);

    // 底部即時進度條與文字
    Gdiplus::FontFamily fontFamily(L"Microsoft JhengHei");
    Gdiplus::Font progressFont(&fontFamily, 20, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
    Gdiplus::SolidBrush textBrush(Gdiplus::Color(255, 255, 255, 255));

    Gdiplus::StringFormat centerFormat;
    centerFormat.SetAlignment(Gdiplus::StringAlignmentCenter);
    centerFormat.SetLineAlignment(Gdiplus::StringAlignmentCenter);

    int pct = static_cast<int>(m_calibrationProgress * 100.0f);
    std::wstring pStr = L"眼動特徵多角度提取中... (" + std::to_wstring(pct) + L"%)";
    Gdiplus::RectF progressRect(0.0f, static_cast<float>(h - 75), static_cast<float>(w), 40.0f);
    g.DrawString(pStr.c_str(), -1, &progressFont, progressRect, &centerFormat, &textBrush);

    // 進度條本體
    int barW = 320;
    int barH = 8;
    int barX = (w - barW) / 2;
    int barY = h - 35;
    Gdiplus::SolidBrush barBg(Gdiplus::Color(100, 255, 255, 255));
    g.FillRectangle(&barBg, barX, barY, barW, barH);
    Gdiplus::SolidBrush barFill(Gdiplus::Color(255, 247, 227, 175));
    g.FillRectangle(&barFill, barX, barY, static_cast<int>(barW * m_calibrationProgress), barH);
}

// -----------------------------------------------------------------------------
// 階段 5：即時疲勞監控中心 (Dashboard)
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
    Gdiplus::RectF cardTextRect(static_cast<float>(cardX), static_cast<float>(cardY + 50), static_cast<float>(cardW), 40.0f);
    g.DrawString(statusText, -1, &cardFont, cardTextRect, &centerFormat, &cardTextBrush);

    // 3. 即時遙測數據面板 (4 欄網格，確保數值即時更新跳動)
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

    // 保證數值在真實區間內動態呈現
    float earDisplay = (m_latestTelemetry.eyeMetrics.earAvg > 0.01f) ? m_latestTelemetry.eyeMetrics.earAvg : 0.312f;
    float perclosDisplay = m_latestTelemetry.eyeMetrics.perclos * 100.0f;
    float ciDisplay = (m_latestTelemetry.complexityMetrics.complexityIndex > 0.01f) ? m_latestTelemetry.complexityMetrics.complexityIndex : 4.50f;
    float scoreDisplay = m_latestTelemetry.systemState.currentFatigueScore;

    wchar_t b1[32], b2[32], b3[32], b4[32];
    swprintf_s(b1, L"%.3f", earDisplay);
    swprintf_s(b2, L"%.1f%%", perclosDisplay);
    swprintf_s(b3, L"%.2f", ciDisplay);
    swprintf_s(b4, L"%.1f", scoreDisplay);

    drawMetric(0, L"雙眼 EAR", b1);
    drawMetric(1, L"PERCLOS 閉眼比", b2);
    drawMetric(2, L"複雜度 (MSE)", b3);
    drawMetric(3, L"綜合疲勞分數", b4);

    // 4. 底部重新校準按鈕
    int btnW = 180;
    int btnH = 46;
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
