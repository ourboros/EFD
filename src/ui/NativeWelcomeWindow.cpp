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

    // 載入設計資產 (歡迎介面 Logo: 保持套用 E:\Project\EFD\design\1x\資產 9.png)
    m_logoImage = std::make_unique<Gdiplus::Image>(L"E:\\Project\\EFD\\design\\1x\\資產 9.png");
    if (!m_logoImage || m_logoImage->GetLastStatus() != Gdiplus::Ok || m_logoImage->GetWidth() == 0) {
        m_logoImage = loadAssetImage(L"資產 9.png");
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

    // 綁定五執行緒引擎遙測事件 (即時更新 HUD、托盤與終端機控制台)
    static int s_telemetryLogCount = 0;
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

        // 在一開始 (歡迎、預覽說明、倒數、校準階段) 持續鎖定並學習使用者的初始坐姿偏向位置
        if (t.detection.hasFace && t.presenceDiagnostic.trackedCx > 0.0f) {
            float curCx = t.presenceDiagnostic.trackedCx;
            float curCy = t.presenceDiagnostic.trackedCy;
            if (!this->m_userPositionBiasDetected) {
                this->m_calibratedUserBiasX = curCx;
                this->m_calibratedUserBiasY = curCy;
                this->m_userPositionBiasDetected = true;
            } else {
                this->m_calibratedUserBiasX = this->m_calibratedUserBiasX * 0.85f + curCx * 0.15f;
                this->m_calibratedUserBiasY = this->m_calibratedUserBiasY * 0.85f + curCy * 0.15f;
            }
            this->m_engine.getFaceLandmarker().calibrateUserPositionBias(this->m_calibratedUserBiasX, this->m_calibratedUserBiasY);
        }

        // 初次測試階段 (歡迎、演示、倒數、校準完成前) 先不進行疲勞判定與離座警告，僅專注學習初始偏向位置
        if (this->m_currentStage != UIStage::MainDashboard) {
            int count = ++s_telemetryLogCount;
            if (count % 30 == 0) {
                if (this->m_userPositionBiasDetected) {
                    std::cout << "[初始位置學習] 影格: " << std::setw(5) << t.totalFramesProcessed
                              << " | 成功捕捉使用者視野自然偏向位置: (" 
                              << std::fixed << std::setprecision(1) << this->m_calibratedUserBiasX << ", " 
                              << this->m_calibratedUserBiasY << ") | 初始測試階段暫停疲勞計算\n";
                } else {
                    std::cout << "[初始位置學習] 影格: " << std::setw(5) << t.totalFramesProcessed
                              << " | 正在偵測鏡頭前使用者坐姿偏向位置... (暫停疲勞計算)\n";
                }
            }

            if (this->m_hwnd && (this->m_currentStage == UIStage::CalibrationInstruction || 
                                 this->m_currentStage == UIStage::ActiveCalibration ||
                                 this->m_currentStage == UIStage::SettingsPanel)) {
                InvalidateRect(this->m_hwnd, NULL, FALSE);
            }
            return;
        }

        // 控制台持續輸出最新眼動與疲勞遙測數據 (進入主儀表板後，每 15 幀或閉眼時輸出)
        int count = ++s_telemetryLogCount;
        if (count % 15 == 0 || t.eyeMetrics.isEyeClosed) {
            bool isPresent = (t.detection.hasFace && t.systemState.userPresent);
            bool inCooldown = (t.systemState.cooldownState == CooldownState::InCooldown);
            int cdRemaining = t.systemState.cooldownRemainingSeconds;
            int cdMins = cdRemaining / 60;
            int cdSecs = cdRemaining % 60;

            std::cout << "[即時眼動數據] 影格: " << std::setw(5) << t.totalFramesProcessed;
            if (!isPresent) {
                std::string reason = t.presenceDiagnostic.unconfirmedReason.empty() 
                    ? "未在視野內偵測到穩定人體/面部特徵" 
                    : t.presenceDiagnostic.unconfirmedReason;
                std::cout << " | 【是否在場: 否 (離座中)】"
                          << " | 【在場依據: " << reason << "】"
                          << " | 【疲勞依據: 暫停特徵計算 (離座防誤報)】\n";
            } else {
                std::cout << " | 【是否在場: 是 (動態跟隨)】";

                if (inCooldown) {
                    std::cout << " | 【是否疲勞: 20/5/5 冷卻暫停提醒中 (剩餘 " << cdMins << "分" << std::setw(2) << std::setfill('0') << cdSecs << "秒)】" << std::setfill(' ');
                } else if (t.systemState.fatigueLevel == FatigueLevel::SevereWarning || t.systemState.currentFatigueScore >= 65.0f) {
                    std::cout << " | 【是否疲勞: 是 (超標警報)】";
                } else if (t.systemState.fatigueLevel == FatigueLevel::Attention) {
                    std::cout << " | 【是否疲勞: 輕度 (注意力提醒)】";
                } else {
                    std::cout << " | 【是否疲勞: 否 (清醒放鬆)】";
                }

                std::cout << " | 【在場依據: 膚色點數 " << t.presenceDiagnostic.skinPixels << ">=" << t.presenceDiagnostic.minSkinRequired
                          << ", 對稱度 " << std::fixed << std::setprecision(2) << t.presenceDiagnostic.symmetryRatio << ">=0.06"
                          << ", 輪廓比例 " << std::setprecision(2) << t.presenceDiagnostic.aspectRatio << "】";

                std::cout << " | 【疲勞依據: EAR=" << std::fixed << std::setprecision(3) << t.eyeMetrics.earAvg
                          << (t.eyeMetrics.isEyeClosed ? " (閉眼)" : " (睜眼)")
                          << " (基準閾值 0.217), PERCLOS=" << std::setprecision(1) << (t.eyeMetrics.perclos * 100.0f) << "%"
                          << ", 眨眼=" << std::setprecision(1) << t.eyeMetrics.blinkRatePerMin << "次/分"
                          << ", MSE CI=" << std::setprecision(2) << t.complexityMetrics.complexityIndex
                          << ", 疲勞分=" << std::setprecision(1) << t.systemState.currentFatigueScore << "/65.0"
                          << ", 狀態: ";
                if (t.systemState.fatigueLevel == FatigueLevel::SevereWarning) std::cout << "嚴重疲勞警告 (SevereWarning)";
                else if (t.systemState.fatigueLevel == FatigueLevel::Attention) std::cout << "注意力提醒 (Attention)";
                else std::cout << "清醒放鬆 (Relaxed)";
                std::cout << "】\n";
            }
        }

        if (this->m_hwnd && (this->m_currentStage == UIStage::MainDashboard || 
                             this->m_currentStage == UIStage::CalibrationInstruction ||
                             this->m_currentStage == UIStage::SettingsPanel)) {
            InvalidateRect(this->m_hwnd, NULL, FALSE);
        }
    });

    m_engine.setAlertCallback([this](FatigueLevel level, float score, const std::string& msg) {
        (void)msg;
        // 僅於主儀表板階段觸發警報，且疲勞分數達到嚴重警告門檻 (>= 65.0)
        if (this->m_currentStage != UIStage::MainDashboard) {
            return;
        }

        auto now = std::chrono::steady_clock::now();
        auto elapsedSinceLastAlert = std::chrono::duration_cast<std::chrono::seconds>(now - this->m_lastAlertNotificationTime).count();

        // 智能防打擾節流：若距離上次提醒未滿 300 秒 (5 分鐘) 則靜默，不重複彈窗騷擾
        if (elapsedSinceLastAlert < 300 && this->m_lastAlertNotificationTime.time_since_epoch().count() > 0) {
            return;
        }

        if (level == FatigueLevel::SevereWarning || score >= 65.0f) {
            this->m_lastAlertNotificationTime = now;
            std::cout << "\n>>> [疲勞警報通知發送] 疲勞分數: " << std::fixed << std::setprecision(1) << score
                      << " (嚴重警告) - 你的眼睛處於疲勞狀態，請適當休息 (已啟動 20/5/5 靜默防打擾冷卻機制) <<<\n\n";

            std::ostringstream oss;
            oss << "疲勞指數 " << std::fixed << std::setprecision(1) << score << " - 你的眼睛處於疲勞狀態，請適當休息 (已進入 20 分鐘暫停提醒冷卻保護)";
            this->m_dashboardMessage = oss.str();
            
            // 發送 Windows 原生氣泡/Toast 警報通知 (純文字無符號表情)
            if (this->m_soundAlertEnabled) {
                this->m_trayManager.showBalloonNotification(
                    L"你的眼睛處於疲勞狀態，請適當休息",
                    L"系統已記錄疲勞狀態，並進入 20 分鐘暫停提醒冷卻保護，避免重複打擾。",
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
        } else if (PtInRect(&m_autoStartBtnRect, pt)) {
            bool cur = m_engine.getLifecycleAdapter().isAutoStartEnabled();
            m_engine.getLifecycleAdapter().setAutoStartEnabled(!cur);
            if (m_hwnd) InvalidateRect(m_hwnd, NULL, FALSE);
        } else if (PtInRect(&m_manualSyncBtnRect, pt)) {
            m_engine.triggerManualDataSync();
            if (m_hwnd) InvalidateRect(m_hwnd, NULL, FALSE);
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
            // 採樣完成，鎖定學習使用者視野自然偏向位置，校準基準並進入階段 5 (測驗完成提示)
            if (m_latestTelemetry.detection.hasFace && m_latestTelemetry.presenceDiagnostic.trackedCx > 0.0f) {
                m_calibratedUserBiasX = m_latestTelemetry.presenceDiagnostic.trackedCx;
                m_calibratedUserBiasY = m_latestTelemetry.presenceDiagnostic.trackedCy;
                m_userPositionBiasDetected = true;
                m_engine.getFaceLandmarker().calibrateUserPositionBias(m_calibratedUserBiasX, m_calibratedUserBiasY);
                std::cout << "[視覺校準] 成功學習並鎖定使用者視野自然偏向位置: (" 
                          << std::fixed << std::setprecision(1) << m_calibratedUserBiasX << ", " 
                          << m_calibratedUserBiasY << ")\n";
            }
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
        bool inAutoStart = (pThis->m_currentStage == UIStage::SettingsPanel) && (PtInRect(&pThis->m_autoStartBtnRect, pt) != FALSE);
        bool inManualSync = (pThis->m_currentStage == UIStage::SettingsPanel) && (PtInRect(&pThis->m_manualSyncBtnRect, pt) != FALSE);
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
                          inAutoStart != pThis->m_isHoveringAutoStartBtn ||
                          inManualSync != pThis->m_isHoveringManualSyncBtn ||
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
            pThis->m_isHoveringAutoStartBtn = inAutoStart;
            pThis->m_isHoveringManualSyncBtn = inManualSync;
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

    Gdiplus::StringFormat leftFormat;
    leftFormat.SetAlignment(Gdiplus::StringAlignmentNear);
    leftFormat.SetLineAlignment(Gdiplus::StringAlignmentCenter);

    Gdiplus::StringFormat rightFormat;
    rightFormat.SetAlignment(Gdiplus::StringAlignmentFar);
    rightFormat.SetLineAlignment(Gdiplus::StringAlignmentCenter);

    bool isNarrow = (w < 720 || h > w);

    // 1. 頂部大標題
    int headerFontSize = isNarrow ? std::clamp(w / 25, 16, 20) : 22;
    Gdiplus::Font headerFont(&fontFamily, static_cast<Gdiplus::REAL>(headerFontSize), Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
    Gdiplus::SolidBrush whiteBrush(Gdiplus::Color(255, 255, 255, 255));
    float headerY = 32.0f;
    Gdiplus::RectF headerRect(0.0f, headerY, static_cast<float>(w), 26.0f);
    g.DrawString(L"EFD 即時眼睛疲勞監控中心", -1, &headerFont, headerRect, &centerFormat, &whiteBrush);

    // 2. 即時相機連線狀態指示條 (Hardware Status Banner)
    std::string camStatusStr = m_latestTelemetry.lifecycleSummary;
    if (camStatusStr.empty()) {
        camStatusStr = "相機狀態: 運作中 (30 FPS)";
    }
    std::wstring wCamStatus = utf8ToWide(camStatusStr);
    Gdiplus::Font camStatusFont(&fontFamily, 11, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
    Gdiplus::SolidBrush camStatusBrush(Gdiplus::Color(255, 150, 197, 247)); // 科技藍
    Gdiplus::RectF camStatusRect(0.0f, headerY + 24.0f, static_cast<float>(w), 16.0f);
    g.DrawString(wCamStatus.c_str(), -1, &camStatusFont, camStatusRect, &centerFormat, &camStatusBrush);

    // 判斷在場、冷卻與疲勞狀態
    bool isPresent = (m_latestTelemetry.detection.hasFace && m_latestTelemetry.systemState.userPresent);
    FatigueLevel level = m_latestTelemetry.systemState.fatigueLevel;
    float fatigueScore = m_latestTelemetry.systemState.currentFatigueScore;
    bool inCooldown = (m_latestTelemetry.systemState.cooldownState == CooldownState::InCooldown);
    int cdRemaining = m_latestTelemetry.systemState.cooldownRemainingSeconds;
    int cdMins = cdRemaining / 60;
    int cdSecs = cdRemaining % 60;

    std::wstring presenceStr = isPresent ? L"是 (動態跟隨中)" : L"否 (離座中 / 未在視野)";
    std::wstring fatigueStr;
    Gdiplus::Color fatiguePillColor;
    if (!isPresent) {
        fatigueStr = L"暫停 (離座保護)";
        fatiguePillColor = Gdiplus::Color(220, 110, 120, 105);
    } else if (inCooldown) {
        wchar_t cdBuf[64];
        swprintf_s(cdBuf, 64, L"冷卻中 (%d分%02d秒)", cdMins, cdSecs);
        fatigueStr = cdBuf;
        fatiguePillColor = Gdiplus::Color(220, 60, 115, 145); // 舒緩冷卻藍
    } else if (level == FatigueLevel::SevereWarning || fatigueScore >= 65.0f) {
        fatigueStr = L"是 (超標警報)";
        fatiguePillColor = Gdiplus::Color(220, 235, 87, 87);
    } else if (level == FatigueLevel::Attention) {
        fatigueStr = L"輕度 (注意力提醒)";
        fatiguePillColor = Gdiplus::Color(220, 247, 227, 175);
    } else {
        fatigueStr = L"否 (清醒放鬆)";
        fatiguePillColor = Gdiplus::Color(220, 30, 177, 138);
    }

    Gdiplus::Color statusColor = Gdiplus::Color(255, 30, 177, 138); // 正常綠
    const wchar_t* statusText = L"生理狀態：正常清醒 (Relaxed)";
    wchar_t statusBuf[128];

    if (!isPresent) {
        statusColor = Gdiplus::Color(255, 110, 120, 105); // 離座灰
        statusText = L"生理狀態：未偵測到人臉 / 離座中 (暫停計算)";
    } else if (inCooldown) {
        statusColor = Gdiplus::Color(255, 60, 115, 145); // 舒緩冷卻藍
        swprintf_s(statusBuf, 128, L"生理狀態：疲勞提醒冷卻中 (%d分%02d秒) - 20/5/5 智能靜默暫停提醒", cdMins, cdSecs);
        statusText = statusBuf;
    } else if (level == FatigueLevel::Attention) {
        statusColor = Gdiplus::Color(255, 247, 227, 175); // 注意黃
        statusText = L"生理狀態：輕度用眼疲勞 (Attention)";
    } else if (level == FatigueLevel::SevereWarning || fatigueScore >= 65.0f) {
        statusColor = Gdiplus::Color(255, 235, 87, 87); // 警告紅
        statusText = L"生理狀態：你的眼睛處於疲勞狀態，請適當休息";
    }

    if (!isNarrow) {
        // =========================================================================
        // 寬螢幕雙欄佈局 (左欄: Viewfinder + 判定依據卡片, 右欄: 狀態與 4 遙測指標卡片)
        // =========================================================================
        int leftX = 24;
        int leftW = static_cast<int>(w * 0.58f);
        int viewY = 74;
        int viewH = 265;

        // -------------------------------------------------------------------------
        // A. 攝影機即時偵測視野視窗 (Live Detection Viewfinder)
        // -------------------------------------------------------------------------
        Gdiplus::SolidBrush viewBg(Gdiplus::Color(255, 18, 22, 16));
        drawRoundedButton(g, leftX, viewY, leftW, viewH, 16, &viewBg);

        // 若有影像幀，繪製相機畫面；若無或模擬，繪製科技掃描網格
        if (m_latestTelemetry.latestFrame.isValid()) {
            int fw = m_latestTelemetry.latestFrame.width;
            int fh = m_latestTelemetry.latestFrame.height;
            std::vector<uint32_t> bgra(fw * fh);
            const uint8_t* src = m_latestTelemetry.latestFrame.data.data();
            if (m_latestTelemetry.latestFrame.format == PixelFormat::RGB888) {
                for (int i = 0; i < fw * fh; ++i) {
                    uint8_t r = src[i * 3 + 0];
                    uint8_t gVal = src[i * 3 + 1];
                    uint8_t b = src[i * 3 + 2];
                    bgra[i] = (255u << 24) | (static_cast<uint32_t>(r) << 16) | (static_cast<uint32_t>(gVal) << 8) | b;
                }
            } else {
                for (int i = 0; i < fw * fh; ++i) {
                    uint8_t b = src[i * 3 + 0];
                    uint8_t gVal = src[i * 3 + 1];
                    uint8_t r = src[i * 3 + 2];
                    bgra[i] = (255u << 24) | (static_cast<uint32_t>(r) << 16) | (static_cast<uint32_t>(gVal) << 8) | b;
                }
            }
            Gdiplus::Bitmap bmp(fw, fh, fw * 4, PixelFormat32bppARGB, reinterpret_cast<BYTE*>(bgra.data()));
            g.DrawImage(&bmp, leftX + 4, viewY + 4, leftW - 8, viewH - 8);
        } else {
            // 繪製科技風格雷達網格背景
            Gdiplus::Pen gridPen(Gdiplus::Color(40, 150, 197, 247), 1.0f);
            for (int gx = leftX + 20; gx < leftX + leftW - 20; gx += 40) {
                g.DrawLine(&gridPen, gx, viewY + 4, gx, viewY + viewH - 4);
            }
            for (int gy = viewY + 20; gy < viewY + viewH - 20; gy += 40) {
                g.DrawLine(&gridPen, leftX + 4, gy, leftX + leftW - 4, gy);
            }
        }

        // 視野四角科技邊框括號
        Gdiplus::Pen cornerBracket(Gdiplus::Color(200, 150, 197, 247), 2.0f);
        int bLen = 14;
        g.DrawLine(&cornerBracket, leftX + 6, viewY + 6, leftX + 6 + bLen, viewY + 6);
        g.DrawLine(&cornerBracket, leftX + 6, viewY + 6, leftX + 6, viewY + 6 + bLen);
        g.DrawLine(&cornerBracket, leftX + leftW - 6, viewY + 6, leftX + leftW - 6 - bLen, viewY + 6);
        g.DrawLine(&cornerBracket, leftX + leftW - 6, viewY + 6, leftX + leftW - 6, viewY + 6 + bLen);
        g.DrawLine(&cornerBracket, leftX + 6, viewY + viewH - 6, leftX + 6 + bLen, viewY + viewH - 6);
        g.DrawLine(&cornerBracket, leftX + 6, viewY + viewH - 6, leftX + 6, viewY + viewH - 6 - bLen);
        g.DrawLine(&cornerBracket, leftX + leftW - 6, viewY + viewH - 6, leftX + leftW - 6 - bLen, viewY + viewH - 6);
        g.DrawLine(&cornerBracket, leftX + leftW - 6, viewY + viewH - 6, leftX + leftW - 6, viewY + viewH - 6 - bLen);

        int fw = (m_latestTelemetry.presenceDiagnostic.frameWidth > 0) ? m_latestTelemetry.presenceDiagnostic.frameWidth : 640;
        int fh = (m_latestTelemetry.presenceDiagnostic.frameHeight > 0) ? m_latestTelemetry.presenceDiagnostic.frameHeight : 480;
        float sx = static_cast<float>(leftW - 8) / fw;
        float sy = static_cast<float>(viewH - 8) / fh;

        // -------------------------------------------------------------------------
        // B. 動態平滑追蹤方形框 (Follow-Me Dynamic User Bounding Box)
        // 隨使用者真實位置動態平滑追蹤，不鎖定在正中間！
        // -------------------------------------------------------------------------
        int bx = m_latestTelemetry.presenceDiagnostic.boxX;
        int by = m_latestTelemetry.presenceDiagnostic.boxY;
        int bw = m_latestTelemetry.presenceDiagnostic.boxW;
        int bh = m_latestTelemetry.presenceDiagnostic.boxH;

        if (isPresent && bw > 0 && bh > 0) {
            // 使用者在場：高亮翡翠綠動態追隨方框
            int drawBoxX = leftX + 4 + static_cast<int>(bx * sx);
            int drawBoxY = viewY + 4 + static_cast<int>(by * sy);
            int drawBoxW = static_cast<int>(bw * sx);
            int drawBoxH = static_cast<int>(bh * sy);

            drawBoxX = std::clamp(drawBoxX, leftX + 8, leftX + leftW - 30);
            drawBoxY = std::clamp(drawBoxY, viewY + 8, viewY + viewH - 30);
            drawBoxW = std::clamp(drawBoxW, 30, leftX + leftW - drawBoxX - 8);
            drawBoxH = std::clamp(drawBoxH, 30, viewY + viewH - drawBoxY - 8);

            Gdiplus::Pen boxPen(Gdiplus::Color(255, 30, 220, 140), 2.5f);
            g.DrawRectangle(&boxPen, drawBoxX, drawBoxY, drawBoxW, drawBoxH);

            // 四角鎖定錨點 (Corner Reticle Brackets)
            Gdiplus::Pen cornerPen(Gdiplus::Color(255, 247, 227, 175), 3.0f);
            int cLen = std::min(15, std::min(drawBoxW, drawBoxH) / 3);
            g.DrawLine(&cornerPen, drawBoxX, drawBoxY, drawBoxX + cLen, drawBoxY);
            g.DrawLine(&cornerPen, drawBoxX, drawBoxY, drawBoxX, drawBoxY + cLen);
            g.DrawLine(&cornerPen, drawBoxX + drawBoxW, drawBoxY, drawBoxX + drawBoxW - cLen, drawBoxY);
            g.DrawLine(&cornerPen, drawBoxX + drawBoxW, drawBoxY, drawBoxX + drawBoxW, drawBoxY + cLen);
            g.DrawLine(&cornerPen, drawBoxX, drawBoxY + drawBoxH, drawBoxX + cLen, drawBoxY + drawBoxH);
            g.DrawLine(&cornerPen, drawBoxX, drawBoxY + drawBoxH, drawBoxX, drawBoxY + drawBoxH - cLen);
            g.DrawLine(&cornerPen, drawBoxX + drawBoxW, drawBoxY + drawBoxH, drawBoxX + drawBoxW - cLen, drawBoxY + drawBoxH);
            g.DrawLine(&cornerPen, drawBoxX + drawBoxW, drawBoxY + drawBoxH, drawBoxX + drawBoxW, drawBoxY + drawBoxH - cLen);

            // 方形框上方標籤
            Gdiplus::SolidBrush tagBg(Gdiplus::Color(220, 30, 177, 138));
            int tagH = 18;
            int tagW = 140;
            int tagY = std::max(viewY + 6, drawBoxY - tagH - 2);
            drawRoundedButton(g, drawBoxX, tagY, tagW, tagH, 6, &tagBg);
            Gdiplus::Font tagFont(&fontFamily, 10, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
            Gdiplus::SolidBrush tagText(Gdiplus::Color(255, 255, 255, 255));
            Gdiplus::RectF tagRect(static_cast<float>(drawBoxX), static_cast<float>(tagY), static_cast<float>(tagW), static_cast<float>(tagH));
            g.DrawString(L"使用者在場 (動態跟隨中)", -1, &tagFont, tagRect, &centerFormat, &tagText);
        } else {
            // 離座狀態：視野中央提示方框
            Gdiplus::Pen redDashedPen(Gdiplus::Color(200, 235, 87, 87), 2.0f);
            redDashedPen.SetDashStyle(Gdiplus::DashStyleDash);
            int defW = leftW * 5 / 10;
            int defH = viewH * 55 / 100;
            int defX = leftX + (leftW - defW) / 2;
            int defY = viewY + (viewH - defH) / 2;
            g.DrawRectangle(&redDashedPen, defX, defY, defW, defH);

            Gdiplus::SolidBrush tagBg(Gdiplus::Color(200, 235, 87, 87));
            int tagH = 20;
            int tagW = 160;
            drawRoundedButton(g, defX + (defW - tagW) / 2, defY + (defH - tagH) / 2, tagW, tagH, 8, &tagBg);
            Gdiplus::Font tagFont(&fontFamily, 11, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
            Gdiplus::SolidBrush tagText(Gdiplus::Color(255, 255, 255, 255));
            Gdiplus::RectF tagRect(static_cast<float>(defX + (defW - tagW) / 2), static_cast<float>(defY + (defH - tagH) / 2), static_cast<float>(tagW), static_cast<float>(tagH));
            g.DrawString(L"【未在場 / 離座中】 等待使用者", -1, &tagFont, tagRect, &centerFormat, &tagText);
        }

        // 視野左上角 HUD 狀態膠囊 (Presence Badge)
        int badgeW = 120;
        int badgeH = 22;
        int badgeX = leftX + 12;
        int badgeY = viewY + 12;
        Gdiplus::SolidBrush badgeBg(isPresent ? Gdiplus::Color(220, 30, 177, 138) : Gdiplus::Color(220, 110, 120, 105));
        drawRoundedButton(g, badgeX, badgeY, badgeW, badgeH, 10, &badgeBg);
        Gdiplus::Font badgeFont(&fontFamily, 11, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
        Gdiplus::SolidBrush badgeText(Gdiplus::Color(255, 255, 255, 255));
        Gdiplus::RectF badgeRect(static_cast<float>(badgeX), static_cast<float>(badgeY), static_cast<float>(badgeW), static_cast<float>(badgeH));
        g.DrawString(isPresent ? L"在座正視" : L"離座等待", -1, &badgeFont, badgeRect, &centerFormat, &badgeText);

        // 視野右上角 疲勞等級膠囊 (Fatigue Badge)
        int fBadgeW = inCooldown ? 140 : 120;
        int fBadgeH = 22;
        int fBadgeX = leftX + leftW - fBadgeW - 12;
        int fBadgeY = viewY + 12;
        Gdiplus::SolidBrush fBadgeBg(fatiguePillColor);
        drawRoundedButton(g, fBadgeX, fBadgeY, fBadgeW, fBadgeH, 10, &fBadgeBg);
        Gdiplus::RectF fBadgeRect(static_cast<float>(fBadgeX), static_cast<float>(fBadgeY), static_cast<float>(fBadgeW), static_cast<float>(fBadgeH));
        g.DrawString(fatigueStr.c_str(), -1, &badgeFont, fBadgeRect, &centerFormat, &badgeText);

        // -------------------------------------------------------------------------
        // C. 左欄下方：即時判斷依據與狀態日誌卡片 (Diagnostic Rationale Card)
        // -------------------------------------------------------------------------
        int diagCardY = viewY + viewH + 10;
        int diagCardH = h - 68 - diagCardY - 8;
        Gdiplus::SolidBrush diagCardBg(Gdiplus::Color(255, 28, 32, 22));
        Gdiplus::Pen diagCardBorder(Gdiplus::Color(160, 30, 177, 138), 1.0f);
        drawRoundedButton(g, leftX, diagCardY, leftW, diagCardH, 14, &diagCardBg, &diagCardBorder);

        Gdiplus::Font dTitleFont(&fontFamily, 12, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
        Gdiplus::SolidBrush dTitleBrush(Gdiplus::Color(255, 247, 227, 175)); // 卡其金
        Gdiplus::RectF dTitleRect(static_cast<float>(leftX + 14), static_cast<float>(diagCardY + 8), static_cast<float>(leftW - 28), 18.0f);
        g.DrawString(L"即時判斷依據與狀態日誌 (Live Diagnostic Rationale)", -1, &dTitleFont, dTitleRect, &leftFormat, &dTitleBrush);

        Gdiplus::Font dTextFont(&fontFamily, 11, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
        Gdiplus::SolidBrush dTextBrush(Gdiplus::Color(255, 220, 220, 220));
        Gdiplus::SolidBrush cyanText(Gdiplus::Color(255, 150, 197, 247));
        Gdiplus::SolidBrush redText(Gdiplus::Color(255, 235, 87, 87));
        Gdiplus::SolidBrush greenText(Gdiplus::Color(255, 30, 220, 140));

        // 診斷行 1: 是否在場
        std::wstring diagLine1 = L"【是否在場】: " + presenceStr;
        Gdiplus::RectF dRect1(static_cast<float>(leftX + 14), static_cast<float>(diagCardY + 30), static_cast<float>(leftW - 28), 18.0f);
        g.DrawString(diagLine1.c_str(), -1, &dTextFont, dRect1, &leftFormat, isPresent ? &greenText : &redText);

        // 診斷行 2: 在場依據
        std::wstring diagLine2;
        if (isPresent) {
            wchar_t presBuf[192];
            swprintf_s(presBuf, 192, L"【在場依據】: 膚色點數 %d>=%d, 對稱度 %.2f>=0.06, 輪廓比例 %.2f, 雙眼垂直對比驗證通過",
                       m_latestTelemetry.presenceDiagnostic.skinPixels,
                       m_latestTelemetry.presenceDiagnostic.minSkinRequired,
                       static_cast<double>(m_latestTelemetry.presenceDiagnostic.symmetryRatio),
                       static_cast<double>(m_latestTelemetry.presenceDiagnostic.aspectRatio));
            diagLine2 = presBuf;
        } else {
            std::string reason = m_latestTelemetry.presenceDiagnostic.unconfirmedReason.empty()
                ? "未在視野內偵測到穩定人體/面部特徵"
                : m_latestTelemetry.presenceDiagnostic.unconfirmedReason;
            diagLine2 = L"【在場依據】: " + utf8ToWide(reason);
        }
        Gdiplus::RectF dRect2(static_cast<float>(leftX + 14), static_cast<float>(diagCardY + 50), static_cast<float>(leftW - 28), 18.0f);
        g.DrawString(diagLine2.c_str(), -1, &dTextFont, dRect2, &leftFormat, &dTextBrush);

        // 診斷行 3: 是否疲勞
        std::wstring diagLine3;
        if (!isPresent) {
            diagLine3 = L"【是否疲勞】: 暫停判定 (離座防誤報保護)";
        } else if (inCooldown) {
            wchar_t cdBuf[128];
            swprintf_s(cdBuf, 128, L"【是否疲勞】: 20/5/5 冷卻暫停提醒中 (剩餘 %d分%02d秒)", cdMins, cdSecs);
            diagLine3 = cdBuf;
        } else if (level == FatigueLevel::SevereWarning || fatigueScore >= 65.0f) {
            diagLine3 = L"【是否疲勞】: 是 (嚴重疲勞警告 - 請適當休息)";
        } else if (level == FatigueLevel::Attention) {
            diagLine3 = L"【是否疲勞】: 輕度 (注意力提醒)";
        } else {
            diagLine3 = L"【是否疲勞】: 否 (清醒放鬆)";
        }
        Gdiplus::RectF dRect3(static_cast<float>(leftX + 14), static_cast<float>(diagCardY + 70), static_cast<float>(leftW - 28), 18.0f);
        g.DrawString(diagLine3.c_str(), -1, &dTextFont, dRect3, &leftFormat, inCooldown ? &cyanText : (level == FatigueLevel::SevereWarning ? &redText : &greenText));

        // 診斷行 4: 疲勞依據
        std::wstring diagLine4;
        if (!isPresent) {
            diagLine4 = L"【疲勞依據】: 暫停特徵計算 (離座防誤報)";
        } else {
            wchar_t fatBuf[256];
            swprintf_s(fatBuf, 256, L"【疲勞依據】: EAR=%.3f (%s), PERCLOS=%.1f%%, 眨眼=%.1f次/分, MSE CI=%.2f, 疲勞分=%.1f/65.0",
                       static_cast<double>(m_latestTelemetry.eyeMetrics.earAvg),
                       m_latestTelemetry.eyeMetrics.isEyeClosed ? L"閉眼" : L"睜眼",
                       static_cast<double>(m_latestTelemetry.eyeMetrics.perclos * 100.0f),
                       static_cast<double>(m_latestTelemetry.eyeMetrics.blinkRatePerMin),
                       static_cast<double>(m_latestTelemetry.complexityMetrics.complexityIndex),
                       static_cast<double>(m_latestTelemetry.systemState.currentFatigueScore));
            diagLine4 = fatBuf;
        }
        Gdiplus::RectF dRect4(static_cast<float>(leftX + 14), static_cast<float>(diagCardY + 90), static_cast<float>(leftW - 28), 18.0f);
        g.DrawString(diagLine4.c_str(), -1, &dTextFont, dRect4, &leftFormat, &cyanText);

        // =========================================================================
        // 右欄：生理狀態大卡片、4 遙測指標網格、科研追蹤資訊卡片
        // =========================================================================
        int rightX = leftX + leftW + 20;
        int rightW = w - rightX - 24;

        // 右欄頂部：核心狀態大卡片
        int statCardH = 56;
        Gdiplus::SolidBrush cardBrush(statusColor);
        drawRoundedButton(g, rightX, viewY, rightW, statCardH, 16, &cardBrush);

        Gdiplus::Font cardFont(&fontFamily, 14, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
        Gdiplus::SolidBrush cardTextBrush(Gdiplus::Color(255, 37, 41, 28));
        Gdiplus::RectF cardTextRect(static_cast<float>(rightX), static_cast<float>(viewY), static_cast<float>(rightW), static_cast<float>(statCardH));
        g.DrawString(statusText, -1, &cardFont, cardTextRect, &centerFormat, &cardTextBrush);

        // 右欄中部：4 欄動態遙測數據 (2x2 網格)
        wchar_t b1[32], b2[32], b3[32], b4[32];
        if (!isPresent) {
            swprintf_s(b1, 32, L"--");
            swprintf_s(b2, 32, L"--");
            swprintf_s(b3, 32, L"--");
            swprintf_s(b4, 32, L"暫停中");
        } else {
            swprintf_s(b1, 32, L"%.3f", static_cast<double>(m_latestTelemetry.eyeMetrics.earAvg));
            swprintf_s(b2, 32, L"%.1f%%", static_cast<double>(m_latestTelemetry.eyeMetrics.perclos * 100.0f));
            swprintf_s(b3, 32, L"%.2f", static_cast<double>(m_latestTelemetry.complexityMetrics.complexityIndex));
            swprintf_s(b4, 32, L"%.1f", static_cast<double>(m_latestTelemetry.systemState.currentFatigueScore));
        }

        auto drawMetricCard = [&](int ix, int iy, int iw, int ih, const wchar_t* label, const wchar_t* val, const wchar_t* subtext) {
            Gdiplus::SolidBrush boxBrush(Gdiplus::Color(255, 50, 56, 38));
            drawRoundedButton(g, ix, iy, iw, ih, 14, &boxBrush);

            Gdiplus::Font lFont(&fontFamily, 11, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
            Gdiplus::SolidBrush lBrush(Gdiplus::Color(255, 180, 180, 180));
            Gdiplus::RectF lRect(static_cast<float>(ix), static_cast<float>(iy + 8), static_cast<float>(iw), 16.0f);
            g.DrawString(label, -1, &lFont, lRect, &centerFormat, &lBrush);

            Gdiplus::Font vFont(&fontFamily, 20, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
            Gdiplus::SolidBrush vBrush(Gdiplus::Color(255, 150, 197, 247)); // 科技藍
            Gdiplus::RectF vRect(static_cast<float>(ix), static_cast<float>(iy + 26), static_cast<float>(iw), 24.0f);
            g.DrawString(val, -1, &vFont, vRect, &centerFormat, &vBrush);

            Gdiplus::Font sFont(&fontFamily, 9, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
            Gdiplus::SolidBrush sBrush(Gdiplus::Color(255, 140, 150, 140));
            Gdiplus::RectF sRect(static_cast<float>(ix), static_cast<float>(iy + 50), static_cast<float>(iw), 14.0f);
            g.DrawString(subtext, -1, &sFont, sRect, &centerFormat, &sBrush);
        };

        int gridY = viewY + statCardH + 12;
        int gap = 10;
        int mCardW = (rightW - gap) / 2;
        int mCardH = 70;

        drawMetricCard(rightX, gridY, mCardW, mCardH, L"雙眼 EAR", b1, isPresent ? (m_latestTelemetry.eyeMetrics.isEyeClosed ? L"閉眼狀態" : L"睜眼常態") : L"等待輸入");
        drawMetricCard(rightX + mCardW + gap, gridY, mCardW, mCardH, L"PERCLOS 閉眼比", b2, isPresent ? L"動態滑動窗口" : L"等待輸入");
        drawMetricCard(rightX, gridY + mCardH + gap, mCardW, mCardH, L"複雜度 (MSE)", b3, isPresent ? L"非線性動力學" : L"等待輸入");
        drawMetricCard(rightX + mCardW + gap, gridY + mCardH + gap, mCardW, mCardH, L"綜合疲勞分數", b4, inCooldown ? L"冷卻防打擾中" : (isPresent ? L"警戒閾值 65.0" : L"暫停保護"));

        // 右欄底部：科研追蹤與本機儲存資訊卡片
        int studyCardY = gridY + (mCardH + gap) * 2;
        int studyCardH = h - studyCardY - 70;
        Gdiplus::SolidBrush studyCardBg(Gdiplus::Color(255, 28, 32, 22));
        Gdiplus::Pen studyCardBorder(Gdiplus::Color(160, 30, 177, 138), 1.0f);
        drawRoundedButton(g, rightX, studyCardY, rightW, studyCardH, 14, &studyCardBg, &studyCardBorder);

        std::string uuid = m_latestTelemetry.subjectUuid;
        if (uuid.empty()) uuid = "EFD-TEST-USER";
        std::wstring wUuid = utf8ToWide(uuid);

        Gdiplus::Font scFont(&fontFamily, 11, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
        Gdiplus::RectF scRect1(static_cast<float>(rightX + 12), static_cast<float>(studyCardY + 8), static_cast<float>(rightW - 24), 18.0f);
        g.DrawString((L"受試者匿名代碼: " + wUuid).c_str(), -1, &scFont, scRect1, &leftFormat, &cyanText);

        Gdiplus::RectF scRect2(static_cast<float>(rightX + 12), static_cast<float>(studyCardY + 28), static_cast<float>(rightW - 24), 18.0f);
        g.DrawString(L"時序資料庫: SQLite 本機封存 (WAL 模式啟動)", -1, &scFont, scRect2, &leftFormat, &whiteBrush);

        Gdiplus::SolidBrush goldBrush(Gdiplus::Color(255, 247, 227, 175));
        Gdiplus::RectF scRect3(static_cast<float>(rightX + 12), static_cast<float>(studyCardY + 48), static_cast<float>(rightW - 24), 18.0f);
        g.DrawString(L"防打擾機制: 20/5/5 智能靜默 (冷卻 1200s, 快篩 300s)", -1, &scFont, scRect3, &leftFormat, &goldBrush);

        Gdiplus::SolidBrush mintBrush(Gdiplus::Color(255, 120, 230, 195));
        Gdiplus::RectF scRect4(static_cast<float>(rightX + 12), static_cast<float>(studyCardY + 68), static_cast<float>(rightW - 24), 18.0f);
        std::wstring syncInfo = m_engine.getSyncWorker().isSyncedToday() 
            ? (L"科研雲端同步: 本日已成功同步 (" + utf8ToWide(m_engine.getSyncWorker().getLastSyncTimeStr()) + L", " + std::to_wstring(m_engine.getSyncWorker().getLastSyncRecordCount()) + L" 筆)")
            : L"科研雲端同步: 每晚 21:00 自動儲存落盤並上傳 (或至設定手動同步)";
        g.DrawString(syncInfo.c_str(), -1, &scFont, scRect4, &leftFormat, &mintBrush);

    } else {
        // =========================================================================
        // 窄螢幕模式 (手機模擬器等比排列)
        // =========================================================================
        int cardW = w - 24;
        int cardH = 50;
        int cardX = 12;
        int cardY = static_cast<int>(headerY + 46);

        Gdiplus::SolidBrush cardBrush(statusColor);
        drawRoundedButton(g, cardX, cardY, cardW, cardH, 14, &cardBrush);
        Gdiplus::Font cardFont(&fontFamily, 13, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
        Gdiplus::SolidBrush cardTextBrush(Gdiplus::Color(255, 37, 41, 28));
        Gdiplus::RectF cardTextRect(static_cast<float>(cardX), static_cast<float>(cardY), static_cast<float>(cardW), static_cast<float>(cardH));
        g.DrawString(statusText, -1, &cardFont, cardTextRect, &centerFormat, &cardTextBrush);

        int viewY = cardY + cardH + 10;
        int viewH = 180;
        Gdiplus::SolidBrush viewBg(Gdiplus::Color(255, 18, 22, 16));
        drawRoundedButton(g, cardX, viewY, cardW, viewH, 14, &viewBg);

        int fw = (m_latestTelemetry.presenceDiagnostic.frameWidth > 0) ? m_latestTelemetry.presenceDiagnostic.frameWidth : 640;
        int fh = (m_latestTelemetry.presenceDiagnostic.frameHeight > 0) ? m_latestTelemetry.presenceDiagnostic.frameHeight : 480;
        float sx = static_cast<float>(cardW - 8) / fw;
        float sy = static_cast<float>(viewH - 8) / fh;

        // 窄螢幕動態方框
        if (isPresent && m_latestTelemetry.presenceDiagnostic.boxW > 0) {
            Gdiplus::Pen boxPen(Gdiplus::Color(255, 30, 220, 140), 2.0f);
            g.DrawRectangle(&boxPen, 
                cardX + 4 + static_cast<int>(m_latestTelemetry.presenceDiagnostic.boxX * sx),
                viewY + 4 + static_cast<int>(m_latestTelemetry.presenceDiagnostic.boxY * sy),
                static_cast<int>(m_latestTelemetry.presenceDiagnostic.boxW * sx),
                static_cast<int>(m_latestTelemetry.presenceDiagnostic.boxH * sy));
        }

        // 窄螢幕簡易診斷文字
        int infoY = viewY + viewH + 10;
        Gdiplus::Font infoFont(&fontFamily, 10, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
        Gdiplus::SolidBrush infoBrush(Gdiplus::Color(255, 230, 230, 230));
        Gdiplus::SolidBrush goldBrush(Gdiplus::Color(255, 247, 227, 175));

        std::wstring l1 = L"【是否在場】: " + presenceStr;
        std::wstring l2 = isPresent 
            ? (L"【在場依據】: 動態跟隨中, 採樣點 " + std::to_wstring(m_latestTelemetry.presenceDiagnostic.skinPixels) + L">=" + std::to_wstring(m_latestTelemetry.presenceDiagnostic.minSkinRequired))
            : L"【在場依據】: 視野內未偵測到真人特徵";
        std::wstring l3 = inCooldown ? (L"【是否疲勞】: 冷卻中 (" + std::to_wstring(cdMins) + L"分" + std::to_wstring(cdSecs) + L"秒)") : (L"【是否疲勞】: " + fatigueStr);
        std::wstring l4 = isPresent
            ? (L"【疲勞依據】: EAR=" + std::to_wstring(m_latestTelemetry.eyeMetrics.earAvg).substr(0, 5) + L", PERCLOS=" + std::to_wstring(static_cast<int>(m_latestTelemetry.eyeMetrics.perclos * 100)) + L"%")
            : L"【疲勞依據】: 暫停特徵計算 (離座防誤報)";

        g.DrawString(l1.c_str(), -1, &infoFont, Gdiplus::RectF(static_cast<float>(cardX), static_cast<float>(infoY), static_cast<float>(cardW), 16.0f), &leftFormat, isPresent ? &infoBrush : &goldBrush);
        g.DrawString(l2.c_str(), -1, &infoFont, Gdiplus::RectF(static_cast<float>(cardX), static_cast<float>(infoY + 18), static_cast<float>(cardW), 16.0f), &leftFormat, &infoBrush);
        g.DrawString(l3.c_str(), -1, &infoFont, Gdiplus::RectF(static_cast<float>(cardX), static_cast<float>(infoY + 36), static_cast<float>(cardW), 16.0f), &leftFormat, &infoBrush);
        g.DrawString(l4.c_str(), -1, &infoFont, Gdiplus::RectF(static_cast<float>(cardX), static_cast<float>(infoY + 54), static_cast<float>(cardW), 16.0f), &leftFormat, &infoBrush);
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
    int itemH = isNarrow ? 46 : 52;
    int gap = isNarrow ? 10 : 12;

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

    // 設定項目 3: 開機自動啟動 (支援 Windows 登錄檔，粗體, 20px 圓角邊框)
    int item3Y = item2Y + itemH + gap;
    m_autoStartBtnRect = { cardX, item3Y, cardX + cardW, item3Y + itemH };
    bool autoStartOn = m_engine.getLifecycleAdapter().isAutoStartEnabled();
    Gdiplus::SolidBrush item3Bg(m_isHoveringAutoStartBtn ? Gdiplus::Color(255, 60, 68, 46) : Gdiplus::Color(255, 50, 56, 38));
    Gdiplus::Pen item3Border(autoStartOn ? Gdiplus::Color(200, 30, 220, 140) : Gdiplus::Color(160, 120, 120, 120), 1.5f);
    drawRoundedButton(g, cardX, item3Y, cardW, itemH, 20, &item3Bg, &item3Border);

    Gdiplus::RectF item3TRect(static_cast<float>(cardX + 20), static_cast<float>(item3Y), static_cast<float>(cardW / 2), static_cast<float>(itemH));
    g.DrawString(L"開機自動啟動", -1, &itemTitleFont, item3TRect, &leftFormat, &whiteBrush);
    Gdiplus::SolidBrush autoStartBrush(autoStartOn ? Gdiplus::Color(255, 120, 230, 195) : Gdiplus::Color(255, 180, 180, 180));
    Gdiplus::RectF item3VRect(static_cast<float>(cardX + cardW / 2), static_cast<float>(item3Y), static_cast<float>(cardW / 2 - 20), static_cast<float>(itemH));
    g.DrawString(autoStartOn ? L"已啟用 (點擊關閉)" : L"已停用 (點擊開啟)", -1, &itemValFont, item3VRect, &rightFormat, &autoStartBrush);

    // 設定項目 4: 當日科研數據同步 (夜間自動儲存或手動立即同步，粗體, 20px 圓角邊框)
    int item4Y = item3Y + itemH + gap;
    m_manualSyncBtnRect = { cardX, item4Y, cardX + cardW, item4Y + itemH };
    bool isSyncing = m_engine.getSyncWorker().isSyncing();
    bool isSyncedToday = m_engine.getSyncWorker().isSyncedToday();
    Gdiplus::SolidBrush item4Bg(m_isHoveringManualSyncBtn ? Gdiplus::Color(255, 30, 80, 120) : Gdiplus::Color(255, 20, 50, 80));
    Gdiplus::Pen item4Border(Gdiplus::Color(200, 150, 197, 247), 1.5f);
    drawRoundedButton(g, cardX, item4Y, cardW, itemH, 20, &item4Bg, &item4Border);

    Gdiplus::RectF item4TRect(static_cast<float>(cardX + 20), static_cast<float>(item4Y), static_cast<float>(cardW / 2), static_cast<float>(itemH));
    g.DrawString(L"當日數據同步", -1, &itemTitleFont, item4TRect, &leftFormat, &whiteBrush);
    std::wstring syncVal = isSyncing ? L"正在同步中..." : (isSyncedToday ? L"本日已同步完成 (點擊再次同步)" : L"每晚21:00自動同步 (點擊立即同步)");
    Gdiplus::SolidBrush syncBrush(isSyncedToday ? Gdiplus::Color(255, 120, 230, 195) : Gdiplus::Color(255, 150, 197, 247));
    Gdiplus::RectF item4VRect(static_cast<float>(cardX + cardW / 2), static_cast<float>(item4Y), static_cast<float>(cardW / 2 - 20), static_cast<float>(itemH));
    g.DrawString(syncVal.c_str(), -1, &itemValFont, item4VRect, &rightFormat, &syncBrush);

    // 3. 底部動作按鈕：返回監控中心 (粗體, 20px 圓角邊框)
    int btnH = isNarrow ? 42 : 46;
    int btnW = isNarrow ? std::min(w - 40, 280) : 280;
    int returnBtnY = item4Y + itemH + (isNarrow ? 18 : 24);
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
    HINSTANCE hInst = GetModuleHandle(NULL);
    HICON hAppIcon = LoadIconW(hInst, MAKEINTRESOURCEW(1));
    if (!hAppIcon) {
        hAppIcon = (HICON)LoadImageW(hInst, L"assets\\app.ico", IMAGE_ICON, 0, 0, LR_LOADFROMFILE | LR_DEFAULTSIZE);
    }

    WNDCLASSEXW wc = { sizeof(WNDCLASSEXW) };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.lpszClassName = L"EFD_FullNativeWindow";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hIcon = hAppIcon;
    wc.hIconSm = hAppIcon;
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

    if (hAppIcon) {
        SendMessageW(m_hwnd, WM_SETICON, ICON_BIG, (LPARAM)hAppIcon);
        SendMessageW(m_hwnd, WM_SETICON, ICON_SMALL, (LPARAM)hAppIcon);
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
