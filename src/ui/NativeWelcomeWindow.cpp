#include "NativeWelcomeWindow.hpp"
#include <iostream>
#include <cmath>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <vector>
#include <string>

#ifdef _WIN32
#pragma comment(lib, "gdiplus.lib")

namespace efd {

namespace {

// 智慧資產載入器：深入探索執行檔目錄、專案根目錄及其各層級父目錄
std::unique_ptr<Gdiplus::Image> loadAssetImage(const std::wstring& filename) {
    WCHAR exePath[MAX_PATH] = { 0 };
    GetModuleFileNameW(NULL, exePath, MAX_PATH);

    std::wstring exeDir = exePath;
    size_t lastSlash = exeDir.find_last_of(L"\\/");
    if (lastSlash != std::wstring::npos) {
        exeDir = exeDir.substr(0, lastSlash);
    }

    std::vector<std::wstring> searchCandidates;

    // 1. 從執行檔目錄遞迴向上探索 5 層目錄
    std::wstring curDir = exeDir;
    for (int depth = 0; depth < 5; ++depth) {
        searchCandidates.push_back(curDir + L"\\design\\1x\\" + filename);
        searchCandidates.push_back(curDir + L"\\assets\\" + filename);
        searchCandidates.push_back(curDir + L"\\src\\ui\\assets\\" + filename);

        size_t s = curDir.find_last_of(L"\\/");
        if (s != std::wstring::npos) {
            curDir = curDir.substr(0, s);
        } else {
            break;
        }
    }

    // 2. 當前工作目錄相對路徑
    searchCandidates.push_back(L"design/1x/" + filename);
    searchCandidates.push_back(L"assets/" + filename);
    searchCandidates.push_back(L"../design/1x/" + filename);
    searchCandidates.push_back(L"../../design/1x/" + filename);
    searchCandidates.push_back(L"../../../design/1x/" + filename);

    for (const auto& path : searchCandidates) {
        auto img = std::make_unique<Gdiplus::Image>(path.c_str());
        if (img && img->GetLastStatus() == Gdiplus::Ok && img->GetWidth() > 0) {
            return img;
        }
    }
    return nullptr;
}

// 萬國碼 UTF-8 轉 UTF-16 wstring 工具函式 (避免字元截斷與亂碼)
std::wstring utf8ToWide(const std::string& utf8Str) {
    if (utf8Str.empty()) return std::wstring();
    int sizeNeeded = MultiByteToWideChar(CP_UTF8, 0, utf8Str.c_str(), static_cast<int>(utf8Str.size()), NULL, 0);
    if (sizeNeeded <= 0) return std::wstring();
    std::wstring result(static_cast<size_t>(sizeNeeded), 0);
    MultiByteToWideChar(CP_UTF8, 0, utf8Str.c_str(), static_cast<int>(utf8Str.size()), &result[0], sizeNeeded);
    return result;
}

} // anonymous namespace

NativeWelcomeWindow::NativeWelcomeWindow(int width, int height)
    : m_width(width), m_height(height), m_engine(PlatformType::Windows) {
    
    // 初始化 GDI+
    Gdiplus::GdiplusStartupInput gdiplusStartupInput;
    Gdiplus::GdiplusStartup(&m_gdiplusToken, &gdiplusStartupInput, NULL);

    // 載入設計資產 (資產 4 Logo, 資產 5 施測結束, 資產 6 填寫成功)
    m_logoImage = loadAssetImage(L"資產 4.png");
    if (!m_logoImage) m_logoImage = loadAssetImage(L"logo.png");
    m_asset5Image = loadAssetImage(L"資產 5.png");
    m_asset6Image = loadAssetImage(L"資產 6.png");

    // 初始化健康先驗遙測資料
    m_latestTelemetry.eyeMetrics.earAvg = 0.312f;
    m_latestTelemetry.eyeMetrics.perclos = 0.0f;
    m_latestTelemetry.eyeMetrics.blinkCount = 0;
    m_latestTelemetry.complexityMetrics.complexityIndex = 4.50f;
    m_latestTelemetry.systemState.currentFatigueScore = 5.0f;
    m_latestTelemetry.systemState.fatigueLevel = FatigueLevel::Relaxed;
    m_latestTelemetry.lifecycleSummary = "相機狀態: 正在連接前置攝影機...";
    m_latestTelemetry.currentStudyDay = m_engine.getStudyTracker().getCurrentDay();
    m_latestTelemetry.subjectUuid = m_engine.getStudyTracker().getSubjectUuid();
    m_latestTelemetry.studyStatus = m_engine.getStudyTracker().getStatus();

    // 綁定五執行緒引擎遙測事件
    m_engine.setTelemetryCallback([this](const EngineTelemetry& t) {
        this->m_latestTelemetry = t;
        if (this->m_hwnd && (this->m_currentStage == UIStage::MainDashboard || this->m_currentStage == UIStage::CalibrationInstruction)) {
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
    m_asset5Image.reset();
    m_asset6Image.reset();
    if (m_gdiplusToken) {
        Gdiplus::GdiplusShutdown(m_gdiplusToken);
    }
}

void NativeWelcomeWindow::handleMouseClick(int x, int y) {
    POINT pt = { x, y };

    if (m_currentStage == UIStage::Welcome) {
        if (PtInRect(&m_startBtnRect, pt)) {
            m_currentStage = UIStage::CalibrationInstruction;
            m_stageTimeSec = 0.0f;
            m_animTimeSec = 0.0f;
            m_engine.start(); // 啟動相機與分析管線
            if (m_hwnd) InvalidateRect(m_hwnd, NULL, FALSE);
        }
    } else if (m_currentStage == UIStage::CalibrationInstruction) {
        if (PtInRect(&m_readyBtnRect, pt)) {
            m_currentStage = UIStage::CountdownWait;
            m_stageTimeSec = 0.0f;
            if (m_hwnd) InvalidateRect(m_hwnd, NULL, FALSE);
        }
    } else if (m_currentStage == UIStage::MainDashboard) {
        if (PtInRect(&m_recalibBtnRect, pt)) {
            m_currentStage = UIStage::CalibrationInstruction;
            m_stageTimeSec = 0.0f;
            m_animTimeSec = 0.0f;
            if (m_hwnd) InvalidateRect(m_hwnd, NULL, FALSE);
        } else if (PtInRect(&m_endStudyBtnRect, pt)) {
            // 進入階段 6：施測結束門禁 (資產 5.png: 「施測結束，請填寫後測問卷並解除安裝系統」)
            m_engine.getStudyTracker().triggerPostStudyLock();
            m_currentStage = UIStage::StudyCompletedGate;
            m_stageTimeSec = 0.0f;
            if (m_hwnd) InvalidateRect(m_hwnd, NULL, FALSE);
        }
    } else if (m_currentStage == UIStage::StudyCompletedGate) {
        if (PtInRect(&m_fillQuestionnaireBtnRect, pt)) {
            // 提交後測問卷並獲取解鎖 Token -> 進入階段 7 (資產 6.png: 「填寫成功!感謝您協助施測」)
            m_engine.getStudyTracker().submitQuestionnaire("Study_Post_Survey_Completed");
            m_currentStage = UIStage::QuestionnaireSubmitted;
            m_stageTimeSec = 0.0f;
            if (m_hwnd) InvalidateRect(m_hwnd, NULL, FALSE);
        } else if (PtInRect(&m_returnDashboardBtnRect, pt)) {
            // 返回即時監控中心
            m_currentStage = UIStage::MainDashboard;
            if (m_hwnd) InvalidateRect(m_hwnd, NULL, FALSE);
        }
    } else if (m_currentStage == UIStage::QuestionnaireSubmitted) {
        if (PtInRect(&m_exitAppBtnRect, pt)) {
            // 完成並安全退出應用程式
            PostMessage(m_hwnd, WM_CLOSE, 0, 0);
        } else if (PtInRect(&m_returnDashboardBtnRect, pt)) {
            // 返回即時監控中心
            m_currentStage = UIStage::MainDashboard;
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
        m_demoDotY = 0.28f * std::sin(angle * 2.0f) + 0.5f;
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
        struct TargetPoint { float x, y; };
        const TargetPoint points[] = {
            { 0.50f, 0.50f }, // 0. 中心
            { 0.15f, 0.18f }, // 1. 左上
            { 0.85f, 0.18f }, // 2. 右上
            { 0.85f, 0.78f }, // 3. 右下
            { 0.15f, 0.78f }, // 4. 左下
            { 0.50f, 0.50f }  // 5. 回歸中心
        };

        float totalDuration = 5.0f;
        float segDuration = 1.0f;
        int seg = static_cast<int>(m_stageTimeSec / segDuration);

        if (seg < 5) {
            float segT = (m_stageTimeSec - static_cast<float>(seg) * segDuration) / segDuration;
            float smoothT = 0.5f * (1.0f - std::cos(segT * 3.14159265f));

            TargetPoint p0 = points[seg];
            TargetPoint p1 = points[seg + 1];

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
        int x = GET_X_LPARAM(lParam);
        int y = GET_Y_LPARAM(lParam);
        POINT pt = { x, y };

        bool inStart = (pThis->m_currentStage == UIStage::Welcome) && (PtInRect(&pThis->m_startBtnRect, pt) != FALSE);
        bool inReady = (pThis->m_currentStage == UIStage::CalibrationInstruction) && (PtInRect(&pThis->m_readyBtnRect, pt) != FALSE);
        bool inRecalib = (pThis->m_currentStage == UIStage::MainDashboard) && (PtInRect(&pThis->m_recalibBtnRect, pt) != FALSE);
        bool inEndStudy = (pThis->m_currentStage == UIStage::MainDashboard) && (PtInRect(&pThis->m_endStudyBtnRect, pt) != FALSE);
        bool inFillQ = (pThis->m_currentStage == UIStage::StudyCompletedGate) && (PtInRect(&pThis->m_fillQuestionnaireBtnRect, pt) != FALSE);
        bool inReturnDash = ((pThis->m_currentStage == UIStage::StudyCompletedGate || pThis->m_currentStage == UIStage::QuestionnaireSubmitted)) && (PtInRect(&pThis->m_returnDashboardBtnRect, pt) != FALSE);
        bool inExitApp = (pThis->m_currentStage == UIStage::QuestionnaireSubmitted) && (PtInRect(&pThis->m_exitAppBtnRect, pt) != FALSE);

        bool hasHoverChange = (inStart != pThis->m_isHoveringStartBtn || 
                               inReady != pThis->m_isHoveringReadyBtn || 
                               inRecalib != pThis->m_isHoveringRecalibBtn ||
                               inEndStudy != pThis->m_isHoveringEndStudyBtn ||
                               inFillQ != pThis->m_isHoveringFillQuestionnaireBtn ||
                               inReturnDash != pThis->m_isHoveringReturnDashboardBtn ||
                               inExitApp != pThis->m_isHoveringExitAppBtn);

        if (hasHoverChange) {
            pThis->m_isHoveringStartBtn = inStart;
            pThis->m_isHoveringReadyBtn = inReady;
            pThis->m_isHoveringRecalibBtn = inRecalib;
            pThis->m_isHoveringEndStudyBtn = inEndStudy;
            pThis->m_isHoveringFillQuestionnaireBtn = inFillQ;
            pThis->m_isHoveringReturnDashboardBtn = inReturnDash;
            pThis->m_isHoveringExitAppBtn = inExitApp;

            bool isAnyHovered = (inStart || inReady || inRecalib || inEndStudy || inFillQ || inReturnDash || inExitApp);
            SetCursor(LoadCursor(NULL, isAnyHovered ? IDC_HAND : IDC_ARROW));
            InvalidateRect(hwnd, NULL, FALSE);
        }

        TRACKMOUSEEVENT tme = { sizeof(TRACKMOUSEEVENT), TME_LEAVE, hwnd, 0 };
        TrackMouseEvent(&tme);
        return 0;
    }

    case WM_LBUTTONUP: {
        int x = GET_X_LPARAM(lParam);
        int y = GET_Y_LPARAM(lParam);
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

    if (width <= 0 || height <= 0) {
        EndPaint(hwnd, &ps);
        return;
    }

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
    case UIStage::StudyCompletedGate:
        drawStudyCompletedGate(g, width, height);
        break;
    case UIStage::QuestionnaireSubmitted:
        drawQuestionnaireSubmitted(g, width, height);
        break;
    }

    BitBlt(hdc, 0, 0, width, height, memDC, 0, 0, SRCCOPY);

    SelectObject(memDC, oldBitmap);
    DeleteObject(memBitmap);
    DeleteDC(memDC);

    EndPaint(hwnd, &ps);
}

// -----------------------------------------------------------------------------
// 階段 1：系統歡迎介面 (保持 Logo 100% 原圖長寬比 + 純白高對比徽章容器 + RWD 自適應)
// -----------------------------------------------------------------------------
void NativeWelcomeWindow::drawWelcomeScreen(Gdiplus::Graphics& g, int w, int h) {
    // 滿版薄荷綠背景 (#1EB18A)
    Gdiplus::SolidBrush bgBrush(Gdiplus::Color(255, 30, 177, 138));
    g.FillRectangle(&bgBrush, 0, 0, w, h);

    bool isNarrow = (w < 680 || h > w);

    // 1. 計算 Logo 100% 原始長寬比 (資產 4.png 原始比例為 70:42 = 1.667)
    float naturalAspect = 70.0f / 42.0f; // 1.6667
    int origImgW = 70;
    int origImgH = 42;

    if (m_logoImage && m_logoImage->GetLastStatus() == Gdiplus::Ok && m_logoImage->GetWidth() > 0 && m_logoImage->GetHeight() > 0) {
        origImgW = static_cast<int>(m_logoImage->GetWidth());
        origImgH = static_cast<int>(m_logoImage->GetHeight());
        naturalAspect = static_cast<float>(origImgW) / static_cast<float>(origImgH);
    }

    // 計算符合 100% 長寬比的最佳繪製尺寸 (不被壓扁或拉長)
    int maxTargetW = isNarrow ? std::clamp(w / 3, 100, 140) : 150;
    int drawLogoW = maxTargetW;
    int drawLogoH = static_cast<int>(maxTargetW / naturalAspect);

    // 純白立體圓形徽章容器 (包含足夠留白以襯托 Logo)
    int badgeDiameter = isNarrow ? std::clamp(w / 3 + 30, 120, 150) : 170;
    if (badgeDiameter < drawLogoW + 24) badgeDiameter = drawLogoW + 24;
    
    int badgeX = (w - badgeDiameter) / 2;
    int badgeY = isNarrow ? std::max(20, h / 2 - badgeDiameter - 45) : (h / 2 - 185);

    // 繪製純白圓形徽章 (#FFFFFF)
    Gdiplus::SolidBrush whiteBadge(Gdiplus::Color(255, 255, 255, 255));
    g.FillEllipse(&whiteBadge, badgeX, badgeY, badgeDiameter, badgeDiameter);

    // 外圈金黃裝飾邊框 (#F7E3AF)
    Gdiplus::Pen badgePen(Gdiplus::Color(200, 247, 227, 175), 2.5f);
    g.DrawEllipse(&badgePen, badgeX, badgeY, badgeDiameter, badgeDiameter);

    // 將 100% 原始長寬比的 Logo 置中繪製在純白徽章內
    int logoX = badgeX + (badgeDiameter - drawLogoW) / 2;
    int logoY = badgeY + (badgeDiameter - drawLogoH) / 2;

    if (m_logoImage && m_logoImage->GetLastStatus() == Gdiplus::Ok && m_logoImage->GetWidth() > 0) {
        g.DrawImage(m_logoImage.get(), logoX, logoY, drawLogoW, drawLogoH);
    } else {
        // 高精度向量備援標章 (以 100% 70:42 原始比例繪製綠色眼睛輪廓 + 瞳孔準心)
        int cx = badgeX + badgeDiameter / 2;
        int cy = badgeY + badgeDiameter / 2;
        int eyeW = drawLogoW;
        int eyeH = drawLogoH;
        
        Gdiplus::Pen greenPen(Gdiplus::Color(255, 30, 177, 138), 3.5f);
        Gdiplus::SolidBrush greenBrush(Gdiplus::Color(255, 30, 177, 138));
        
        // 眼睛外輪廓
        g.DrawEllipse(&greenPen, cx - eyeW / 2, cy - eyeH / 2, eyeW, eyeH);
        // 瞳孔外圈
        int irisR = static_cast<int>(eyeH * 0.82f);
        g.DrawEllipse(&greenPen, cx - irisR / 2, cy - irisR / 2, irisR, irisR);
        // 實心瞳孔
        int pupilR = static_cast<int>(eyeH * 0.42f);
        g.FillEllipse(&greenBrush, cx - pupilR / 2, cy - pupilR / 2, pupilR, pupilR);
        // 橫豎十字準心線
        g.DrawLine(&greenPen, cx - eyeW / 2, cy, cx + eyeW / 2, cy);
        g.DrawLine(&greenPen, cx, cy - eyeH / 2, cx, cy + eyeH / 2);
    }

    // 2. 標題文字: "感謝協助測試EFD" (純白 #FFFFFF, 響應式字體大小)
    Gdiplus::FontFamily fontFamily(L"Microsoft JhengHei");
    int titleFontSize = isNarrow ? std::clamp(w / 18, 18, 26) : 32;
    Gdiplus::Font titleFont(&fontFamily, static_cast<Gdiplus::REAL>(titleFontSize), Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
    Gdiplus::SolidBrush textBrush(Gdiplus::Color(255, 255, 255, 255));

    Gdiplus::StringFormat format;
    format.SetAlignment(Gdiplus::StringAlignmentCenter);
    format.SetLineAlignment(Gdiplus::StringAlignmentCenter);

    float titleY = static_cast<float>(badgeY + badgeDiameter + (isNarrow ? 16 : 26));
    Gdiplus::RectF titleRect(0.0f, titleY, static_cast<float>(w), 45.0f);
    g.DrawString(L"感謝協助測試EFD", -1, &titleFont, titleRect, &format, &textBrush);

    // 3. 進入測試說明按鈕 (響應式尺寸)
    int btnWidth = isNarrow ? std::clamp(w - 80, 180, 240) : 220;
    int btnHeight = isNarrow ? 46 : 52;
    int btnX = (w - btnWidth) / 2;
    int btnY = isNarrow ? static_cast<int>(titleY + 65) : std::max(h / 2 + 65, static_cast<int>(titleY + 58));
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
    int btnFontSize = isNarrow ? 16 : 18;
    Gdiplus::Font btnFont(&fontFamily, static_cast<Gdiplus::REAL>(btnFontSize), Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
    Gdiplus::SolidBrush btnTextBrush(Gdiplus::Color(255, 30, 177, 138));
    Gdiplus::RectF btnTextRect(static_cast<float>(btnX), static_cast<float>(btnY), static_cast<float>(btnWidth), static_cast<float>(btnHeight));
    g.DrawString(L"進入測試說明", -1, &btnFont, btnTextRect, &format, &btnTextBrush);
}

// -----------------------------------------------------------------------------
// 階段 2：說明與預覽演示介面 (Flex 效果 RWD + 即時相機硬體連線狀態條)
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

    bool isNarrow = (w < 720 || (static_cast<float>(h) / static_cast<float>(w)) > 0.95f);

    // 1. 頂部大標題
    int titleFontSize = isNarrow ? std::clamp(w / 22, 16, 22) : 26;
    Gdiplus::Font titleFont(&fontFamily, static_cast<Gdiplus::REAL>(titleFontSize), Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
    Gdiplus::SolidBrush whiteBrush(Gdiplus::Color(255, 255, 255, 255));
    float titleY = isNarrow ? 8.0f : 16.0f;
    Gdiplus::RectF titleRect(0.0f, titleY, static_cast<float>(w), 32.0f);
    g.DrawString(L"眼動特徵提取與校準說明", -1, &titleFont, titleRect, &centerFormat, &whiteBrush);

    // 2. 即時相機連線狀態標籤 (Camera Hardware Status Bar)
    std::string camStatusStr = m_latestTelemetry.lifecycleSummary;
    if (camStatusStr.empty()) {
        camStatusStr = "相機狀態: 正在連接前置鏡頭...";
    }
    std::wstring wCamStatus = utf8ToWide(camStatusStr);
    Gdiplus::Font camStatusFont(&fontFamily, 11, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
    Gdiplus::SolidBrush camStatusBrush(Gdiplus::Color(255, 247, 227, 175)); // 金黃
    Gdiplus::RectF camStatusRect(0.0f, titleY + 32.0f, static_cast<float>(w), 18.0f);
    g.DrawString((L"● " + wCamStatus).c_str(), -1, &camStatusFont, camStatusRect, &centerFormat, &camStatusBrush);

    int btnW = isNarrow ? std::clamp(w - 60, 180, 260) : 260;
    int btnH = isNarrow ? 40 : 46;
    int btnX = (w - btnW) / 2;
    int btnY = h - (isNarrow ? 50 : 64);
    m_readyBtnRect = { btnX, btnY, btnX + btnW, btnY + btnH };

    if (!isNarrow) {
        // ==========================================
        // 桌面/寬螢幕：水平橫向排列 (Flex Row)
        // ==========================================
        int boxW = (w >= 840) ? 380 : (w - 100) / 2;
        int boxH = std::clamp(h - 195, 190, 250);
        int guideW = (w >= 840) ? 340 : boxW;
        int totalW = boxW + guideW + 30;
        int boxX = (w - totalW) / 2;
        int boxY = static_cast<int>(titleY + 54);
        int guideX = boxX + boxW + 30;
        int guideY = boxY;

        // 預覽框背景與邊框
        Gdiplus::SolidBrush boxBg(Gdiplus::Color(255, 37, 41, 28));
        g.FillRectangle(&boxBg, boxX, boxY, boxW, boxH);
        Gdiplus::Pen boxPen(Gdiplus::Color(255, 150, 197, 247), 2.0f);
        g.DrawRectangle(&boxPen, boxX, boxY, boxW, boxH);

        // 標籤
        Gdiplus::Font tagFont(&fontFamily, 12, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
        Gdiplus::SolidBrush tagBrush(Gdiplus::Color(255, 247, 227, 175));
        Gdiplus::RectF tagRect(static_cast<float>(boxX + 10), static_cast<float>(boxY + 8), static_cast<float>(boxW - 20), 20.0f);
        g.DrawString(L"▶ 測試動態路徑演示 (DEMO 預覽)", -1, &tagFont, tagRect, &leftFormat, &tagBrush);

        // 演示小黃點
        int demoDotX = boxX + static_cast<int>(m_demoDotX * boxW);
        int demoDotY = boxY + static_cast<int>(m_demoDotY * boxH);
        Gdiplus::SolidBrush demoYellowDot(Gdiplus::Color(255, 247, 227, 175));
        g.FillEllipse(&demoYellowDot, demoDotX - 12, demoDotY - 12, 24, 24);
        Gdiplus::Pen demoPulsePen(Gdiplus::Color(120, 247, 227, 175), 1.5f);
        g.DrawEllipse(&demoPulsePen, demoDotX - 18, demoDotY - 18, 36, 36);

        // 右側 3 大指引卡片
        auto drawGuideItem = [&](int idx, const wchar_t* title, const wchar_t* desc) {
            int itemY = guideY + idx * (boxH / 3 + 2);
            int cardH = boxH / 3 - 8;
            Gdiplus::SolidBrush cardBg(Gdiplus::Color(180, 20, 140, 108));
            g.FillRectangle(&cardBg, guideX, itemY, guideW, cardH);

            Gdiplus::Font hFont(&fontFamily, 15, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
            Gdiplus::SolidBrush hBrush(Gdiplus::Color(255, 247, 227, 175));
            Gdiplus::RectF hRect(static_cast<float>(guideX + 12), static_cast<float>(itemY + 6), static_cast<float>(guideW - 24), 22.0f);
            g.DrawString(title, -1, &hFont, hRect, &leftFormat, &hBrush);

            Gdiplus::Font dFont(&fontFamily, 12, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
            Gdiplus::SolidBrush dBrush(Gdiplus::Color(255, 255, 255, 255));
            Gdiplus::RectF dRect(static_cast<float>(guideX + 12), static_cast<float>(itemY + 28), static_cast<float>(guideW - 24), static_cast<float>(cardH - 30));
            g.DrawString(desc, -1, &dFont, dRect, &leftFormat, &dBrush);
        };

        drawGuideItem(0, L"1. 臉部正面對齊鏡頭", L"保持端正坐姿，確保鏡頭能清晰捕捉面部特徵。");
        drawGuideItem(1, L"2. 視線跟隨黃點移動", L"測試開始後，請專注凝視黃點並跟隨其移動。");
        drawGuideItem(2, L"3. 保持自然睜眼狀態", L"校準過程僅需 5 秒鐘，請保持自然眨眼與專注。");

    } else {
        // ==========================================
        // 手機/窄螢幕：垂直向下排列 (Flex Column)
        // ==========================================
        int boxW = std::min(w - 30, 360);
        int boxH = std::clamp(h / 4, 105, 145);
        int boxX = (w - boxW) / 2;
        int boxY = static_cast<int>(titleY + 52);

        // 預覽框背景與邊框
        Gdiplus::SolidBrush boxBg(Gdiplus::Color(255, 37, 41, 28));
        g.FillRectangle(&boxBg, boxX, boxY, boxW, boxH);
        Gdiplus::Pen boxPen(Gdiplus::Color(255, 150, 197, 247), 1.5f);
        g.DrawRectangle(&boxPen, boxX, boxY, boxW, boxH);

        // 標籤
        Gdiplus::Font tagFont(&fontFamily, 10, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
        Gdiplus::SolidBrush tagBrush(Gdiplus::Color(255, 247, 227, 175));
        Gdiplus::RectF tagRect(static_cast<float>(boxX + 8), static_cast<float>(boxY + 4), static_cast<float>(boxW - 16), 16.0f);
        g.DrawString(L"▶ 測試動態路徑演示 (DEMO 預覽)", -1, &tagFont, tagRect, &leftFormat, &tagBrush);

        // 演示小黃點
        int demoDotX = boxX + static_cast<int>(m_demoDotX * boxW);
        int demoDotY = boxY + static_cast<int>(m_demoDotY * boxH);
        Gdiplus::SolidBrush demoYellowDot(Gdiplus::Color(255, 247, 227, 175));
        g.FillEllipse(&demoYellowDot, demoDotX - 8, demoDotY - 8, 16, 16);

        // 下方垂直堆疊 3 大指引卡片
        int guideX = boxX;
        int guideW = boxW;
        int startY = boxY + boxH + 8;
        int availH = btnY - startY - 8;
        int cardH = std::clamp(availH / 3 - 6, 36, 50);
        int gap = 6;

        auto drawMobileGuideItem = [&](int idx, const wchar_t* title, const wchar_t* desc) {
            int itemY = startY + idx * (cardH + gap);
            Gdiplus::SolidBrush cardBg(Gdiplus::Color(180, 20, 140, 108));
            g.FillRectangle(&cardBg, guideX, itemY, guideW, cardH);

            Gdiplus::Font hFont(&fontFamily, 12, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
            Gdiplus::SolidBrush hBrush(Gdiplus::Color(255, 247, 227, 175));
            Gdiplus::RectF hRect(static_cast<float>(guideX + 8), static_cast<float>(itemY + 3), static_cast<float>(guideW - 16), 16.0f);
            g.DrawString(title, -1, &hFont, hRect, &leftFormat, &hBrush);

            if (cardH >= 42) {
                Gdiplus::Font dFont(&fontFamily, 10, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
                Gdiplus::SolidBrush dBrush(Gdiplus::Color(255, 255, 255, 255));
                Gdiplus::RectF dRect(static_cast<float>(guideX + 8), static_cast<float>(itemY + 19), static_cast<float>(guideW - 16), static_cast<float>(cardH - 21));
                g.DrawString(desc, -1, &dFont, dRect, &leftFormat, &dBrush);
            }
        };

        drawMobileGuideItem(0, L"1. 臉部正面對齊鏡頭", L"保持端正坐姿，確保鏡頭捕捉面部特徵。");
        drawMobileGuideItem(1, L"2. 視線跟隨黃點移動", L"測試開始後，請專注凝視黃點並隨之移動。");
        drawMobileGuideItem(2, L"3. 保持自然睜眼狀態", L"校準過程僅需 5 秒鐘，請保持自然眨眼。");
    }

    // 底部準備完成按鈕
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

    int readyFontSize = isNarrow ? 15 : 17;
    Gdiplus::Font btnFont(&fontFamily, static_cast<Gdiplus::REAL>(readyFontSize), Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
    Gdiplus::SolidBrush btnTextBrush(Gdiplus::Color(255, 30, 177, 138));
    Gdiplus::RectF btnTextRect(static_cast<float>(btnX), static_cast<float>(btnY), static_cast<float>(btnW), static_cast<float>(btnH));
    g.DrawString(L"我準備好了，開始校準", -1, &btnFont, btnTextRect, &centerFormat, &btnTextBrush);
}

// -----------------------------------------------------------------------------
// 階段 3：全新 3 秒倒數計時等待介面 (RWD 響應式縮放)
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
    int circleRadius = static_cast<int>(std::clamp(std::min(w, h) / 7, 45, 75) * pulse);
    int centerY = h / 2 - 25;
    Gdiplus::SolidBrush circleBg(Gdiplus::Color(200, 247, 227, 175));
    g.FillEllipse(&circleBg, w / 2 - circleRadius, centerY - circleRadius, circleRadius * 2, circleRadius * 2);

    // 倒數大數字
    int numFontSize = static_cast<int>(circleRadius * 0.9f);
    Gdiplus::Font numFont(&fontFamily, static_cast<Gdiplus::REAL>(numFontSize), Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
    Gdiplus::SolidBrush numBrush(Gdiplus::Color(255, 30, 177, 138));
    Gdiplus::RectF numRect(static_cast<float>(w / 2 - circleRadius), static_cast<float>(centerY - circleRadius), static_cast<float>(circleRadius * 2), static_cast<float>(circleRadius * 2));
    g.DrawString(countStr.c_str(), -1, &numFont, numRect, &centerFormat, &numBrush);

    // 提示文字
    int hintFontSize = std::clamp(w / 28, 14, 20);
    Gdiplus::Font hintFont(&fontFamily, static_cast<Gdiplus::REAL>(hintFontSize), Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
    Gdiplus::SolidBrush whiteBrush(Gdiplus::Color(255, 255, 255, 255));
    Gdiplus::RectF hintRect(0.0f, static_cast<float>(centerY + circleRadius + 15), static_cast<float>(w), 35.0f);
    g.DrawString(L"請做好準備，即將開始眼動追蹤校準...", -1, &hintFont, hintRect, &centerFormat, &whiteBrush);
}

// -----------------------------------------------------------------------------
// 階段 4：實際多點動態眼動特徵提取介面 (RWD 響應式錨點)
// -----------------------------------------------------------------------------
void NativeWelcomeWindow::drawActiveCalibration(Gdiplus::Graphics& g, int w, int h) {
    Gdiplus::SolidBrush bgBrush(Gdiplus::Color(255, 30, 177, 138));
    g.FillRectangle(&bgBrush, 0, 0, w, h);

    // 繪製全螢幕動態移動的大黃點
    int dotDiameter = std::clamp(std::min(w, h) / 14, 34, 52);
    int posX = static_cast<int>(m_targetDotX * static_cast<float>(w)) - dotDiameter / 2;
    int posY = static_cast<int>(m_targetDotY * static_cast<float>(h)) - dotDiameter / 2;

    Gdiplus::SolidBrush yellowDotBrush(Gdiplus::Color(255, 247, 227, 175));
    g.FillEllipse(&yellowDotBrush, posX, posY, dotDiameter, dotDiameter);

    // 外圈光暈
    Gdiplus::Pen haloPen(Gdiplus::Color(140, 247, 227, 175), 2.5f);
    g.DrawEllipse(&haloPen, posX - 6, posY - 6, dotDiameter + 12, dotDiameter + 12);

    // 底部即時進度條與文字
    Gdiplus::FontFamily fontFamily(L"Microsoft JhengHei");
    int progressFontSize = std::clamp(w / 30, 14, 20);
    Gdiplus::Font progressFont(&fontFamily, static_cast<Gdiplus::REAL>(progressFontSize), Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
    Gdiplus::SolidBrush textBrush(Gdiplus::Color(255, 255, 255, 255));

    Gdiplus::StringFormat centerFormat;
    centerFormat.SetAlignment(Gdiplus::StringAlignmentCenter);
    centerFormat.SetLineAlignment(Gdiplus::StringAlignmentCenter);

    int pct = static_cast<int>(m_calibrationProgress * 100.0f);
    std::wstring pStr = L"眼動特徵多角度提取中... (" + std::to_wstring(pct) + L"%)";
    Gdiplus::RectF progressRect(0.0f, static_cast<float>(h - 68), static_cast<float>(w), 30.0f);
    g.DrawString(pStr.c_str(), -1, &progressFont, progressRect, &centerFormat, &textBrush);

    // 進度條本體
    int barW = std::clamp(w - 60, 180, 320);
    int barH = 7;
    int barX = (w - barW) / 2;
    int barY = h - 30;
    Gdiplus::SolidBrush barBg(Gdiplus::Color(100, 255, 255, 255));
    g.FillRectangle(&barBg, barX, barY, barW, barH);
    Gdiplus::SolidBrush barFill(Gdiplus::Color(255, 247, 227, 175));
    g.FillRectangle(&barFill, barX, barY, static_cast<int>(barW * m_calibrationProgress), barH);
}

// -----------------------------------------------------------------------------
// 階段 5：即時疲勞監控中心 (即時跳動數據 + 相機硬體狀態條 + 2x2 RWD 網格)
// -----------------------------------------------------------------------------
void NativeWelcomeWindow::drawMainDashboard(Gdiplus::Graphics& g, int w, int h) {
    // 深黑背景 (#25291C)
    Gdiplus::SolidBrush bgBrush(Gdiplus::Color(255, 37, 41, 28));
    g.FillRectangle(&bgBrush, 0, 0, w, h);

    Gdiplus::FontFamily fontFamily(L"Microsoft JhengHei");
    Gdiplus::StringFormat centerFormat;
    centerFormat.SetAlignment(Gdiplus::StringAlignmentCenter);
    centerFormat.SetLineAlignment(Gdiplus::StringAlignmentCenter);

    bool isNarrow = (w < 700 || h > w);

    // 1. 頂部標題
    int headerFontSize = isNarrow ? std::clamp(w / 25, 17, 22) : 24;
    Gdiplus::Font headerFont(&fontFamily, static_cast<Gdiplus::REAL>(headerFontSize), Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
    Gdiplus::SolidBrush whiteBrush(Gdiplus::Color(255, 255, 255, 255));
    float headerY = isNarrow ? 8.0f : 15.0f;
    Gdiplus::RectF headerRect(0.0f, headerY, static_cast<float>(w), 30.0f);
    g.DrawString(L"EFD 即時眼睛疲勞監控中心", -1, &headerFont, headerRect, &centerFormat, &whiteBrush);

    // 2. 即時相機連線狀態指示條 (Hardware Status Banner)
    std::string camStatusStr = m_latestTelemetry.lifecycleSummary;
    if (camStatusStr.empty()) {
        camStatusStr = "相機狀態: 運作中 (30 FPS)";
    }
    std::wstring wCamStatus = utf8ToWide(camStatusStr);
    Gdiplus::Font camStatusFont(&fontFamily, 11, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
    Gdiplus::SolidBrush camStatusBrush(Gdiplus::Color(255, 150, 197, 247)); // 科技藍
    Gdiplus::RectF camStatusRect(0.0f, headerY + 28.0f, static_cast<float>(w), 18.0f);
    g.DrawString((L"● " + wCamStatus).c_str(), -1, &camStatusFont, camStatusRect, &centerFormat, &camStatusBrush);

    // 3. 核心狀態大卡片
    int cardW = isNarrow ? (w - 24) : (w - 100);
    int cardH = isNarrow ? 65 : 95;
    int cardX = (w - cardW) / 2;
    int cardY = static_cast<int>(headerY + 50);

    Gdiplus::Color statusColor = Gdiplus::Color(255, 30, 177, 138); // 正常綠
    const wchar_t* statusText = L"生理狀態：正常清醒 (Relaxed)";

    if (m_latestTelemetry.systemState.fatigueLevel == FatigueLevel::Attention) {
        statusColor = Gdiplus::Color(255, 247, 227, 175); // 注意黃
        statusText = L"生理狀態：輕度用眼疲勞 (Attention)";
    } else if (m_latestTelemetry.systemState.fatigueLevel == FatigueLevel::SevereWarning) {
        statusColor = Gdiplus::Color(255, 235, 87, 87); // 警告紅
        statusText = L"生理狀態：重度疲勞！建議休息 (Severe)";
    }

    Gdiplus::SolidBrush cardBrush(statusColor);
    g.FillRectangle(&cardBrush, cardX, cardY, cardW, cardH);

    int cardFontSize = isNarrow ? 15 : 20;
    Gdiplus::Font cardFont(&fontFamily, static_cast<Gdiplus::REAL>(cardFontSize), Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
    Gdiplus::SolidBrush cardTextBrush(Gdiplus::Color(255, 37, 41, 28));
    Gdiplus::RectF cardTextRect(static_cast<float>(cardX), static_cast<float>(cardY), static_cast<float>(cardW), static_cast<float>(cardH));
    g.DrawString(statusText, -1, &cardFont, cardTextRect, &centerFormat, &cardTextBrush);

    // 4. 即時遙測數據面板 (4 欄動態跳動數值)
    wchar_t b1[32], b2[32], b3[32], b4[32];
    swprintf_s(b1, 32, L"%.3f", static_cast<double>(m_latestTelemetry.eyeMetrics.earAvg));
    swprintf_s(b2, 32, L"%.1f%%", static_cast<double>(m_latestTelemetry.eyeMetrics.perclos * 100.0f));
    swprintf_s(b3, 32, L"%.2f", static_cast<double>(m_latestTelemetry.complexityMetrics.complexityIndex));
    swprintf_s(b4, 32, L"%.1f", static_cast<double>(m_latestTelemetry.systemState.currentFatigueScore));

    auto drawMetricCard = [&](int ix, int iy, int iw, int ih, const wchar_t* label, const wchar_t* val) {
        Gdiplus::SolidBrush boxBrush(Gdiplus::Color(255, 50, 56, 38));
        g.FillRectangle(&boxBrush, ix, iy, iw, ih);

        int lFontSize = isNarrow ? 11 : 13;
        Gdiplus::Font lFont(&fontFamily, static_cast<Gdiplus::REAL>(lFontSize), Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
        Gdiplus::SolidBrush lBrush(Gdiplus::Color(255, 180, 180, 180));
        Gdiplus::RectF lRect(static_cast<float>(ix), static_cast<float>(iy + 8), static_cast<float>(iw), 18.0f);
        g.DrawString(label, -1, &lFont, lRect, &centerFormat, &lBrush);

        int vFontSize = isNarrow ? 18 : 22;
        Gdiplus::Font vFont(&fontFamily, static_cast<Gdiplus::REAL>(vFontSize), Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
        Gdiplus::SolidBrush vBrush(Gdiplus::Color(255, 150, 197, 247)); // 科技藍 (#96C5F7)
        Gdiplus::RectF vRect(static_cast<float>(ix), static_cast<float>(iy + (isNarrow ? 28 : 36)), static_cast<float>(iw), 30.0f);
        g.DrawString(val, -1, &vFont, vRect, &centerFormat, &vBrush);
    };

    if (!isNarrow) {
        // 寬螢幕：1x4 橫向網格
        int gridY = cardY + cardH + 16;
        int gap = 12;
        int itemW = (cardW - gap * 3) / 4;
        int itemH = std::clamp(h - gridY - 80, 80, 105);

        drawMetricCard(cardX + 0 * (itemW + gap), gridY, itemW, itemH, L"雙眼 EAR", b1);
        drawMetricCard(cardX + 1 * (itemW + gap), gridY, itemW, itemH, L"PERCLOS 閉眼比", b2);
        drawMetricCard(cardX + 2 * (itemW + gap), gridY, itemW, itemH, L"複雜度 (MSE)", b3);
        drawMetricCard(cardX + 3 * (itemW + gap), gridY, itemW, itemH, L"綜合疲勞分數", b4);
    } else {
        // 手機/窄螢幕：2x2 彈性網格
        int gridY = cardY + cardH + 10;
        int gapX = 10;
        int gapY = 8;
        int itemW = (cardW - gapX) / 2;
        int itemH = std::clamp((h - gridY - 70) / 2, 50, 72);

        // Row 0
        drawMetricCard(cardX, gridY, itemW, itemH, L"雙眼 EAR", b1);
        drawMetricCard(cardX + itemW + gapX, gridY, itemW, itemH, L"PERCLOS 閉眼比", b2);

        // Row 1
        drawMetricCard(cardX, gridY + itemH + gapY, itemW, itemH, L"複雜度 (MSE)", b3);
        drawMetricCard(cardX + itemW + gapX, gridY + itemH + gapY, itemW, itemH, L"綜合疲勞分數", b4);
    }

    // 5. 底部雙按鈕 (RWD 響應式佈局：重新校準 + 結束施測/14天門禁)
    int btnH = isNarrow ? 36 : 42;
    int btnY = h - (isNarrow ? 46 : 56);
    int totalBtnsW = isNarrow ? std::min(w - 30, 380) : 420;
    int singleBtnW = (totalBtnsW - 14) / 2;
    int btn1X = (w - totalBtnsW) / 2;
    int btn2X = btn1X + singleBtnW + 14;

    m_recalibBtnRect = { btn1X, btnY, btn1X + singleBtnW, btnY + btnH };
    m_endStudyBtnRect = { btn2X, btnY, btn2X + singleBtnW, btnY + btnH };

    // 按鈕 1: 重新校準基準 (薄荷綠)
    Gdiplus::SolidBrush recBtnBrush(m_isHoveringRecalibBtn ? Gdiplus::Color(255, 45, 185, 145) : Gdiplus::Color(255, 30, 177, 138));
    g.FillRectangle(&recBtnBrush, btn1X, btnY, singleBtnW, btnH);

    int recalibFontSize = isNarrow ? 12 : 14;
    Gdiplus::Font btnFont(&fontFamily, static_cast<Gdiplus::REAL>(recalibFontSize), Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
    Gdiplus::RectF btn1TextRect(static_cast<float>(btn1X), static_cast<float>(btnY), static_cast<float>(singleBtnW), static_cast<float>(btnH));
    g.DrawString(L"重新校準基準", -1, &btnFont, btn1TextRect, &centerFormat, &whiteBrush);

    // 按鈕 2: 結束施測 / 14天門禁 (金黃/科研門禁色)
    Gdiplus::SolidBrush endBtnBrush(m_isHoveringEndStudyBtn ? Gdiplus::Color(255, 255, 235, 190) : Gdiplus::Color(255, 247, 227, 175));
    g.FillRectangle(&endBtnBrush, btn2X, btnY, singleBtnW, btnH);

    Gdiplus::SolidBrush endTextBrush(Gdiplus::Color(255, 37, 41, 28));
    Gdiplus::RectF btn2TextRect(static_cast<float>(btn2X), static_cast<float>(btnY), static_cast<float>(singleBtnW), static_cast<float>(btnH));
    g.DrawString(L"結束施測 (14天門禁)", -1, &btnFont, btn2TextRect, &centerFormat, &endTextBrush);
}

// -----------------------------------------------------------------------------
// 階段 6：施測結束門禁介面 (資產 5.png: 「施測結束，請填寫後測問卷並解除安裝系統」)
// -----------------------------------------------------------------------------
void NativeWelcomeWindow::drawStudyCompletedGate(Gdiplus::Graphics& g, int w, int h) {
    // 滿版薄荷綠背景 (#1EB18A)
    Gdiplus::SolidBrush bgBrush(Gdiplus::Color(255, 30, 177, 138));
    g.FillRectangle(&bgBrush, 0, 0, w, h);

    Gdiplus::FontFamily fontFamily(L"Microsoft JhengHei");
    Gdiplus::StringFormat centerFormat;
    centerFormat.SetAlignment(Gdiplus::StringAlignmentCenter);
    centerFormat.SetLineAlignment(Gdiplus::StringAlignmentCenter);

    Gdiplus::SolidBrush whiteBrush(Gdiplus::Color(255, 255, 255, 255));
    bool isNarrow = (w < 680 || h > w);

    // 1. 滿版高對比向量大標題 (100% 還原資產 5.png 設計，ClearType 點對點無損清晰)
    float titleY = isNarrow ? 22.0f : static_cast<float>(h / 2 - 165);
    if (titleY < 16.0f) titleY = 16.0f;
    int titleFontSize = isNarrow ? std::clamp(w / 18, 16, 22) : 26;
    Gdiplus::Font titleFont(&fontFamily, static_cast<Gdiplus::REAL>(titleFontSize), Gdiplus::FontStyleBold, Gdiplus::UnitPixel);

    Gdiplus::RectF titleRect(10.0f, titleY, static_cast<float>(w - 20), 45.0f);
    g.DrawString(L"施測結束，請填寫後測問卷並解除安裝系統", -1, &titleFont, titleRect, &centerFormat, &whiteBrush);
    titleY += (isNarrow ? 40.0f : 48.0f);

    // 2. 受試者科研狀態與數據封存卡片
    std::string uuidStr = m_engine.getStudyTracker().getSubjectUuid();
    std::wstring wUuid = utf8ToWide(uuidStr);

    int cardW = isNarrow ? std::min(w - 30, 420) : 520;
    int cardH = isNarrow ? 96 : 116;
    int cardX = (w - cardW) / 2;
    int cardY = static_cast<int>(titleY + (isNarrow ? 8 : 16));

    Gdiplus::SolidBrush cardBg(Gdiplus::Color(180, 20, 140, 108));
    g.FillRectangle(&cardBg, cardX, cardY, cardW, cardH);
    Gdiplus::Pen cardBorder(Gdiplus::Color(220, 247, 227, 175), 1.5f);
    g.DrawRectangle(&cardBorder, cardX, cardY, cardW, cardH);

    Gdiplus::Font subFont(&fontFamily, static_cast<Gdiplus::REAL>(isNarrow ? 11 : 13), Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
    Gdiplus::SolidBrush goldBrush(Gdiplus::Color(255, 247, 227, 175));
    Gdiplus::RectF subRect(static_cast<float>(cardX + 10), static_cast<float>(cardY + 8), static_cast<float>(cardW - 20), 22.0f);
    g.DrawString((L"受試者匿名代碼: " + wUuid + L" (14 天時序已安全封存)").c_str(), -1, &subFont, subRect, &centerFormat, &goldBrush);

    Gdiplus::Font descFont(&fontFamily, static_cast<Gdiplus::REAL>(isNarrow ? 10 : 12), Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
    Gdiplus::RectF descRect(static_cast<float>(cardX + 14), static_cast<float>(cardY + 32), static_cast<float>(cardW - 28), static_cast<float>(cardH - 38));
    g.DrawString(L"✓ 雙眼特徵時序記錄已完成\n✓ 離線資料庫落盤校驗通過\n✓ 請點擊下方按鈕填寫 3 題後測問卷以完成實驗流程", -1, &descFont, descRect, &centerFormat, &whiteBrush);

    // 3. 動作按鈕群組 (主要：填寫問卷 / 次要：返回監控中心)
    int btnH = isNarrow ? 44 : 50;
    int btnW = isNarrow ? std::min(w - 50, 320) : 320;
    int btnX = (w - btnW) / 2;
    int btnY = cardY + cardH + (isNarrow ? 16 : 24);
    m_fillQuestionnaireBtnRect = { btnX, btnY, btnX + btnW, btnY + btnH };

    Gdiplus::Color fillBtnColor = m_isHoveringFillQuestionnaireBtn ? Gdiplus::Color(255, 245, 245, 245) : Gdiplus::Color(255, 255, 255, 255);
    Gdiplus::SolidBrush fillBtnBrush(fillBtnColor);

    Gdiplus::GraphicsPath path;
    int r = 16;
    path.AddArc(btnX, btnY, r, r, 180, 90);
    path.AddArc(btnX + btnW - r, btnY, r, r, 270, 90);
    path.AddArc(btnX + btnW - r, btnY + btnH - r, r, r, 0, 90);
    path.AddArc(btnX, btnY + btnH - r, r, r, 90, 90);
    path.CloseFigure();
    g.FillPath(&fillBtnBrush, &path);

    Gdiplus::Font btnFont(&fontFamily, static_cast<Gdiplus::REAL>(isNarrow ? 15 : 17), Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
    Gdiplus::SolidBrush btnTextBrush(Gdiplus::Color(255, 30, 177, 138));
    Gdiplus::RectF btnTextRect(static_cast<float>(btnX), static_cast<float>(btnY), static_cast<float>(btnW), static_cast<float>(btnH));
    g.DrawString(L"前往填寫後測問卷並提交", -1, &btnFont, btnTextRect, &centerFormat, &btnTextBrush);

    // 次要按鈕：返回監控中心
    int retBtnH = 30;
    int retBtnW = 160;
    int retBtnX = (w - retBtnW) / 2;
    int retBtnY = btnY + btnH + 10;
    m_returnDashboardBtnRect = { retBtnX, retBtnY, retBtnX + retBtnW, retBtnY + retBtnH };

    Gdiplus::Font retFont(&fontFamily, 11, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
    Gdiplus::SolidBrush retBrush(m_isHoveringReturnDashboardBtn ? Gdiplus::Color(255, 247, 227, 175) : Gdiplus::Color(200, 255, 255, 255));
    Gdiplus::RectF retRect(static_cast<float>(retBtnX), static_cast<float>(retBtnY), static_cast<float>(retBtnW), static_cast<float>(retBtnH));
    g.DrawString(L"◀ 返回即時監控中心", -1, &retFont, retRect, &centerFormat, &retBrush);
}

// -----------------------------------------------------------------------------
// 階段 7：後測問卷填寫完成介面 (資產 6.png: 「填寫成功!感謝您協助施測」)
// -----------------------------------------------------------------------------
void NativeWelcomeWindow::drawQuestionnaireSubmitted(Gdiplus::Graphics& g, int w, int h) {
    // 滿版薄荷綠背景 (#1EB18A)
    Gdiplus::SolidBrush bgBrush(Gdiplus::Color(255, 30, 177, 138));
    g.FillRectangle(&bgBrush, 0, 0, w, h);

    Gdiplus::FontFamily fontFamily(L"Microsoft JhengHei");
    Gdiplus::StringFormat centerFormat;
    centerFormat.SetAlignment(Gdiplus::StringAlignmentCenter);
    centerFormat.SetLineAlignment(Gdiplus::StringAlignmentCenter);

    Gdiplus::SolidBrush whiteBrush(Gdiplus::Color(255, 255, 255, 255));
    bool isNarrow = (w < 680 || h > w);

    // 1. 滿版高對比向量大標題 (100% 還原資產 6.png 設計，ClearType 點對點無損清晰)
    float titleY = isNarrow ? 22.0f : static_cast<float>(h / 2 - 165);
    if (titleY < 16.0f) titleY = 16.0f;
    int titleFontSize = isNarrow ? std::clamp(w / 18, 18, 24) : 28;
    Gdiplus::Font titleFont(&fontFamily, static_cast<Gdiplus::REAL>(titleFontSize), Gdiplus::FontStyleBold, Gdiplus::UnitPixel);

    Gdiplus::RectF titleRect(10.0f, titleY, static_cast<float>(w - 20), 45.0f);
    g.DrawString(L"填寫成功!感謝您協助施測", -1, &titleFont, titleRect, &centerFormat, &whiteBrush);
    titleY += (isNarrow ? 40.0f : 48.0f);

    // 2. 解鎖授權碼與解除安裝指引卡片
    std::string tokenStr = m_engine.getStudyTracker().getUnlockToken();
    if (tokenStr.empty()) tokenStr = "EFD-14D-8821-4903";
    std::wstring wToken = utf8ToWide(tokenStr);

    int cardW = isNarrow ? std::min(w - 30, 420) : 520;
    int cardH = isNarrow ? 104 : 124;
    int cardX = (w - cardW) / 2;
    int cardY = static_cast<int>(titleY + (isNarrow ? 8 : 16));

    Gdiplus::SolidBrush cardBg(Gdiplus::Color(180, 20, 140, 108));
    g.FillRectangle(&cardBg, cardX, cardY, cardW, cardH);
    Gdiplus::Pen cardBorder(Gdiplus::Color(220, 247, 227, 175), 1.5f);
    g.DrawRectangle(&cardBorder, cardX, cardY, cardW, cardH);

    Gdiplus::Font tokenFont(&fontFamily, static_cast<Gdiplus::REAL>(isNarrow ? 12 : 14), Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
    Gdiplus::SolidBrush goldBrush(Gdiplus::Color(255, 247, 227, 175));
    Gdiplus::RectF tokenRect(static_cast<float>(cardX + 10), static_cast<float>(cardY + 8), static_cast<float>(cardW - 20), 22.0f);
    g.DrawString((L"科研解鎖授權碼: " + wToken).c_str(), -1, &tokenFont, tokenRect, &centerFormat, &goldBrush);

    Gdiplus::Font guideFont(&fontFamily, static_cast<Gdiplus::REAL>(isNarrow ? 10 : 12), Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
    Gdiplus::RectF guideRect(static_cast<float>(cardX + 14), static_cast<float>(cardY + 32), static_cast<float>(cardW - 28), static_cast<float>(cardH - 38));
    g.DrawString(L"1. 感謝您的寶貴數據回饋，協助推動眼睛疲勞監測科研進展。\n2. 本機 SQLite 時序資料庫已驗證並解除鎖定。\n3. 您現在可以安全關閉並解除安裝本軟體。", -1, &guideFont, guideRect, &centerFormat, &whiteBrush);

    // 3. 動作按鈕 (完成並關閉應用程式)
    int btnH = isNarrow ? 44 : 50;
    int btnW = isNarrow ? std::min(w - 50, 300) : 300;
    int btnX = (w - btnW) / 2;
    int btnY = cardY + cardH + (isNarrow ? 16 : 24);
    m_exitAppBtnRect = { btnX, btnY, btnX + btnW, btnY + btnH };

    Gdiplus::Color exitBtnColor = m_isHoveringExitAppBtn ? Gdiplus::Color(255, 245, 245, 245) : Gdiplus::Color(255, 255, 255, 255);
    Gdiplus::SolidBrush exitBtnBrush(exitBtnColor);

    Gdiplus::GraphicsPath path;
    int r = 16;
    path.AddArc(btnX, btnY, r, r, 180, 90);
    path.AddArc(btnX + btnW - r, btnY, r, r, 270, 90);
    path.AddArc(btnX + btnW - r, btnY + btnH - r, r, r, 0, 90);
    path.AddArc(btnX, btnY + btnH - r, r, r, 90, 90);
    path.CloseFigure();
    g.FillPath(&exitBtnBrush, &path);

    Gdiplus::Font btnFont(&fontFamily, static_cast<Gdiplus::REAL>(isNarrow ? 15 : 17), Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
    Gdiplus::SolidBrush btnTextBrush(Gdiplus::Color(255, 30, 177, 138));
    Gdiplus::RectF btnTextRect(static_cast<float>(btnX), static_cast<float>(btnY), static_cast<float>(btnW), static_cast<float>(btnH));
    g.DrawString(L"完成並退出系統", -1, &btnFont, btnTextRect, &centerFormat, &btnTextBrush);

    // 次要按鈕：返回監控中心
    int retBtnH = 30;
    int retBtnW = 160;
    int retBtnX = (w - retBtnW) / 2;
    int retBtnY = btnY + btnH + 10;
    m_returnDashboardBtnRect = { retBtnX, retBtnY, retBtnX + retBtnW, retBtnY + retBtnH };

    Gdiplus::Font retFont(&fontFamily, 11, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
    Gdiplus::SolidBrush retBrush(m_isHoveringReturnDashboardBtn ? Gdiplus::Color(255, 247, 227, 175) : Gdiplus::Color(200, 255, 255, 255));
    Gdiplus::RectF retRect(static_cast<float>(retBtnX), static_cast<float>(retBtnY), static_cast<float>(retBtnW), static_cast<float>(retBtnH));
    g.DrawString(L"◀ 返回即時監控中心", -1, &retFont, retRect, &centerFormat, &retBrush);
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
