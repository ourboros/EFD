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

    // 0. 優先嘗試專案絕對路徑
    searchCandidates.push_back(L"E:\\Project\\EFD\\design\\1x\\" + filename);
    searchCandidates.push_back(L"E:\\Project\\EFD\\src\\ui\\assets\\" + filename);

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

// 繪製 20px 圓角邊框按鈕工具函式
static void drawRoundedButton(Gdiplus::Graphics& g, int x, int y, int w, int h, int radius, Gdiplus::Brush* brush, Gdiplus::Pen* pen = nullptr) {
    int d = radius * 2;
    if (d > w) d = w;
    if (d > h) d = h;
    Gdiplus::GraphicsPath path;
    path.AddArc(x, y, d, d, 180, 90);
    path.AddArc(x + w - d, y, d, d, 270, 90);
    path.AddArc(x + w - d, y + h - d, d, d, 0, 90);
    path.AddArc(x, y + h - d, d, d, 90, 90);
    path.CloseFigure();
    if (brush) g.FillPath(brush, &path);
    if (pen) g.DrawPath(pen, &path);
}

} // anonymous namespace

NativeWelcomeWindow::NativeWelcomeWindow(int width, int height)
    : m_width(width), m_height(height), m_engine(PlatformType::Windows) {
    
    // 初始化 GDI+
    Gdiplus::GdiplusStartupInput gdiplusStartupInput;
    Gdiplus::GdiplusStartup(&m_gdiplusToken, &gdiplusStartupInput, NULL);

    // 載入設計資產 (歡迎介面 Logo: 直接套用 E:\Project\EFD\design\1x\資產 10.png)
    m_logoImage = std::make_unique<Gdiplus::Image>(L"E:\\Project\\EFD\\design\\1x\\資產 10.png");
    if (!m_logoImage || m_logoImage->GetLastStatus() != Gdiplus::Ok || m_logoImage->GetWidth() == 0) {
        m_logoImage = loadAssetImage(L"資產 10.png");
    }
    if (!m_logoImage || m_logoImage->GetLastStatus() != Gdiplus::Ok || m_logoImage->GetWidth() == 0) {
        m_logoImage = loadAssetImage(L"logo.png");
    }
    m_asset5Image = loadAssetImage(L"資產 5.png");
    m_asset6Image = loadAssetImage(L"資產 6.png");

    // 初始化健康先驗遙測資料
    m_latestTelemetry.eyeMetrics.earAvg = 0.312f;
    m_latestTelemetry.eyeMetrics.perclos = 0.0f;
    m_latestTelemetry.eyeMetrics.blinkCount = 0;
    m_latestTelemetry.complexityMetrics.complexityIndex = 4.50f;
    m_latestTelemetry.systemState.currentFatigueScore = 5.0f;
    m_latestTelemetry.systemState.fatigueLevel = FatigueLevel::Relaxed;
    m_latestTelemetry.lifecycleSummary = "相機狀態: 正在連接前置攝影機 (30 FPS)...";
    m_latestTelemetry.currentStudyDay = m_engine.getStudyTracker().getCurrentDay();
    m_latestTelemetry.subjectUuid = m_engine.getStudyTracker().getSubjectUuid();
    m_latestTelemetry.studyStatus = m_engine.getStudyTracker().getStatus();

    // 綁定置頂懸浮指標 HUD 雙擊還原事件
    m_floatingIndicator.setRestoreCallback([this]() {
        this->showMainWindow();
    });

    // 綁定系統托盤功能表事件
    m_trayManager.setActionCallback([this](TrayMenuAction action) {
        switch (action) {
        case TrayMenuAction::ShowMainWindow:
            this->showMainWindow();
            break;
        case TrayMenuAction::ToggleFloatingIndicator:
            this->toggleFloatingHUD();
            break;
        case TrayMenuAction::Recalibrate:
            this->showMainWindow();
            this->setStage(UIStage::CalibrationInstruction);
            break;
        case TrayMenuAction::EndStudyGate:
            this->showMainWindow();
            this->m_engine.getStudyTracker().triggerPostStudyLock();
            this->setStage(UIStage::StudyCompletedGate);
            break;
        case TrayMenuAction::ExitApp:
            if (this->m_hwnd) PostMessage(this->m_hwnd, WM_CLOSE, 0, 0);
            break;
        }
    });

    // 綁定五執行緒引擎遙測事件 (即時更新 HUD 與托盤)
    m_engine.setTelemetryCallback([this](const EngineTelemetry& t) {
        this->m_latestTelemetry = t;
        // 即時同步至懸浮指標 HUD
        this->m_floatingIndicator.updateMetrics(
            t.eyeMetrics.earAvg,
            t.systemState.currentFatigueScore,
            t.systemState.fatigueLevel,
            t.lifecycleSummary
        );
        // 即時更新系統托盤提示
        this->m_trayManager.updateStatus(
            t.systemState.fatigueLevel,
            t.systemState.currentFatigueScore,
            utf8ToWide(t.lifecycleSummary)
        );

        if (this->m_hwnd && (this->m_currentStage == UIStage::MainDashboard || 
                             this->m_currentStage == UIStage::CalibrationInstruction ||
                             this->m_currentStage == UIStage::SettingsPanel)) {
            InvalidateRect(this->m_hwnd, NULL, FALSE);
        }
    });

    m_engine.setAlertCallback([this](FatigueLevel level, float score, const std::string& msg) {
        // 僅於使用者眼睛疲勞值超標時 (SevereWarning 或疲勞分數 >= 70.0) 跳出提醒
        if (level == FatigueLevel::SevereWarning || score >= 70.0f) {
            std::ostringstream oss;
            oss << "疲勞指數 " << std::fixed << std::setprecision(1) << score << " - 你的眼睛處於疲勞狀態，請適當休息";
            this->m_dashboardMessage = oss.str();
            
            // 發送 Windows 原生氣泡/Toast 警報通知 (純文字無符號表情)
            if (this->m_soundAlertEnabled) {
                this->m_trayManager.showBalloonNotification(
                    L"你的眼睛處於疲勞狀態，請適當休息",
                    L"你的眼睛處於疲勞狀態，請適當休息",
                    level
                );
            }
        }
    });
}

NativeWelcomeWindow::~NativeWelcomeWindow() {
    m_engine.stop();
    m_trayManager.removeIcon();
    m_floatingIndicator.hide();
    m_logoImage.reset();
    m_asset5Image.reset();
    m_asset6Image.reset();
    if (m_gdiplusToken) {
        Gdiplus::GdiplusShutdown(m_gdiplusToken);
    }
}

void NativeWelcomeWindow::showMainWindow() {
    if (m_hwnd) {
        ShowWindow(m_hwnd, SW_RESTORE);
        SetForegroundWindow(m_hwnd);
    }
}

void NativeWelcomeWindow::toggleFloatingHUD() {
    m_floatingIndicator.toggle();
}

void NativeWelcomeWindow::minimizeToTray() {
    if (!m_engine.isRunning()) {
        m_engine.start();
    }
    if (m_hwnd) {
        ShowWindow(m_hwnd, SW_HIDE);
        // 靜默轉入背景執行，僅於眼睛疲勞值超標時跳出提醒
    }
}

void NativeWelcomeWindow::setStage(UIStage stage) {
    m_currentStage = stage;
    m_stageTimeSec = 0.0f;
    m_animTimeSec = 0.0f;
    if (stage == UIStage::ActiveCalibration) {
        m_calibrationProgress = 0.0f;
    }
    if (stage == UIStage::CalibrationInstruction || stage == UIStage::ActiveCalibration || stage == UIStage::MainDashboard) {
        if (!m_engine.isRunning()) {
            m_engine.start();
        }
    }
    if (m_hwnd) {
        InvalidateRect(m_hwnd, NULL, FALSE);
    }
}

void NativeWelcomeWindow::handleMouseClick(int x, int y) {
    POINT pt = { x, y };

    // 依據主畫面流程按鈕進行切換 (無頂部導覽列)
    if (m_currentStage == UIStage::Welcome) {
        if (PtInRect(&m_startBtnRect, pt)) {
            setStage(UIStage::CalibrationInstruction);
        }
    } else if (m_currentStage == UIStage::CalibrationInstruction) {
        if (PtInRect(&m_readyBtnRect, pt)) {
            setStage(UIStage::CountdownWait);
        } else if (PtInRect(&m_backWelcomeBtnRect, pt)) {
            setStage(UIStage::Welcome);
        }
    } else if (m_currentStage == UIStage::ActiveCalibration) {
        if (PtInRect(&m_skipCalibBtnRect, pt)) {
            m_engine.calibrate(3.0f);
            setStage(UIStage::CalibrationResult);
        }
    } else if (m_currentStage == UIStage::CalibrationResult) {
        if (PtInRect(&m_proceedDashboardBtnRect, pt)) {
            setStage(UIStage::MainDashboard);
        } else if (PtInRect(&m_closeBgResultBtnRect, pt)) {
            minimizeToTray();
        } else if (PtInRect(&m_restartCalibBtnRect, pt)) {
            setStage(UIStage::CalibrationInstruction);
        }
    } else if (m_currentStage == UIStage::MainDashboard) {
        if (PtInRect(&m_settingsBtnRect, pt)) {
            setStage(UIStage::SettingsPanel);
        } else if (PtInRect(&m_minimizeTrayBtnRect, pt)) {
            minimizeToTray();
        }
    } else if (m_currentStage == UIStage::SettingsPanel) {
        if (PtInRect(&m_sensitivityBtnRect, pt)) {
            m_sensitivityLevel = (m_sensitivityLevel + 1) % 3;
            if (m_hwnd) InvalidateRect(m_hwnd, NULL, FALSE);
        } else if (PtInRect(&m_recalibFromSettingsBtnRect, pt)) {
            setStage(UIStage::CalibrationInstruction);
        } else if (PtInRect(&m_saveSettingsBtnRect, pt)) {
            setStage(UIStage::MainDashboard);
        }
    } else if (m_currentStage == UIStage::StudyCompletedGate) {
        if (PtInRect(&m_fillQuestionnaireBtnRect, pt)) {
            // 直接以系統預設瀏覽器開啟 Google 表單後測問卷
            ShellExecuteW(NULL, L"open", L"https://forms.gle/y5f1jTnrrtsxz65G9", NULL, NULL, SW_SHOWNORMAL);
            m_engine.getStudyTracker().submitQuestionnaire("Study_Post_Survey_Completed");
            m_engine.getSyncWorker().triggerSync(
                m_engine.getStudyTracker().getSubjectUuid(),
                m_engine.getStudyTracker().getCurrentDay(),
                "Study_Post_Survey_Completed",
                [this](const SyncResult& res) {
                    if (res.success) {
                        this->m_engine.getStudyTracker().submitQuestionnaire("Study_Post_Survey_Completed");
                    }
                }
            );
            setStage(UIStage::QuestionnaireSubmitted);
        } else if (PtInRect(&m_returnDashboardBtnRect, pt)) {
            setStage(UIStage::MainDashboard);
        }
    } else if (m_currentStage == UIStage::QuestionnaireSubmitted) {
        if (PtInRect(&m_exitAppBtnRect, pt)) {
            PostMessage(m_hwnd, WM_CLOSE, 0, 0);
        } else if (PtInRect(&m_returnDashboardBtnRect, pt)) {
            setStage(UIStage::MainDashboard);
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
            setStage(UIStage::ActiveCalibration);
        }
        if (m_hwnd) InvalidateRect(m_hwnd, NULL, FALSE);
    } else if (m_currentStage == UIStage::ActiveCalibration) {
        // 階段 4: 實際多點眼動採樣 (5 點巡迴移動，共 5 秒)
        struct TargetPoint { float x, y; };
        const TargetPoint points[] = {
            { 0.50f, 0.50f }, // 0. 中心
            { 0.15f, 0.22f }, // 1. 左上
            { 0.85f, 0.22f }, // 2. 右上
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
            // 採樣完成，校準基準並進入階段 5 (測驗完成提示)
            m_engine.calibrate(3.0f);
            setStage(UIStage::CalibrationResult);
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

    case SystemTrayManager::WM_TRAY_NOTIFY:
        pThis->m_trayManager.handleTrayMessage(lParam);
        return 0;

    case WM_SYSCOMMAND:
        if ((wParam & 0xFFF0) == SC_MINIMIZE) {
            pThis->minimizeToTray();
            return 0;
        }
        break;

    case WM_MOUSEMOVE: {
        int x = GET_X_LPARAM(lParam);
        int y = GET_Y_LPARAM(lParam);
        POINT pt = { x, y };

        // 檢查導覽列 Hover
        int hoveredTab = -1;
        for (size_t i = 0; i < pThis->m_navTabRects.size(); ++i) {
            if (PtInRect(&pThis->m_navTabRects[i], pt)) {
                hoveredTab = static_cast<int>(i);
                break;
            }
        }

        bool inStart = (pThis->m_currentStage == UIStage::Welcome) && (PtInRect(&pThis->m_startBtnRect, pt) != FALSE);
        bool inReady = (pThis->m_currentStage == UIStage::CalibrationInstruction) && (PtInRect(&pThis->m_readyBtnRect, pt) != FALSE);
        bool inBackWelcome = (pThis->m_currentStage == UIStage::CalibrationInstruction) && (PtInRect(&pThis->m_backWelcomeBtnRect, pt) != FALSE);
        bool inSkipCalib = (pThis->m_currentStage == UIStage::ActiveCalibration) && (PtInRect(&pThis->m_skipCalibBtnRect, pt) != FALSE);
        bool inProceedDash = (pThis->m_currentStage == UIStage::CalibrationResult) && (PtInRect(&pThis->m_proceedDashboardBtnRect, pt) != FALSE);
        bool inCloseBgResult = (pThis->m_currentStage == UIStage::CalibrationResult) && (PtInRect(&pThis->m_closeBgResultBtnRect, pt) != FALSE);
        bool inRestartCalib = (pThis->m_currentStage == UIStage::CalibrationResult) && (PtInRect(&pThis->m_restartCalibBtnRect, pt) != FALSE);
        bool inSettings = (pThis->m_currentStage == UIStage::MainDashboard) && (PtInRect(&pThis->m_settingsBtnRect, pt) != FALSE);
        bool inMinimizeTray = (pThis->m_currentStage == UIStage::MainDashboard) && (PtInRect(&pThis->m_minimizeTrayBtnRect, pt) != FALSE);
        bool inSensitivity = (pThis->m_currentStage == UIStage::SettingsPanel) && (PtInRect(&pThis->m_sensitivityBtnRect, pt) != FALSE);
        bool inRecalibFromSet = (pThis->m_currentStage == UIStage::SettingsPanel) && (PtInRect(&pThis->m_recalibFromSettingsBtnRect, pt) != FALSE);
        bool inSaveSet = (pThis->m_currentStage == UIStage::SettingsPanel) && (PtInRect(&pThis->m_saveSettingsBtnRect, pt) != FALSE);
        bool inFillQ = (pThis->m_currentStage == UIStage::StudyCompletedGate) && (PtInRect(&pThis->m_fillQuestionnaireBtnRect, pt) != FALSE);
        bool inReturnDash = ((pThis->m_currentStage == UIStage::StudyCompletedGate || pThis->m_currentStage == UIStage::QuestionnaireSubmitted)) && (PtInRect(&pThis->m_returnDashboardBtnRect, pt) != FALSE);
        bool inExitApp = (pThis->m_currentStage == UIStage::QuestionnaireSubmitted) && (PtInRect(&pThis->m_exitAppBtnRect, pt) != FALSE);

        bool hasChange = (hoveredTab != pThis->m_hoveredNavTab ||
                          inStart != pThis->m_isHoveringStartBtn || 
                          inReady != pThis->m_isHoveringReadyBtn || 
                          inBackWelcome != pThis->m_isHoveringBackWelcomeBtn ||
                          inSkipCalib != pThis->m_isHoveringSkipCalibBtn ||
                          inProceedDash != pThis->m_isHoveringProceedDashboardBtn ||
                          inCloseBgResult != pThis->m_isHoveringCloseBgResultBtn ||
                          inRestartCalib != pThis->m_isHoveringRestartCalibBtn ||
                          inSettings != pThis->m_isHoveringSettingsBtn ||
                          inMinimizeTray != pThis->m_isHoveringMinimizeTrayBtn ||
                          inSensitivity != pThis->m_isHoveringSensitivityBtn ||
                          inRecalibFromSet != pThis->m_isHoveringRecalibFromSettingsBtn ||
                          inSaveSet != pThis->m_isHoveringSaveSettingsBtn ||
                          inFillQ != pThis->m_isHoveringFillQuestionnaireBtn ||
                          inReturnDash != pThis->m_isHoveringReturnDashboardBtn ||
                          inExitApp != pThis->m_isHoveringExitAppBtn);

        if (hasChange) {
            pThis->m_hoveredNavTab = hoveredTab;
            pThis->m_isHoveringStartBtn = inStart;
            pThis->m_isHoveringReadyBtn = inReady;
            pThis->m_isHoveringBackWelcomeBtn = inBackWelcome;
            pThis->m_isHoveringSkipCalibBtn = inSkipCalib;
            pThis->m_isHoveringProceedDashboardBtn = inProceedDash;
            pThis->m_isHoveringCloseBgResultBtn = inCloseBgResult;
            pThis->m_isHoveringRestartCalibBtn = inRestartCalib;
            pThis->m_isHoveringSettingsBtn = inSettings;
            pThis->m_isHoveringMinimizeTrayBtn = inMinimizeTray;
            pThis->m_isHoveringSensitivityBtn = inSensitivity;
            pThis->m_isHoveringRecalibFromSettingsBtn = inRecalibFromSet;
            pThis->m_isHoveringSaveSettingsBtn = inSaveSet;
            pThis->m_isHoveringFillQuestionnaireBtn = inFillQ;
            pThis->m_isHoveringReturnDashboardBtn = inReturnDash;
            pThis->m_isHoveringExitAppBtn = inExitApp;

            bool isAnyHovered = (hoveredTab != -1 || inStart || inReady || inBackWelcome || inSkipCalib || inProceedDash || inRestartCalib || inSettings || inMinimizeTray || inSensitivity || inRecalibFromSet || inSaveSet || inFillQ || inReturnDash || inExitApp);
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
    case UIStage::CalibrationResult:
        drawCalibrationResult(g, width, height);
        break;
    case UIStage::MainDashboard:
        drawMainDashboard(g, width, height);
        break;
    case UIStage::SettingsPanel:
        drawSettingsPanel(g, width, height);
        break;
    case UIStage::StudyCompletedGate:
        drawStudyCompletedGate(g, width, height);
        break;
    case UIStage::QuestionnaireSubmitted:
        drawQuestionnaireSubmitted(g, width, height);
        break;
    }

    // 繪製頂部全局快速導覽切換列 (Top Navigation Bar)
    drawTopNavigationBar(g, width, height);

    BitBlt(hdc, 0, 0, width, height, memDC, 0, 0, SRCCOPY);

    SelectObject(memDC, oldBitmap);
    DeleteObject(memBitmap);
    DeleteDC(memDC);

    EndPaint(hwnd, &ps);
}

// -----------------------------------------------------------------------------
// 頂部全局快速導覽列 (Top Navigation Tab Bar - 方便隨時預覽各階段 UI)
// -----------------------------------------------------------------------------
void NativeWelcomeWindow::drawTopNavigationBar(Gdiplus::Graphics& g, int w, int /*h*/) {
    bool isNarrow = (w < 680);
    int navH = isNarrow ? 56 : 34;
    Gdiplus::SolidBrush navBg(Gdiplus::Color(210, 24, 28, 18));
    g.FillRectangle(&navBg, 0, 0, w, navH);

    Gdiplus::Pen navBottomLine(Gdiplus::Color(100, 255, 255, 255), 1.0f);
    g.DrawLine(&navBottomLine, 0, navH - 1, w, navH - 1);

    const struct NavItem {
        UIStage stage;
        const wchar_t* label;
    } items[] = {
        { UIStage::Welcome,                L"歡迎" },
        { UIStage::CalibrationInstruction, L"說明" },
        { UIStage::CountdownWait,          L"倒數" },
        { UIStage::ActiveCalibration,      L"測驗" },
        { UIStage::CalibrationResult,      L"結果" },
        { UIStage::MainDashboard,          L"監控" },
        { UIStage::SettingsPanel,          L"設定" },
        { UIStage::StudyCompletedGate,     L"後測" },
        { UIStage::QuestionnaireSubmitted, L"問卷" }
    };

    size_t itemCount = sizeof(items) / sizeof(items[0]);
    m_navTabRects.resize(itemCount);

    Gdiplus::FontFamily fontFamily(L"Microsoft JhengHei");
    Gdiplus::StringFormat centerFormat;
    centerFormat.SetAlignment(Gdiplus::StringAlignmentCenter);
    centerFormat.SetLineAlignment(Gdiplus::StringAlignmentCenter);

    if (!isNarrow) {
        int gap = 4;
        int totalPadding = 12;
        int tabW = std::clamp((w - totalPadding * 2 - static_cast<int>(itemCount - 1) * gap) / static_cast<int>(itemCount), 50, 105);
        int totalTabsW = static_cast<int>(itemCount) * tabW + static_cast<int>(itemCount - 1) * gap;
        int startX = (w - totalTabsW) / 2;
        if (startX < totalPadding) startX = totalPadding;

        int tabFontSize = (tabW < 75) ? 10 : 11;
        Gdiplus::Font tabFont(&fontFamily, static_cast<Gdiplus::REAL>(tabFontSize), Gdiplus::FontStyleBold, Gdiplus::UnitPixel);

        for (size_t i = 0; i < itemCount; ++i) {
            int tabX = startX + static_cast<int>(i) * (tabW + gap);
            int tabY = 4;
            int tabH = navH - 8;
            m_navTabRects[i] = { tabX, tabY, tabX + tabW, tabY + tabH };

            bool isActive = (m_currentStage == items[i].stage);
            bool isHovered = (static_cast<int>(i) == m_hoveredNavTab);

            if (isActive) {
                Gdiplus::SolidBrush activeBg(Gdiplus::Color(255, 30, 177, 138)); // 薄荷綠
                g.FillRectangle(&activeBg, tabX, tabY, tabW, tabH);
                Gdiplus::SolidBrush textBrush(Gdiplus::Color(255, 255, 255, 255));
                Gdiplus::RectF textRect(static_cast<float>(tabX), static_cast<float>(tabY), static_cast<float>(tabW), static_cast<float>(tabH));
                g.DrawString(items[i].label, -1, &tabFont, textRect, &centerFormat, &textBrush);
            } else {
                if (isHovered) {
                    Gdiplus::SolidBrush hoverBg(Gdiplus::Color(180, 50, 60, 40));
                    g.FillRectangle(&hoverBg, tabX, tabY, tabW, tabH);
                }
                Gdiplus::SolidBrush textBrush(isHovered ? Gdiplus::Color(255, 247, 227, 175) : Gdiplus::Color(200, 200, 200, 200));
                Gdiplus::RectF textRect(static_cast<float>(tabX), static_cast<float>(tabY), static_cast<float>(tabW), static_cast<float>(tabH));
                g.DrawString(items[i].label, -1, &tabFont, textRect, &centerFormat, &textBrush);
            }
        }
    } else {
        // 手機/窄螢幕 2 列排版：第 1 列 5 個分頁，第 2 列 4 個分頁
        int gap = 3;
        int padding = 6;
        int row1Count = 5;
        int row2Count = 4;
        int tabH = 22;

        int tabW1 = (w - padding * 2 - (row1Count - 1) * gap) / row1Count;
        int tabW2 = (w - padding * 2 - (row2Count - 1) * gap) / row2Count;

        Gdiplus::Font tabFont(&fontFamily, 10, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);

        for (size_t i = 0; i < itemCount; ++i) {
            int tabX, tabY, tabW;
            if (i < 5) {
                tabX = padding + static_cast<int>(i) * (tabW1 + gap);
                tabY = 4;
                tabW = tabW1;
            } else {
                tabX = padding + static_cast<int>(i - 5) * (tabW2 + gap);
                tabY = 30;
                tabW = tabW2;
            }

            m_navTabRects[i] = { tabX, tabY, tabX + tabW, tabY + tabH };

            bool isActive = (m_currentStage == items[i].stage);
            bool isHovered = (static_cast<int>(i) == m_hoveredNavTab);

            if (isActive) {
                Gdiplus::SolidBrush activeBg(Gdiplus::Color(255, 30, 177, 138));
                g.FillRectangle(&activeBg, tabX, tabY, tabW, tabH);
                Gdiplus::SolidBrush textBrush(Gdiplus::Color(255, 255, 255, 255));
                Gdiplus::RectF textRect(static_cast<float>(tabX), static_cast<float>(tabY), static_cast<float>(tabW), static_cast<float>(tabH));
                g.DrawString(items[i].label, -1, &tabFont, textRect, &centerFormat, &textBrush);
            } else {
                if (isHovered) {
                    Gdiplus::SolidBrush hoverBg(Gdiplus::Color(180, 50, 60, 40));
                    g.FillRectangle(&hoverBg, tabX, tabY, tabW, tabH);
                }
                Gdiplus::SolidBrush textBrush(isHovered ? Gdiplus::Color(255, 247, 227, 175) : Gdiplus::Color(200, 200, 200, 200));
                Gdiplus::RectF textRect(static_cast<float>(tabX), static_cast<float>(tabY), static_cast<float>(tabW), static_cast<float>(tabH));
                g.DrawString(items[i].label, -1, &tabFont, textRect, &centerFormat, &textBrush);
            }
        }
    }
}

// -----------------------------------------------------------------------------
// 階段 1：系統歡迎介面 (保持 Logo 100% 原圖長寬比 + 純白高對比徽章容器 + RWD 自適應)
// -----------------------------------------------------------------------------
void NativeWelcomeWindow::drawWelcomeScreen(Gdiplus::Graphics& g, int w, int h) {
    // 滿版薄荷綠背景 (#1EB18A)
    Gdiplus::SolidBrush bgBrush(Gdiplus::Color(255, 30, 177, 138));
    g.FillRectangle(&bgBrush, 0, 0, w, h);

    bool isNarrow = (w < 680 || h > w);

    // 1. 計算 Logo 100% 原始長寬比 (資產 10.png 原始比例為 568:341 = 1.6657)
    float naturalAspect = 568.0f / 341.0f; // 1.6657
    int origImgW = 70;
    int origImgH = 42;

    if (m_logoImage && m_logoImage->GetLastStatus() == Gdiplus::Ok && m_logoImage->GetWidth() > 0 && m_logoImage->GetHeight() > 0) {
        origImgW = static_cast<int>(m_logoImage->GetWidth());
        origImgH = static_cast<int>(m_logoImage->GetHeight());
        naturalAspect = static_cast<float>(origImgW) / static_cast<float>(origImgH);
    }

    // 依視窗尺寸優雅置中繪製高解析度 Logo (寬度縮小 50% 至 110px，長寬比適配)
    int logoW = isNarrow ? std::clamp(w / 6, 70, 100) : 110;
    int logoH = static_cast<int>(logoW / naturalAspect);

    int logoX = (w - logoW) / 2;
    int logoY = isNarrow ? std::max(45, h / 2 - logoH - 60) : (h / 2 - 130);

    g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
    g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);

    if (m_logoImage && m_logoImage->GetLastStatus() == Gdiplus::Ok && m_logoImage->GetWidth() > 0) {
        g.DrawImage(m_logoImage.get(), logoX, logoY, logoW, logoH);
    }

    // 2. 標題文字: "感謝協助測試EFD" (純白 #FFFFFF, 響應式字體大小, 純文字無前綴符號)
    Gdiplus::FontFamily fontFamily(L"Microsoft JhengHei");
    int titleFontSize = isNarrow ? std::clamp(w / 18, 18, 26) : 30;
    Gdiplus::Font titleFont(&fontFamily, static_cast<Gdiplus::REAL>(titleFontSize), Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
    Gdiplus::SolidBrush textBrush(Gdiplus::Color(255, 255, 255, 255));

    Gdiplus::StringFormat format;
    format.SetAlignment(Gdiplus::StringAlignmentCenter);
    format.SetLineAlignment(Gdiplus::StringAlignmentCenter);

    float titleY = static_cast<float>(logoY + logoH + (isNarrow ? 20 : 30));
    Gdiplus::RectF titleRect(0.0f, titleY, static_cast<float>(w), 40.0f);
    g.DrawString(L"感謝協助測試EFD", -1, &titleFont, titleRect, &format, &textBrush);

    // 副標題 (粗體)
    Gdiplus::Font subFont(&fontFamily, static_cast<Gdiplus::REAL>(isNarrow ? 12 : 14), Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
    Gdiplus::SolidBrush subBrush(Gdiplus::Color(230, 247, 227, 175));
    Gdiplus::RectF subRect(0.0f, titleY + 38.0f, static_cast<float>(w), 24.0f);
    g.DrawString(L"AI 驅動眼睛特徵提取與即時疲勞監控研究系統", -1, &subFont, subRect, &format, &subBrush);

    // 3. 進入測試說明按鈕 (粗體字體、20px 圓角邊框)
    int btnWidth = isNarrow ? std::clamp(w - 80, 180, 240) : 240;
    int btnHeight = isNarrow ? 48 : 52;
    int btnX = (w - btnWidth) / 2;
    int btnY = isNarrow ? static_cast<int>(titleY + 75) : std::max(h / 2 + 75, static_cast<int>(titleY + 75));
    m_startBtnRect = { btnX, btnY, btnX + btnWidth, btnY + btnHeight };

    Gdiplus::Color btnColor = m_isHoveringStartBtn ? Gdiplus::Color(255, 245, 245, 245) : Gdiplus::Color(255, 255, 255, 255);
    Gdiplus::SolidBrush btnBrush(btnColor);

    // 繪製 20px 圓角矩形按鈕
    drawRoundedButton(g, btnX, btnY, btnWidth, btnHeight, 20, &btnBrush);

    // 按鈕文字: 薄荷綠 (#1EB18A), 粗體, 無表情貼
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
    int titleFontSize = isNarrow ? std::clamp(w / 22, 16, 22) : 25;
    Gdiplus::Font titleFont(&fontFamily, static_cast<Gdiplus::REAL>(titleFontSize), Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
    Gdiplus::SolidBrush whiteBrush(Gdiplus::Color(255, 255, 255, 255));
    float titleY = 40.0f;
    Gdiplus::RectF titleRect(0.0f, titleY, static_cast<float>(w), 30.0f);
    g.DrawString(L"眼動特徵提取與校準說明", -1, &titleFont, titleRect, &centerFormat, &whiteBrush);

    // 2. 即時相機連線狀態標籤 (Camera Hardware Status Bar)
    std::string camStatusStr = m_latestTelemetry.lifecycleSummary;
    if (camStatusStr.empty()) {
        camStatusStr = "相機狀態: 正在連接前置鏡頭...";
    }
    std::wstring wCamStatus = utf8ToWide(camStatusStr);
    Gdiplus::Font camStatusFont(&fontFamily, 11, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
    Gdiplus::SolidBrush camStatusBrush(Gdiplus::Color(255, 247, 227, 175)); // 金黃
    Gdiplus::RectF camStatusRect(0.0f, titleY + 30.0f, static_cast<float>(w), 18.0f);
    g.DrawString(wCamStatus.c_str(), -1, &camStatusFont, camStatusRect, &centerFormat, &camStatusBrush);

    int btnW = isNarrow ? std::clamp(w - 60, 180, 240) : 240;
    int btnH = isNarrow ? 40 : 46;
    int btnX = (w - btnW) / 2;
    int btnY = h - (isNarrow ? 52 : 62);
    m_readyBtnRect = { btnX, btnY, btnX + btnW, btnY + btnH };

    // 次要按鈕：返回歡迎頁 (20px 圓角邊框, 粗體)
    int backBtnW = 120;
    int backBtnH = 32;
    int backBtnX = 20;
    int backBtnY = h - 46;
    m_backWelcomeBtnRect = { backBtnX, backBtnY, backBtnX + backBtnW, backBtnY + backBtnH };

    Gdiplus::SolidBrush backBg(m_isHoveringBackWelcomeBtn ? Gdiplus::Color(220, 24, 140, 108) : Gdiplus::Color(140, 20, 120, 90));
    drawRoundedButton(g, backBtnX, backBtnY, backBtnW, backBtnH, 20, &backBg);
    Gdiplus::Font backFont(&fontFamily, 12, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
    Gdiplus::SolidBrush backBrush(m_isHoveringBackWelcomeBtn ? Gdiplus::Color(255, 247, 227, 175) : Gdiplus::Color(255, 255, 255, 255));
    Gdiplus::RectF backRect(static_cast<float>(backBtnX), static_cast<float>(backBtnY), static_cast<float>(backBtnW), static_cast<float>(backBtnH));
    g.DrawString(L"返回歡迎頁", -1, &backFont, backRect, &centerFormat, &backBrush);

    if (!isNarrow) {
        // 寬螢幕：水平橫向排列 (Flex Row)
        int boxW = (w >= 840) ? 380 : (w - 100) / 2;
        int boxH = std::clamp(h - 200, 190, 250);
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
        g.DrawString(L"測試動態路徑演示 (DEMO 預覽)", -1, &tagFont, tagRect, &leftFormat, &tagBrush);

        // 演示小黃點
        int demoDotX = boxX + static_cast<int>(m_demoDotX * boxW);
        int demoDotY = boxY + static_cast<int>(m_demoDotY * boxH);
        Gdiplus::SolidBrush demoYellowDot(Gdiplus::Color(255, 247, 227, 175));
        g.FillEllipse(&demoYellowDot, demoDotX - 12, demoDotY - 12, 24, 24);
        Gdiplus::Pen demoPulsePen(Gdiplus::Color(120, 247, 227, 175), 1.5f);
        g.DrawEllipse(&demoPulsePen, demoDotX - 18, demoDotY - 18, 36, 36);

        // 右側 3 大指引卡片 (純文字無數字前綴符號)
        auto drawGuideItem = [&](int idx, const wchar_t* title, const wchar_t* desc) {
            int itemY = guideY + idx * (boxH / 3 + 2);
            int cardH = boxH / 3 - 8;
            Gdiplus::SolidBrush cardBg(Gdiplus::Color(180, 20, 140, 108));
            g.FillRectangle(&cardBg, guideX, itemY, guideW, cardH);

            Gdiplus::Font hFont(&fontFamily, 15, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
            Gdiplus::SolidBrush hBrush(Gdiplus::Color(255, 247, 227, 175));
            Gdiplus::RectF hRect(static_cast<float>(guideX + 12), static_cast<float>(itemY + 6), static_cast<float>(guideW - 24), 22.0f);
            g.DrawString(title, -1, &hFont, hRect, &leftFormat, &hBrush);

            Gdiplus::Font dFont(&fontFamily, 12, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
            Gdiplus::SolidBrush dBrush(Gdiplus::Color(255, 255, 255, 255));
            Gdiplus::RectF dRect(static_cast<float>(guideX + 12), static_cast<float>(itemY + 28), static_cast<float>(guideW - 24), static_cast<float>(cardH - 30));
            g.DrawString(desc, -1, &dFont, dRect, &leftFormat, &dBrush);
        };

        drawGuideItem(0, L"臉部正面對齊鏡頭", L"保持端正坐姿，確保鏡頭能清晰捕捉面部特徵。");
        drawGuideItem(1, L"視線跟隨黃點移動", L"測試開始後，請專注凝視黃點並跟隨其移動。");
        drawGuideItem(2, L"保持自然睜眼狀態", L"校準過程僅需 5 秒鐘，請保持自然眨眼與專注。");

    } else {
        // 手機/窄螢幕：垂直向下排列 (Flex Column)
        int boxW = std::min(w - 30, 360);
        int boxH = std::clamp(h / 4, 105, 140);
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
        g.DrawString(L"測試動態路徑演示 (DEMO 預覽)", -1, &tagFont, tagRect, &leftFormat, &tagBrush);

        // 演示小黃點
        int demoDotX = boxX + static_cast<int>(m_demoDotX * boxW);
        int demoDotY = boxY + static_cast<int>(m_demoDotY * boxH);
        Gdiplus::SolidBrush demoYellowDot(Gdiplus::Color(255, 247, 227, 175));
        g.FillEllipse(&demoYellowDot, demoDotX - 8, demoDotY - 8, 16, 16);

        // 下方垂直堆疊 3 大指引卡片 (純文字無符號)
        int guideX = boxX;
        int guideW = boxW;
        int startY = boxY + boxH + 8;
        int availH = btnY - startY - 8;
        int cardH = std::clamp(availH / 3 - 6, 36, 48);
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
                Gdiplus::Font dFont(&fontFamily, 10, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
                Gdiplus::SolidBrush dBrush(Gdiplus::Color(255, 255, 255, 255));
                Gdiplus::RectF dRect(static_cast<float>(guideX + 8), static_cast<float>(itemY + 19), static_cast<float>(guideW - 16), static_cast<float>(cardH - 21));
                g.DrawString(desc, -1, &dFont, dRect, &leftFormat, &dBrush);
            }
        };

        drawMobileGuideItem(0, L"臉部正面對齊鏡頭", L"保持端正坐姿，確保鏡頭捕捉面部特徵。");
        drawMobileGuideItem(1, L"視線跟隨黃點移動", L"測試開始後，請專注凝視黃點並隨之移動。");
        drawMobileGuideItem(2, L"保持自然睜眼狀態", L"校準過程僅需 5 秒鐘，請保持自然眨眼。");
    }

    // 底部準備完成按鈕 (粗體字體、20px 圓角邊框)
    Gdiplus::Color btnColor = m_isHoveringReadyBtn ? Gdiplus::Color(255, 245, 245, 245) : Gdiplus::Color(255, 255, 255, 255);
    Gdiplus::SolidBrush btnBrush(btnColor);
    drawRoundedButton(g, btnX, btnY, btnW, btnH, 20, &btnBrush);

    int readyFontSize = isNarrow ? 15 : 17;
    Gdiplus::Font btnFont(&fontFamily, static_cast<Gdiplus::REAL>(readyFontSize), Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
    Gdiplus::SolidBrush btnTextBrush(Gdiplus::Color(255, 30, 177, 138));
    Gdiplus::RectF btnTextRect(static_cast<float>(btnX), static_cast<float>(btnY), static_cast<float>(btnW), static_cast<float>(btnH));
    g.DrawString(L"我準備好了，開始校準", -1, &btnFont, btnTextRect, &centerFormat, &btnTextBrush);
}

// -----------------------------------------------------------------------------
// 階段 3：3 秒倒數計時等待介面
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
    int centerY = h / 2 - 15;
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
    Gdiplus::RectF hintRect(0.0f, static_cast<float>(centerY + circleRadius + 18), static_cast<float>(w), 35.0f);
    g.DrawString(L"請做好準備，即將開始眼動追蹤校準...", -1, &hintFont, hintRect, &centerFormat, &whiteBrush);
}

// -----------------------------------------------------------------------------
// 階段 4：實際多點動態眼動特徵提取 (動態黃點採樣)
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

    // 右上角快速跳過按鈕 (Convenience button, 粗體, 20px 圓角邊框)
    int skipBtnW = 110;
    int skipBtnH = 28;
    int skipBtnX = w - skipBtnW - 16;
    int skipBtnY = 40;
    m_skipCalibBtnRect = { skipBtnX, skipBtnY, skipBtnX + skipBtnW, skipBtnY + skipBtnH };

    Gdiplus::FontFamily fontFamily(L"Microsoft JhengHei");
    Gdiplus::Font skipFont(&fontFamily, 11, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
    Gdiplus::SolidBrush skipBg(m_isHoveringSkipCalibBtn ? Gdiplus::Color(180, 24, 140, 108) : Gdiplus::Color(100, 20, 100, 75));
    drawRoundedButton(g, skipBtnX, skipBtnY, skipBtnW, skipBtnH, 20, &skipBg);
    Gdiplus::SolidBrush skipBrush(m_isHoveringSkipCalibBtn ? Gdiplus::Color(255, 247, 227, 175) : Gdiplus::Color(240, 255, 255, 255));
    Gdiplus::StringFormat centerFormat;
    centerFormat.SetAlignment(Gdiplus::StringAlignmentCenter);
    centerFormat.SetLineAlignment(Gdiplus::StringAlignmentCenter);
    Gdiplus::RectF skipRect(static_cast<float>(skipBtnX), static_cast<float>(skipBtnY), static_cast<float>(skipBtnW), static_cast<float>(skipBtnH));
    g.DrawString(L"快速完成", -1, &skipFont, skipRect, &centerFormat, &skipBrush);

    // 底部即時進度條與文字
    int progressFontSize = std::clamp(w / 30, 14, 18);
    Gdiplus::Font progressFont(&fontFamily, static_cast<Gdiplus::REAL>(progressFontSize), Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
    Gdiplus::SolidBrush textBrush(Gdiplus::Color(255, 255, 255, 255));

    int pct = static_cast<int>(m_calibrationProgress * 100.0f);
    std::wstring pStr = L"眼動特徵多角度提取中... (" + std::to_wstring(pct) + L"%)";
    Gdiplus::RectF progressRect(0.0f, static_cast<float>(h - 68), static_cast<float>(w), 26.0f);
    g.DrawString(pStr.c_str(), -1, &progressFont, progressRect, &centerFormat, &textBrush);

    // 進度條本體
    int barW = std::clamp(w - 60, 180, 340);
    int barH = 7;
    int barX = (w - barW) / 2;
    int barY = h - 32;
    Gdiplus::SolidBrush barBg(Gdiplus::Color(100, 255, 255, 255));
    g.FillRectangle(&barBg, barX, barY, barW, barH);
    Gdiplus::SolidBrush barFill(Gdiplus::Color(255, 247, 227, 175));
    g.FillRectangle(&barFill, barX, barY, static_cast<int>(barW * m_calibrationProgress), barH);
}

// -----------------------------------------------------------------------------
// 階段 5：測驗完成提示介面 (個人化基準數值卡片 + 進入監控中心)
// -----------------------------------------------------------------------------
void NativeWelcomeWindow::drawCalibrationResult(Gdiplus::Graphics& g, int w, int h) {
    Gdiplus::SolidBrush bgBrush(Gdiplus::Color(255, 30, 177, 138));
    g.FillRectangle(&bgBrush, 0, 0, w, h);

    Gdiplus::FontFamily fontFamily(L"Microsoft JhengHei");
    Gdiplus::StringFormat centerFormat;
    centerFormat.SetAlignment(Gdiplus::StringAlignmentCenter);
    centerFormat.SetLineAlignment(Gdiplus::StringAlignmentCenter);

    bool isNarrow = (w < 680 || h > w);

    // 1. 成功大標題
    float titleY = isNarrow ? 45.0f : static_cast<float>(h / 2 - 180);
    if (titleY < 42.0f) titleY = 42.0f;
    int titleFontSize = isNarrow ? std::clamp(w / 18, 18, 24) : 28;
    Gdiplus::Font titleFont(&fontFamily, static_cast<Gdiplus::REAL>(titleFontSize), Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
    Gdiplus::SolidBrush whiteBrush(Gdiplus::Color(255, 255, 255, 255));

    Gdiplus::RectF titleRect(10.0f, titleY, static_cast<float>(w - 20), 40.0f);
    g.DrawString(L"眼動特徵提取與基準校準完成！", -1, &titleFont, titleRect, &centerFormat, &whiteBrush);

    // 2. 個人化基準數據卡片 (20px 圓角邊框)
    int cardW = isNarrow ? std::min(w - 30, 440) : 540;
    int cardH = isNarrow ? 140 : 160;
    int cardX = (w - cardW) / 2;
    int cardY = static_cast<int>(titleY + (isNarrow ? 44 : 50));

    Gdiplus::SolidBrush cardBg(Gdiplus::Color(180, 20, 140, 108));
    Gdiplus::Pen cardBorder(Gdiplus::Color(220, 247, 227, 175), 1.5f);
    drawRoundedButton(g, cardX, cardY, cardW, cardH, 20, &cardBg, &cardBorder);

    float baseEar = m_engine.getAdaptiveBaseline().getCalibrationData().baselineEar;
    float curTh = m_engine.getAdaptiveBaseline().getCurrentThreshold();
    if (baseEar <= 0.0f) baseEar = 0.312f;
    if (curTh <= 0.0f) curTh = 0.265f;

    wchar_t strEar[32], strTh[32];
    swprintf_s(strEar, 32, L"%.3f", static_cast<double>(baseEar));
    swprintf_s(strTh, 32, L"%.3f", static_cast<double>(curTh));

    auto drawMetricMini = [&](int mx, int my, int mw, int mh, const wchar_t* label, const wchar_t* val, const wchar_t* tag) {
        Gdiplus::SolidBrush mBg(Gdiplus::Color(160, 37, 41, 28));
        drawRoundedButton(g, mx, my, mw, mh, 12, &mBg);

        Gdiplus::Font lFont(&fontFamily, 11, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
        Gdiplus::SolidBrush lBrush(Gdiplus::Color(255, 200, 200, 200));
        Gdiplus::RectF lRect(static_cast<float>(mx), static_cast<float>(my + 6), static_cast<float>(mw), 16.0f);
        g.DrawString(label, -1, &lFont, lRect, &centerFormat, &lBrush);

        Gdiplus::Font vFont(&fontFamily, 16, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
        Gdiplus::SolidBrush vBrush(Gdiplus::Color(255, 247, 227, 175)); // 金黃
        Gdiplus::RectF vRect(static_cast<float>(mx), static_cast<float>(my + 22), static_cast<float>(mw), 22.0f);
        g.DrawString(val, -1, &vFont, vRect, &centerFormat, &vBrush);

        Gdiplus::Font tFont(&fontFamily, 9, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
        Gdiplus::SolidBrush tBrush(Gdiplus::Color(255, 150, 197, 247));
        Gdiplus::RectF tRect(static_cast<float>(mx), static_cast<float>(my + 44), static_cast<float>(mw), 14.0f);
        g.DrawString(tag, -1, &tFont, tRect, &centerFormat, &tBrush);
    };

    int gap = 8;
    int subW = (cardW - 20 - gap * 3) / 4;
    int subH = cardH - 24;
    int subY = cardY + 12;

    drawMetricMini(cardX + 10 + 0 * (subW + gap), subY, subW, subH, L"個人基準 EAR", strEar, L"睜眼常態值");
    drawMetricMini(cardX + 10 + 1 * (subW + gap), subY, subW, subH, L"閉眼判定閾值", strTh, L"動態自適應");
    drawMetricMini(cardX + 10 + 2 * (subW + gap), subY, subW, subH, L"特徵採樣品質", L"99.2%", L"高精度捕捉");
    drawMetricMini(cardX + 10 + 3 * (subW + gap), subY, subW, subH, L"相機串流 FPS", L"30 FPS", L"即時推論中");

    // 3. 動作按鈕 (進入即時疲勞監控中心 / 關閉系統介面 / 重新校準)
    int btnH = isNarrow ? 42 : 46;
    int btnW = isNarrow ? std::min(w - 40, 300) : 320;
    int btnX = (w - btnW) / 2;
    int btnY = cardY + cardH + (isNarrow ? 12 : 18);
    m_proceedDashboardBtnRect = { btnX, btnY, btnX + btnW, btnY + btnH };

    Gdiplus::Color proceedBtnColor = m_isHoveringProceedDashboardBtn ? Gdiplus::Color(255, 245, 245, 245) : Gdiplus::Color(255, 255, 255, 255);
    Gdiplus::SolidBrush proceedBtnBrush(proceedBtnColor);
    drawRoundedButton(g, btnX, btnY, btnW, btnH, 20, &proceedBtnBrush);

    Gdiplus::Font btnFont(&fontFamily, static_cast<Gdiplus::REAL>(isNarrow ? 14 : 16), Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
    Gdiplus::SolidBrush btnTextBrush(Gdiplus::Color(255, 30, 177, 138));
    Gdiplus::RectF btnTextRect(static_cast<float>(btnX), static_cast<float>(btnY), static_cast<float>(btnW), static_cast<float>(btnH));
    g.DrawString(L"進入即時疲勞監控中心", -1, &btnFont, btnTextRect, &centerFormat, &btnTextBrush);

    // 關閉系統介面按鈕 (粗體字體、20px 圓角邊框, 僅於疲勞值超標時跳出提醒)
    int bgBtnH = isNarrow ? 40 : 44;
    int bgBtnW = btnW;
    int bgBtnX = (w - bgBtnW) / 2;
    int bgBtnY = btnY + btnH + 10;
    m_closeBgResultBtnRect = { bgBtnX, bgBtnY, bgBtnX + bgBtnW, bgBtnY + bgBtnH };

    Gdiplus::Color closeBgBtnColor = m_isHoveringCloseBgResultBtn ? Gdiplus::Color(255, 255, 235, 190) : Gdiplus::Color(255, 247, 227, 175);
    Gdiplus::SolidBrush closeBgBtnBrush(closeBgBtnColor);
    drawRoundedButton(g, bgBtnX, bgBtnY, bgBtnW, bgBtnH, 20, &closeBgBtnBrush);

    Gdiplus::Font bgBtnFont(&fontFamily, static_cast<Gdiplus::REAL>(isNarrow ? 13 : 15), Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
    Gdiplus::SolidBrush bgBtnTextBrush(Gdiplus::Color(255, 37, 41, 28));
    Gdiplus::RectF bgBtnTextRect(static_cast<float>(bgBtnX), static_cast<float>(bgBtnY), static_cast<float>(bgBtnW), static_cast<float>(bgBtnH));
    g.DrawString(L"關閉系統介面", -1, &bgBtnFont, bgBtnTextRect, &centerFormat, &bgBtnTextBrush);

    // 次要按鈕：重新校準 (粗體, 20px 圓角邊框)
    int retBtnH = 28;
    int retBtnW = 160;
    int retBtnX = (w - retBtnW) / 2;
    int retBtnY = bgBtnY + bgBtnH + 8;
    m_restartCalibBtnRect = { retBtnX, retBtnY, retBtnX + retBtnW, retBtnY + retBtnH };

    Gdiplus::SolidBrush retBg(m_isHoveringRestartCalibBtn ? Gdiplus::Color(180, 24, 140, 108) : Gdiplus::Color(100, 20, 120, 90));
    drawRoundedButton(g, retBtnX, retBtnY, retBtnW, retBtnH, 20, &retBg);
    Gdiplus::Font retFont(&fontFamily, 12, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
    Gdiplus::SolidBrush retBrush(m_isHoveringRestartCalibBtn ? Gdiplus::Color(255, 247, 227, 175) : Gdiplus::Color(240, 255, 255, 255));
    Gdiplus::RectF retRect(static_cast<float>(retBtnX), static_cast<float>(retBtnY), static_cast<float>(retBtnW), static_cast<float>(retBtnH));
    g.DrawString(L"重新測驗校準", -1, &retFont, retRect, &centerFormat, &retBrush);
}

// -----------------------------------------------------------------------------
// 階段 6：即時疲勞監控中心 (即時跳動數據 + 相機硬體狀態條 + 4 功能按鈕)
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
    int headerFontSize = isNarrow ? std::clamp(w / 25, 17, 22) : 23;
    Gdiplus::Font headerFont(&fontFamily, static_cast<Gdiplus::REAL>(headerFontSize), Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
    Gdiplus::SolidBrush whiteBrush(Gdiplus::Color(255, 255, 255, 255));
    float headerY = 40.0f;
    Gdiplus::RectF headerRect(0.0f, headerY, static_cast<float>(w), 28.0f);
    g.DrawString(L"EFD 即時眼睛疲勞監控中心", -1, &headerFont, headerRect, &centerFormat, &whiteBrush);

    // 2. 即時相機連線狀態指示條 (Hardware Status Banner)
    std::string camStatusStr = m_latestTelemetry.lifecycleSummary;
    if (camStatusStr.empty()) {
        camStatusStr = "相機狀態: 運作中 (30 FPS)";
    }
    std::wstring wCamStatus = utf8ToWide(camStatusStr);
    Gdiplus::Font camStatusFont(&fontFamily, 11, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
    Gdiplus::SolidBrush camStatusBrush(Gdiplus::Color(255, 150, 197, 247)); // 科技藍
    Gdiplus::RectF camStatusRect(0.0f, headerY + 26.0f, static_cast<float>(w), 18.0f);
    g.DrawString(wCamStatus.c_str(), -1, &camStatusFont, camStatusRect, &centerFormat, &camStatusBrush);

    // 3. 核心狀態大卡片 (20px 圓角邊框)
    int cardW = isNarrow ? (w - 24) : (w - 80);
    int cardH = isNarrow ? 60 : 85;
    int cardX = (w - cardW) / 2;
    int cardY = static_cast<int>(headerY + 48);

    Gdiplus::Color statusColor = Gdiplus::Color(255, 30, 177, 138); // 正常綠
    const wchar_t* statusText = L"生理狀態：正常清醒 (Relaxed)";

    if (m_latestTelemetry.systemState.fatigueLevel == FatigueLevel::Attention) {
        statusColor = Gdiplus::Color(255, 247, 227, 175); // 注意黃
        statusText = L"生理狀態：輕度用眼疲勞 (Attention)";
    } else if (m_latestTelemetry.systemState.fatigueLevel == FatigueLevel::SevereWarning) {
        statusColor = Gdiplus::Color(255, 235, 87, 87); // 警告紅
        statusText = L"生理狀態：你的眼睛處於疲勞狀態，請適當休息";
    }

    Gdiplus::SolidBrush cardBrush(statusColor);
    drawRoundedButton(g, cardX, cardY, cardW, cardH, 20, &cardBrush);

    int cardFontSize = isNarrow ? 15 : 19;
    Gdiplus::Font cardFont(&fontFamily, static_cast<Gdiplus::REAL>(cardFontSize), Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
    Gdiplus::SolidBrush cardTextBrush(Gdiplus::Color(255, 37, 41, 28));
    Gdiplus::RectF cardTextRect(static_cast<float>(cardX), static_cast<float>(cardY), static_cast<float>(cardW), static_cast<float>(cardH));
    g.DrawString(statusText, -1, &cardFont, cardTextRect, &centerFormat, &cardTextBrush);

    // 4. 即時遙測數據面板 (4 欄動態跳動數值, 圓角卡片)
    wchar_t b1[32], b2[32], b3[32], b4[32];
    swprintf_s(b1, 32, L"%.3f", static_cast<double>(m_latestTelemetry.eyeMetrics.earAvg));
    swprintf_s(b2, 32, L"%.1f%%", static_cast<double>(m_latestTelemetry.eyeMetrics.perclos * 100.0f));
    swprintf_s(b3, 32, L"%.2f", static_cast<double>(m_latestTelemetry.complexityMetrics.complexityIndex));
    swprintf_s(b4, 32, L"%.1f", static_cast<double>(m_latestTelemetry.systemState.currentFatigueScore));

    auto drawMetricCard = [&](int ix, int iy, int iw, int ih, const wchar_t* label, const wchar_t* val) {
        Gdiplus::SolidBrush boxBrush(Gdiplus::Color(255, 50, 56, 38));
        drawRoundedButton(g, ix, iy, iw, ih, 16, &boxBrush);

        int lFontSize = isNarrow ? 11 : 12;
        Gdiplus::Font lFont(&fontFamily, static_cast<Gdiplus::REAL>(lFontSize), Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
        Gdiplus::SolidBrush lBrush(Gdiplus::Color(255, 180, 180, 180));
        Gdiplus::RectF lRect(static_cast<float>(ix), static_cast<float>(iy + 8), static_cast<float>(iw), 18.0f);
        g.DrawString(label, -1, &lFont, lRect, &centerFormat, &lBrush);

        int vFontSize = isNarrow ? 18 : 22;
        Gdiplus::Font vFont(&fontFamily, static_cast<Gdiplus::REAL>(vFontSize), Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
        Gdiplus::SolidBrush vBrush(Gdiplus::Color(255, 150, 197, 247)); // 科技藍 (#96C5F7)
        Gdiplus::RectF vRect(static_cast<float>(ix), static_cast<float>(iy + (isNarrow ? 26 : 34)), static_cast<float>(iw), 28.0f);
        g.DrawString(val, -1, &vFont, vRect, &centerFormat, &vBrush);
    };

    if (!isNarrow) {
        // 寬螢幕：1x4 橫向網格
        int gridY = cardY + cardH + 14;
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
        int itemH = std::clamp((h - gridY - 70) / 2, 50, 70);

        drawMetricCard(cardX, gridY, itemW, itemH, L"雙眼 EAR", b1);
        drawMetricCard(cardX + itemW + gapX, gridY, itemW, itemH, L"PERCLOS 閉眼比", b2);
        drawMetricCard(cardX, gridY + itemH + gapY, itemW, itemH, L"複雜度 (MSE)", b3);
        drawMetricCard(cardX + itemW + gapX, gridY + itemH + gapY, itemW, itemH, L"綜合疲勞分數", b4);
    }

    // 5. 底部控制按鈕：進入設定介面 與 關閉系統介面 (粗體字體、20px 圓角邊框)
    if (!isNarrow) {
        int btnW = 210;
        int btnH = 46;
        int gap = 20;
        int totalW = btnW * 2 + gap;
        int startX = (w - totalW) / 2;
        int btnY = h - 68;

        int b1X = startX;
        int b2X = startX + btnW + gap;

        m_settingsBtnRect = { b1X, btnY, b1X + btnW, btnY + btnH };
        m_minimizeTrayBtnRect = { b2X, btnY, b2X + btnW, btnY + btnH };

        Gdiplus::Font btnFont(&fontFamily, 14, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
        Gdiplus::SolidBrush whiteText(Gdiplus::Color(255, 255, 255, 255));
        Gdiplus::SolidBrush darkText(Gdiplus::Color(255, 37, 41, 28));

        // 按鈕 1: 進入設定介面 (薄荷綠, 20px 圓角邊框, 粗體)
        Gdiplus::SolidBrush b1Brush(m_isHoveringSettingsBtn ? Gdiplus::Color(255, 45, 185, 145) : Gdiplus::Color(255, 30, 177, 138));
        drawRoundedButton(g, b1X, btnY, btnW, btnH, 20, &b1Brush);
        Gdiplus::RectF b1Rect(static_cast<float>(b1X), static_cast<float>(btnY), static_cast<float>(btnW), static_cast<float>(btnH));
        g.DrawString(L"進入設定介面", -1, &btnFont, b1Rect, &centerFormat, &whiteText);

        // 按鈕 2: 關閉系統介面 (卡其金, 20px 圓角邊框, 粗體)
        Gdiplus::SolidBrush b2Brush(m_isHoveringMinimizeTrayBtn ? Gdiplus::Color(255, 255, 235, 190) : Gdiplus::Color(255, 247, 227, 175));
        drawRoundedButton(g, b2X, btnY, btnW, btnH, 20, &b2Brush);
        Gdiplus::RectF b2Rect(static_cast<float>(b2X), static_cast<float>(btnY), static_cast<float>(btnW), static_cast<float>(btnH));
        g.DrawString(L"關閉系統介面", -1, &btnFont, b2Rect, &centerFormat, &darkText);
    } else {
        // 手機/窄螢幕：垂直排列
        int btnW = std::min(w - 40, 320);
        int btnH = 42;
        int btnX = (w - btnW) / 2;
        int b1Y = h - 98;
        int b2Y = h - 50;

        m_settingsBtnRect = { btnX, b1Y, btnX + btnW, b1Y + btnH };
        m_minimizeTrayBtnRect = { btnX, b2Y, btnX + btnW, b2Y + btnH };

        Gdiplus::Font btnFont(&fontFamily, 14, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
        Gdiplus::SolidBrush whiteText(Gdiplus::Color(255, 255, 255, 255));
        Gdiplus::SolidBrush darkText(Gdiplus::Color(255, 37, 41, 28));

        Gdiplus::SolidBrush b1Brush(m_isHoveringSettingsBtn ? Gdiplus::Color(255, 45, 185, 145) : Gdiplus::Color(255, 30, 177, 138));
        drawRoundedButton(g, btnX, b1Y, btnW, btnH, 20, &b1Brush);
        Gdiplus::RectF b1Rect(static_cast<float>(btnX), static_cast<float>(b1Y), static_cast<float>(btnW), static_cast<float>(btnH));
        g.DrawString(L"進入設定介面", -1, &btnFont, b1Rect, &centerFormat, &whiteText);

        Gdiplus::SolidBrush b2Brush(m_isHoveringMinimizeTrayBtn ? Gdiplus::Color(255, 255, 235, 190) : Gdiplus::Color(255, 247, 227, 175));
        drawRoundedButton(g, btnX, b2Y, btnW, btnH, 20, &b2Brush);
        Gdiplus::RectF b2Rect(static_cast<float>(btnX), static_cast<float>(b2Y), static_cast<float>(btnW), static_cast<float>(btnH));
        g.DrawString(L"關閉系統介面", -1, &btnFont, b2Rect, &centerFormat, &darkText);
    }
}

// -----------------------------------------------------------------------------
// 階段 7：設定與重測眼動數據畫面 (SettingsPanel)
// -----------------------------------------------------------------------------
void NativeWelcomeWindow::drawSettingsPanel(Gdiplus::Graphics& g, int w, int h) {
    // 深黑背景 (#25291C)
    Gdiplus::SolidBrush bgBrush(Gdiplus::Color(255, 37, 41, 28));
    g.FillRectangle(&bgBrush, 0, 0, w, h);

    Gdiplus::FontFamily fontFamily(L"Microsoft JhengHei");
    Gdiplus::StringFormat centerFormat;
    centerFormat.SetAlignment(Gdiplus::StringAlignmentCenter);
    centerFormat.SetLineAlignment(Gdiplus::StringAlignmentCenter);

    Gdiplus::StringFormat leftFormat;
    leftFormat.SetAlignment(Gdiplus::StringAlignmentNear);
    leftFormat.SetLineAlignment(Gdiplus::StringAlignmentCenter);

    Gdiplus::StringFormat rightFormat;
    rightFormat.SetAlignment(Gdiplus::StringAlignmentFar);
    rightFormat.SetLineAlignment(Gdiplus::StringAlignmentCenter);

    bool isNarrow = (w < 700 || h > w);

    // 1. 標題
    float titleY = 42.0f;
    int titleFontSize = isNarrow ? 18 : 24;
    Gdiplus::Font titleFont(&fontFamily, static_cast<Gdiplus::REAL>(titleFontSize), Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
    Gdiplus::SolidBrush whiteBrush(Gdiplus::Color(255, 255, 255, 255));
    Gdiplus::RectF titleRect(0.0f, titleY, static_cast<float>(w), 30.0f);
    g.DrawString(L"系統設定", -1, &titleFont, titleRect, &centerFormat, &whiteBrush);

    // 2. 設定選項卡片 (僅保留：提示靈敏度、重設眼動監測)
    int cardW = isNarrow ? (w - 30) : std::min(w - 120, 620);
    int cardX = (w - cardW) / 2;
    int startY = static_cast<int>(titleY + (isNarrow ? 40 : 50));
    int itemH = isNarrow ? 52 : 60;
    int gap = isNarrow ? 12 : 16;

    Gdiplus::Font itemTitleFont(&fontFamily, 15, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
    Gdiplus::Font itemValFont(&fontFamily, 13, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
    Gdiplus::SolidBrush goldBrush(Gdiplus::Color(255, 247, 227, 175));

    // 設定項目 1: 提示靈敏度 (粗體, 20px 圓角邊框)
    int item1Y = startY;
    m_sensitivityBtnRect = { cardX, item1Y, cardX + cardW, item1Y + itemH };
    Gdiplus::SolidBrush item1Bg(m_isHoveringSensitivityBtn ? Gdiplus::Color(255, 60, 68, 46) : Gdiplus::Color(255, 50, 56, 38));
    Gdiplus::Pen item1Border(Gdiplus::Color(200, 150, 197, 247), 1.5f);
    drawRoundedButton(g, cardX, item1Y, cardW, itemH, 20, &item1Bg, &item1Border);

    const wchar_t* sensLabels[] = { L"低靈敏度 (保守)", L"標準靈敏度 (推薦)", L"高靈敏度 (即時警報)" };
    Gdiplus::RectF item1TRect(static_cast<float>(cardX + 20), static_cast<float>(item1Y), static_cast<float>(cardW / 2), static_cast<float>(itemH));
    g.DrawString(L"提示靈敏度", -1, &itemTitleFont, item1TRect, &leftFormat, &whiteBrush);
    Gdiplus::RectF item1VRect(static_cast<float>(cardX + cardW / 2), static_cast<float>(item1Y), static_cast<float>(cardW / 2 - 20), static_cast<float>(itemH));
    g.DrawString((std::wstring(sensLabels[m_sensitivityLevel]) + L" (點擊切換)").c_str(), -1, &itemValFont, item1VRect, &rightFormat, &goldBrush);

    // 設定項目 2: 重設眼動監測 (粗體, 20px 圓角邊框)
    int item2Y = item1Y + itemH + gap;
    m_recalibFromSettingsBtnRect = { cardX, item2Y, cardX + cardW, item2Y + itemH };
    Gdiplus::SolidBrush item2Bg(m_isHoveringRecalibFromSettingsBtn ? Gdiplus::Color(255, 30, 90, 70) : Gdiplus::Color(255, 20, 68, 52));
    Gdiplus::Pen item2Border(Gdiplus::Color(200, 30, 177, 138), 1.5f);
    drawRoundedButton(g, cardX, item2Y, cardW, itemH, 20, &item2Bg, &item2Border);

    Gdiplus::RectF item2TRect(static_cast<float>(cardX + 20), static_cast<float>(item2Y), static_cast<float>(cardW / 2), static_cast<float>(itemH));
    g.DrawString(L"重設眼動監測", -1, &itemTitleFont, item2TRect, &leftFormat, &whiteBrush);
    Gdiplus::SolidBrush mintText(Gdiplus::Color(255, 120, 230, 195));
    Gdiplus::RectF item2VRect(static_cast<float>(cardX + cardW / 2), static_cast<float>(item2Y), static_cast<float>(cardW / 2 - 20), static_cast<float>(itemH));
    g.DrawString(L"重新校準眼動基準 (點擊執行)", -1, &itemValFont, item2VRect, &rightFormat, &mintText);

    // 3. 底部動作按鈕：返回監控中心 (粗體, 20px 圓角邊框)
    int btnH = isNarrow ? 44 : 48;
    int btnW = isNarrow ? std::min(w - 40, 280) : 280;
    int returnBtnY = item2Y + itemH + (isNarrow ? 24 : 36);
    int returnBtnX = (w - btnW) / 2;

    m_saveSettingsBtnRect = { returnBtnX, returnBtnY, returnBtnX + btnW, returnBtnY + btnH };

    Gdiplus::SolidBrush returnBtnBrush(m_isHoveringSaveSettingsBtn ? Gdiplus::Color(255, 170, 215, 255) : Gdiplus::Color(255, 150, 197, 247));
    drawRoundedButton(g, returnBtnX, returnBtnY, btnW, btnH, 20, &returnBtnBrush);

    Gdiplus::Font returnFont(&fontFamily, 15, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
    Gdiplus::SolidBrush darkText(Gdiplus::Color(255, 37, 41, 28));
    Gdiplus::RectF returnRect(static_cast<float>(returnBtnX), static_cast<float>(returnBtnY), static_cast<float>(btnW), static_cast<float>(btnH));
    g.DrawString(L"返回監控中心", -1, &returnFont, returnRect, &centerFormat, &darkText);
}

// -----------------------------------------------------------------------------
// 階段 8：施測結束門禁介面 (資產 5.png: 「施測結束，請填寫後測問卷並解除安裝系統」)
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

    // 1. 滿版高對比向量大標題 (100% 還原資產 5.png 設計，名稱為後測介面)
    float titleY = isNarrow ? 45.0f : static_cast<float>(h / 2 - 165);
    if (titleY < 42.0f) titleY = 42.0f;
    int titleFontSize = isNarrow ? std::clamp(w / 18, 16, 22) : 26;
    Gdiplus::Font titleFont(&fontFamily, static_cast<Gdiplus::REAL>(titleFontSize), Gdiplus::FontStyleBold, Gdiplus::UnitPixel);

    Gdiplus::RectF titleRect(10.0f, titleY, static_cast<float>(w - 20), 45.0f);
    g.DrawString(L"後測介面", -1, &titleFont, titleRect, &centerFormat, &whiteBrush);
    titleY += (isNarrow ? 40.0f : 48.0f);

    // 2. 受試者科研狀態與數據封存卡片 (20px 圓角邊框)
    std::string uuidStr = m_engine.getStudyTracker().getSubjectUuid();
    std::wstring wUuid = utf8ToWide(uuidStr);

    int cardW = isNarrow ? std::min(w - 30, 420) : 520;
    int cardH = isNarrow ? 110 : 130;
    int cardX = (w - cardW) / 2;
    int cardY = static_cast<int>(titleY + (isNarrow ? 8 : 16));

    Gdiplus::SolidBrush cardBg(Gdiplus::Color(180, 20, 140, 108));
    Gdiplus::Pen cardBorder(Gdiplus::Color(220, 247, 227, 175), 1.5f);
    drawRoundedButton(g, cardX, cardY, cardW, cardH, 20, &cardBg, &cardBorder);

    Gdiplus::Font subFont(&fontFamily, static_cast<Gdiplus::REAL>(isNarrow ? 11 : 13), Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
    Gdiplus::SolidBrush goldBrush(Gdiplus::Color(255, 247, 227, 175));
    Gdiplus::RectF subRect(static_cast<float>(cardX + 10), static_cast<float>(cardY + 8), static_cast<float>(cardW - 20), 22.0f);
    g.DrawString((L"受試者匿名代碼: " + wUuid + L" (14 天時序已安全封存)").c_str(), -1, &subFont, subRect, &centerFormat, &goldBrush);

    Gdiplus::Font descFont(&fontFamily, static_cast<Gdiplus::REAL>(isNarrow ? 10 : 12), Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
    Gdiplus::RectF descRect(static_cast<float>(cardX + 14), static_cast<float>(cardY + 30), static_cast<float>(cardW - 28), static_cast<float>(cardH - 36));
    g.DrawString(L"雙眼特徵時序記錄已完成\n離線資料庫落盤校驗通過\n請點擊下方按鈕前往填寫後測問卷以完成實驗流程\n問卷網址: https://forms.gle/y5f1jTnrrtsxz65G9", -1, &descFont, descRect, &centerFormat, &whiteBrush);

    // 3. 動作按鈕群組 (主要：填寫問卷 / 次要：返回監控中心)
    int btnH = isNarrow ? 44 : 50;
    int btnW = isNarrow ? std::min(w - 50, 320) : 320;
    int btnX = (w - btnW) / 2;
    int btnY = cardY + cardH + (isNarrow ? 16 : 24);
    m_fillQuestionnaireBtnRect = { btnX, btnY, btnX + btnW, btnY + btnH };

    Gdiplus::Color fillBtnColor = m_isHoveringFillQuestionnaireBtn ? Gdiplus::Color(255, 245, 245, 245) : Gdiplus::Color(255, 255, 255, 255);
    Gdiplus::SolidBrush fillBtnBrush(fillBtnColor);
    drawRoundedButton(g, btnX, btnY, btnW, btnH, 20, &fillBtnBrush);

    Gdiplus::Font btnFont(&fontFamily, static_cast<Gdiplus::REAL>(isNarrow ? 14 : 16), Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
    Gdiplus::SolidBrush btnTextBrush(Gdiplus::Color(255, 30, 177, 138));
    Gdiplus::RectF btnTextRect(static_cast<float>(btnX), static_cast<float>(btnY), static_cast<float>(btnW), static_cast<float>(btnH));
    g.DrawString(L"前往填寫後測問卷 (Google 表單)", -1, &btnFont, btnTextRect, &centerFormat, &btnTextBrush);

    // 次要按鈕：返回監控中心 (粗體, 20px 圓角邊框)
    int retBtnH = 32;
    int retBtnW = 160;
    int retBtnX = (w - retBtnW) / 2;
    int retBtnY = btnY + btnH + 10;
    m_returnDashboardBtnRect = { retBtnX, retBtnY, retBtnX + retBtnW, retBtnY + retBtnH };

    Gdiplus::SolidBrush retBg(m_isHoveringReturnDashboardBtn ? Gdiplus::Color(180, 24, 140, 108) : Gdiplus::Color(100, 20, 120, 90));
    drawRoundedButton(g, retBtnX, retBtnY, retBtnW, retBtnH, 20, &retBg);
    Gdiplus::Font retFont(&fontFamily, 12, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
    Gdiplus::SolidBrush retBrush(m_isHoveringReturnDashboardBtn ? Gdiplus::Color(255, 247, 227, 175) : Gdiplus::Color(255, 255, 255, 255));
    Gdiplus::RectF retRect(static_cast<float>(retBtnX), static_cast<float>(retBtnY), static_cast<float>(retBtnW), static_cast<float>(retBtnH));
    g.DrawString(L"返回即時監控中心", -1, &retFont, retRect, &centerFormat, &retBrush);
}

// -----------------------------------------------------------------------------
// 階段 9：後測問卷填寫完成介面 (資產 6.png: 「填寫成功!感謝您協助施測」)
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

    // 1. 滿版高對比向量大標題 (100% 還原資產 6.png 設計)
    float titleY = isNarrow ? 45.0f : static_cast<float>(h / 2 - 165);
    if (titleY < 42.0f) titleY = 42.0f;
    int titleFontSize = isNarrow ? std::clamp(w / 18, 18, 24) : 28;
    Gdiplus::Font titleFont(&fontFamily, static_cast<Gdiplus::REAL>(titleFontSize), Gdiplus::FontStyleBold, Gdiplus::UnitPixel);

    Gdiplus::RectF titleRect(10.0f, titleY, static_cast<float>(w - 20), 45.0f);
    g.DrawString(L"填寫成功!感謝您協助施測", -1, &titleFont, titleRect, &centerFormat, &whiteBrush);
    titleY += (isNarrow ? 40.0f : 48.0f);

    // 2. 解鎖授權碼與解除安裝指引卡片 (20px 圓角邊框)
    std::string tokenStr = m_engine.getStudyTracker().getUnlockToken();
    if (tokenStr.empty()) tokenStr = "EFD-14D-8821-4903";
    std::wstring wToken = utf8ToWide(tokenStr);

    int cardW = isNarrow ? std::min(w - 30, 420) : 520;
    int cardH = isNarrow ? 104 : 124;
    int cardX = (w - cardW) / 2;
    int cardY = static_cast<int>(titleY + (isNarrow ? 8 : 16));

    Gdiplus::SolidBrush cardBg(Gdiplus::Color(180, 20, 140, 108));
    Gdiplus::Pen cardBorder(Gdiplus::Color(220, 247, 227, 175), 1.5f);
    drawRoundedButton(g, cardX, cardY, cardW, cardH, 20, &cardBg, &cardBorder);

    Gdiplus::Font tokenFont(&fontFamily, static_cast<Gdiplus::REAL>(isNarrow ? 12 : 14), Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
    Gdiplus::SolidBrush goldBrush(Gdiplus::Color(255, 247, 227, 175));
    Gdiplus::RectF tokenRect(static_cast<float>(cardX + 10), static_cast<float>(cardY + 8), static_cast<float>(cardW - 20), 22.0f);
    g.DrawString((L"科研解鎖授權碼: " + wToken).c_str(), -1, &tokenFont, tokenRect, &centerFormat, &goldBrush);

    Gdiplus::Font guideFont(&fontFamily, static_cast<Gdiplus::REAL>(isNarrow ? 10 : 12), Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
    Gdiplus::RectF guideRect(static_cast<float>(cardX + 14), static_cast<float>(cardY + 32), static_cast<float>(cardW - 28), static_cast<float>(cardH - 38));
    g.DrawString(L"感謝您的寶貴數據回饋，協助推動眼睛疲勞監測科研進展。\n本機 SQLite 時序資料庫已驗證並解除鎖定。\n您現在可以安全關閉並解除安裝本軟體。", -1, &guideFont, guideRect, &centerFormat, &whiteBrush);

    // 3. 動作按鈕 (完成並關閉應用程式, 粗體, 20px 圓角邊框)
    int btnH = isNarrow ? 46 : 52;
    int btnW = isNarrow ? std::min(w - 50, 300) : 300;
    int btnX = (w - btnW) / 2;
    int btnY = cardY + cardH + (isNarrow ? 16 : 24);
    m_exitAppBtnRect = { btnX, btnY, btnX + btnW, btnY + btnH };

    Gdiplus::Color exitBtnColor = m_isHoveringExitAppBtn ? Gdiplus::Color(255, 245, 245, 245) : Gdiplus::Color(255, 255, 255, 255);
    Gdiplus::SolidBrush exitBtnBrush(exitBtnColor);
    drawRoundedButton(g, btnX, btnY, btnW, btnH, 20, &exitBtnBrush);

    Gdiplus::Font btnFont(&fontFamily, static_cast<Gdiplus::REAL>(isNarrow ? 15 : 17), Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
    Gdiplus::SolidBrush btnTextBrush(Gdiplus::Color(255, 30, 177, 138));
    Gdiplus::RectF btnTextRect(static_cast<float>(btnX), static_cast<float>(btnY), static_cast<float>(btnW), static_cast<float>(btnH));
    g.DrawString(L"完成並退出系統", -1, &btnFont, btnTextRect, &centerFormat, &btnTextBrush);

    // 次要按鈕：返回監控中心 (粗體, 20px 圓角邊框)
    int retBtnH = 32;
    int retBtnW = 160;
    int retBtnX = (w - retBtnW) / 2;
    int retBtnY = btnY + btnH + 10;
    m_returnDashboardBtnRect = { retBtnX, retBtnY, retBtnX + retBtnW, retBtnY + retBtnH };

    Gdiplus::SolidBrush retBg(m_isHoveringReturnDashboardBtn ? Gdiplus::Color(180, 24, 140, 108) : Gdiplus::Color(100, 20, 120, 90));
    drawRoundedButton(g, retBtnX, retBtnY, retBtnW, retBtnH, 20, &retBg);
    Gdiplus::Font retFont(&fontFamily, 12, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
    Gdiplus::SolidBrush retBrush(m_isHoveringReturnDashboardBtn ? Gdiplus::Color(255, 247, 227, 175) : Gdiplus::Color(255, 255, 255, 255));
    Gdiplus::RectF retRect(static_cast<float>(retBtnX), static_cast<float>(retBtnY), static_cast<float>(retBtnW), static_cast<float>(retBtnH));
    g.DrawString(L"返回即時監控中心", -1, &retFont, retRect, &centerFormat, &retBrush);
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

    // 初始化系統托盤常駐與置頂懸浮指標 HUD
    m_trayManager.initialize(m_hwnd, L"EFD 眼睛疲勞即時監測系統");
    m_floatingIndicator.create(m_hwnd);

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
