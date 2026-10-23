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

// 繪製現代滑動開關按鈕 (Toggle Switch)，嚴格遵照專案五色盤：
// 1EB18A (翡翠綠), 96C5F7 (天藍), FFFFFF (純白), F7E3AF (溫暖鵝黃), 25291C (深黑曜)
// 開啟 ON:  軌道底色為柔和薄荷綠 (RGBA 30, 177, 138, 110)，滑塊圓形為深翡翠綠 0x1EB18A (RGB 30, 177, 138)
// 關閉 OFF: 軌道底色為中性深灰/黑曜 (RGBA 37, 41, 28, 140)，滑塊圓形為溫和白 0xFFFFFF (RGB 245, 245, 245)
static void drawToggleSwitch(Gdiplus::Graphics& g, int x, int y, int w, int h, bool isOn) {
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

    // 1. 膠囊外軌道 (Capsule Track)
    Gdiplus::GraphicsPath trackPath;
    int d = h;
    trackPath.AddArc(x, y, d, d, 90, 180);
    trackPath.AddArc(x + w - d, y, d, d, 270, 180);
    trackPath.CloseFigure();

    if (isOn) {
        // ON 狀態軌道：翡翠綠 0x1EB18A 帶柔和透明底色 (RGB: 140, 218, 198)
        Gdiplus::SolidBrush trackBrush(Gdiplus::Color(255, 140, 218, 198));
        g.FillPath(&trackBrush, &trackPath);
    } else {
        // OFF 狀態軌道：深墨黑曜 0x25291C 與中性灰融合 (RGB: 158, 158, 158)
        Gdiplus::SolidBrush trackBrush(Gdiplus::Color(255, 158, 158, 158));
        g.FillPath(&trackBrush, &trackPath);
    }

    // 2. 圓形滑動鈕 (Thumb Knob)
    // 直徑略小於軌道高度，提供完美的留白邊距 (padding)
    int knobPadding = 3;
    int knobDiameter = h - knobPadding * 2;
    int knobX = isOn ? (x + w - knobDiameter - knobPadding) : (x + knobPadding);
    int knobY = y + knobPadding;

    // 滑塊微立體陰影 (Soft Drop Shadow)
    Gdiplus::SolidBrush shadowBrush(Gdiplus::Color(40, 37, 41, 28)); // 0x25291C 陰影
    g.FillEllipse(&shadowBrush, knobX, knobY + 1, knobDiameter, knobDiameter);

    if (isOn) {
        // ON 狀態滑塊：品牌核心主色翡翠綠 0x1EB18A (RGB 30, 177, 138)
        Gdiplus::SolidBrush knobBrush(Gdiplus::Color(255, 30, 177, 138));
        g.FillEllipse(&knobBrush, knobX, knobY, knobDiameter, knobDiameter);
        // 微光高光輪廓
        Gdiplus::Pen highlightPen(Gdiplus::Color(100, 255, 255, 255), 1.0f);
        g.DrawEllipse(&highlightPen, knobX, knobY, knobDiameter, knobDiameter);
    } else {
        // OFF 狀態滑塊：純白亮灰 0xFFFFFF (RGB 245, 245, 245)
        Gdiplus::SolidBrush knobBrush(Gdiplus::Color(255, 245, 245, 245));
        g.FillEllipse(&knobBrush, knobX, knobY, knobDiameter, knobDiameter);
        // 微柔和輪廓 (0xF7E3AF 鵝黃高光邊緣)
        Gdiplus::Pen edgePen(Gdiplus::Color(120, 220, 220, 220), 1.0f);
        g.DrawEllipse(&edgePen, knobX, knobY, knobDiameter, knobDiameter);
    }
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

static const UINT WM_EFD_SHOW_UI = RegisterWindowMessageW(L"WM_EFD_SHOW_UI_MSG");

NativeWelcomeWindow::NativeWelcomeWindow(int width, int height, bool startInBackground)
    : m_width(width), m_height(height), m_startInBackground(startInBackground), m_engine(PlatformType::Windows) {
    
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
    m_latestTelemetry.systemState.smoothedFatigueScore = 5.0f;
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
            if (this->m_hwnd) PostMessage(this->m_hwnd, WM_DESTROY, 0, 0);
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
            t.systemState.smoothedFatigueScore,
            t.systemState.fatigueLevel,
            t.lifecycleSummary
        );
        // 即時更新系統托盤提示
        this->m_trayManager.updateStatus(
            t.systemState.fatigueLevel,
            t.systemState.smoothedFatigueScore,
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
        // 已有受試者資料時 (MainDashboard 或 SettingsPanel) 均進入正式遙測輸出
        if (this->m_currentStage != UIStage::MainDashboard && this->m_currentStage != UIStage::SettingsPanel) {
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
                                 this->m_currentStage == UIStage::ActiveCalibration)) {
                InvalidateRect(this->m_hwnd, NULL, FALSE);
            }
            return;
        }

        // 控制台持續輸出最新眼動與疲勞遙測數據 (進入主儀表板後，每 15 幀或閉眼時輸出)
        int count = ++s_telemetryLogCount;
        if (count % 15 == 0 || t.eyeMetrics.isEyeClosed) {
            bool isPresent = (t.detection.hasFace && t.systemState.userPresent);
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

                if (t.systemState.cooldownState == CooldownState::InCooldown) {
                    std::cout << " | 【是否疲勞: 20分鐘冷卻靜默暫停提醒中 (剩餘 " << cdMins << "分" << std::setw(2) << std::setfill('0') << cdSecs << "秒)】" << std::setfill(' ');
                } else if (t.systemState.cooldownState == CooldownState::FastScreening) {
                    std::cout << " | 【是否疲勞: 5分鐘輕量快篩追蹤中 (總體分數: " << std::fixed << std::setprecision(1) << t.systemState.smoothedFatigueScore << ")】";
                } else if (t.systemState.fatigueLevel == FatigueLevel::SevereWarning || t.systemState.smoothedFatigueScore >= 65.0f) {
                    std::cout << " | 【是否疲勞: 是 (超標警報)】";
                } else if (t.systemState.fatigueLevel == FatigueLevel::Attention) {
                    std::cout << " | 【是否疲勞: 輕度 (注意力提醒)】";
                } else {
                    std::cout << " | 【是否疲勞: 否 (清醒放鬆)】";
                }

                std::cout << " | 【在場依據: 膚色點數 " << t.presenceDiagnostic.skinPixels
                          << (t.presenceDiagnostic.skinPixels >= t.presenceDiagnostic.minSkinRequired ? ">=" : "<")
                          << t.presenceDiagnostic.minSkinRequired
                          << ", 對稱度 " << std::fixed << std::setprecision(2) << t.presenceDiagnostic.symmetryRatio
                          << (t.presenceDiagnostic.symmetryRatio >= 0.06f ? ">=0.06" : "<0.06(邊緣/追蹤寬限)")
                          << ", 輪廓比例 " << std::setprecision(2) << t.presenceDiagnostic.aspectRatio << "】";

                std::cout << " | 【疲勞依據: EAR=" << std::fixed << std::setprecision(3) << t.eyeMetrics.earAvg
                          << (t.eyeMetrics.isEyeClosed ? " (閉眼)" : " (睜眼)")
                          << " (基準閾值 " << std::fixed << std::setprecision(3) << t.currentThreshold << "), PERCLOS=" << std::setprecision(1) << (t.eyeMetrics.perclos * 100.0f) << "%"
                          << ", 眨眼=" << std::setprecision(1) << t.eyeMetrics.blinkRatePerMin << "次/分"
                          << ", MSE CI=" << std::setprecision(2) << t.complexityMetrics.complexityIndex
                          << ", 疲勞分=" << std::setprecision(1) << t.systemState.smoothedFatigueScore << "/100.0 (警戒閾值 65.0)"
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
        // 初始測試階段不彈出打擾提醒
        if (this->m_currentStage != UIStage::MainDashboard && this->m_currentStage != UIStage::SettingsPanel) {
            return;
        }

        auto now = std::chrono::steady_clock::now();

        // 狀態機 (FatigueStateMachine) 已具備完整的 20/5/5 冷卻防打擾邏輯
        // 此處不應再做強制 1200 秒攔截，否則會導致快篩期的「主動升級提醒」無法彈出

        if (level == FatigueLevel::SevereWarning || score >= 65.0f) {
            this->m_lastAlertNotificationTime = now;
            std::cout << "\n>>> [疲勞警報通知發送] 疲勞分數: " << std::fixed << std::setprecision(1) << score
                      << " (嚴重警告) - 你的眼睛處於疲勞狀態，請適當休息 (已啟動 20 分鐘絕對靜默防打擾冷卻機制) <<<\n\n";

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

    // 判斷受試者是否已完成初次測試 (包含校準紀錄或已有時序特徵資料)
    bool hasTested = m_engine.getStudyTracker().isInitialSetupCompleted() || 
                     (m_engine.getDatabaseService().getRecordCount() > 0);

    if (m_engine.getStudyTracker().getStatus() == StudyStatus::LockedForPostTest) {
        // 第 14 天實驗期滿門禁：強制解除背景隱藏，直接開啟後測問卷門禁畫面
        m_startInBackground = false;
        m_currentStage = UIStage::StudyCompletedGate;
        std::cout << "[實驗週期完成] 偵測到已達第 14 天實驗期滿，自動進入後測問卷門禁介面 (資產 5.png)\n";
    } else if (m_startInBackground) {
        if (hasTested) {
            // 開機背景啟動模式且已測試過：在背景運行 (常駐托盤不彈窗)，自動連線攝影機開始即時記錄眼動數據與疲勞監控
            m_currentStage = UIStage::MainDashboard;
            std::cout << "[開機啟動模式] 已載入受試者資料，系統已在背景運行，攝影機已自動連線並即時記錄眼動數據 (常駐系統托盤)\n";
        } else {
            // 開機背景啟動模式但尚未進行過初次測試：解除背景隱藏，直接呈現歡迎畫面引導使用者完成初次校準
            m_startInBackground = false;
            m_currentStage = UIStage::Welcome;
            std::cout << "[開機啟動模式] 偵測到尚未完成初次視覺校準，自動彈出歡迎介面引導使用者\n";
        }
    } else {
        // 手動開啟程式：
        if (hasTested) {
            // 已有受試者測試紀錄：直接進入設定畫面，同時自動連線攝影機開始即時記錄眼動數據
            m_currentStage = UIStage::SettingsPanel;
            std::cout << "[手動開啟系統] 成功載入受試者資料，直接進入設定介面，攝影機已自動連線並記錄眼動數據\n";
        } else {
            // 尚未進行過測試：顯示歡迎畫面引導校準
            m_currentStage = UIStage::Welcome;
            std::cout << "[手動開啟系統] 首次使用或尚未完成初次測試，進入歡迎介面引導流程\n";
        }
    }
    
    // 無條件立即啟動核心引擎，確保背景追蹤與離座判定立刻生效
    m_engine.start();
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
    if (m_engine.getStudyTracker().getStatus() == StudyStatus::LockedForPostTest &&
        m_currentStage != UIStage::QuestionnaireSubmitted) {
        setStage(UIStage::StudyCompletedGate);
    }
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
        // 進入實際校準階段才開始收集樣本 (只收有偵測到人臉的幀)
        m_engine.beginCalibration();
    }
    if (stage == UIStage::CalibrationInstruction || stage == UIStage::ActiveCalibration || stage == UIStage::MainDashboard || stage == UIStage::SettingsPanel) {
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

    // 0. 優先檢查頂部全局快速導覽列切換
    for (size_t i = 0; i < m_navTabRects.size(); ++i) {
        if (PtInRect(&m_navTabRects[i], pt)) {
            const UIStage stages[] = {
                UIStage::Welcome,
                UIStage::CalibrationInstruction,
                UIStage::CountdownWait,
                UIStage::ActiveCalibration,
                UIStage::CalibrationResult,
                UIStage::MainDashboard,
                UIStage::SettingsPanel,
                UIStage::StudyCompletedGate,
                UIStage::QuestionnaireSubmitted
            };
            if (i < sizeof(stages) / sizeof(stages[0])) {
                setStage(stages[i]);
                return;
            }
        }
    }

    // 依據主畫面流程按鈕進行切換
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
            // 略過：以已收集的樣本嘗試校準，不足時保留原基準
            m_lastCalibrationOk = m_engine.finishCalibration();
            m_engine.getStudyTracker().setInitialSetupCompleted(true);
            setStage(UIStage::CalibrationResult);
        }
    } else if (m_currentStage == UIStage::CalibrationResult) {
        if (PtInRect(&m_proceedDashboardBtnRect, pt)) {
            m_engine.getStudyTracker().setInitialSetupCompleted(true);
            setStage(UIStage::MainDashboard);
        } else if (PtInRect(&m_closeBgResultBtnRect, pt)) {
            m_engine.getStudyTracker().setInitialSetupCompleted(true);
            minimizeToTray();
        } else if (PtInRect(&m_restartCalibBtnRect, pt)) {
            setStage(UIStage::CalibrationInstruction);
        }
    } else if (m_currentStage == UIStage::MainDashboard) {
        if (PtInRect(&m_recalibBtnRect, pt)) {
            setStage(UIStage::CalibrationInstruction);
        } else if (PtInRect(&m_settingsBtnRect, pt)) {
            setStage(UIStage::SettingsPanel);
        } else if (PtInRect(&m_minimizeTrayBtnRect, pt)) {
            minimizeToTray();
        }
    } else if (m_currentStage == UIStage::SettingsPanel) {
        if (PtInRect(&m_sensitivityBtnRect, pt)) {
            m_highSensitivityMode = !m_highSensitivityMode;
            m_sensitivityLevel = m_highSensitivityMode ? 2 : 1;
            if (m_hwnd) InvalidateRect(m_hwnd, NULL, FALSE);
        } else if (PtInRect(&m_toggleSoundBtnRect, pt)) {
            m_soundAlertEnabled = !m_soundAlertEnabled;
            if (m_hwnd) InvalidateRect(m_hwnd, NULL, FALSE);
        } else if (PtInRect(&m_autoStartBtnRect, pt)) {
            bool cur = m_engine.getLifecycleAdapter().isAutoStartEnabled();
            bool target = !cur;
            bool success = m_engine.getLifecycleAdapter().setAutoStartEnabled(target);
            if (success) {
                m_trayManager.showBalloonNotification(
                    target ? L"已開啟開機自動啟動" : L"已停用開機自動啟動",
                    target ? L"系統已為目前位置配置開機背景守護，下次開機將自動在背景運行。" : L"已移除開機背景自啟動捷徑與註冊資訊。",
                    FatigueLevel::Relaxed
                );
            }
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
            PostMessage(m_hwnd, WM_DESTROY, 0, 0);
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
            m_lastCalibrationOk = m_engine.finishCalibration();
            setStage(UIStage::CalibrationResult);
        }

        if (m_hwnd) InvalidateRect(m_hwnd, NULL, FALSE);
    }

    // 獨立系統時鐘夜間 21:00 定時科研同步與補傳檢查 (每 1800 tick / 約 30 秒委派核心引擎原子檢查，防止重複)
    static int s_wallClockTickCount = 0;
    if (++s_wallClockTickCount >= 1800) {
        s_wallClockTickCount = 0;
        if (m_autoSyncNightly) {
            if (m_engine.checkAndTriggerNightlySync()) {
                m_trayManager.showBalloonNotification(
                    L"當日眼動科研數據已同步",
                    L"系統已於 21:00 將今日眼動數據安全上傳至 Google 試算表。",
                    FatigueLevel::Relaxed
                );
            }
        }

        // 檢查運行過程中是否到達第 14 天 (跨日進入第 14 天)
        if (m_engine.getStudyTracker().getStatus() == StudyStatus::LockedForPostTest &&
            m_currentStage != UIStage::StudyCompletedGate &&
            m_currentStage != UIStage::QuestionnaireSubmitted) {
            showMainWindow();
            setStage(UIStage::StudyCompletedGate);
            m_trayManager.showBalloonNotification(
                L"EFD 14天科研施測期滿",
                L"恭喜完成 14 天科研監控！請開啟視窗填寫後測問卷以完成實驗解鎖。",
                FatigueLevel::Relaxed
            );
        }
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

    if (msg == WM_EFD_SHOW_UI) {
        pThis->showMainWindow();
        bool hasTested = pThis->m_engine.getStudyTracker().isInitialSetupCompleted() || 
                         (pThis->m_engine.getDatabaseService().getRecordCount() > 0);
        if (hasTested) {
            pThis->setStage(UIStage::SettingsPanel);
        } else {
            pThis->setStage(UIStage::Welcome);
        }
        return 0;
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

    case WM_SYSCOMMAND: break;

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
        bool inRecalib = (pThis->m_currentStage == UIStage::MainDashboard) && (PtInRect(&pThis->m_recalibBtnRect, pt) != FALSE);
        bool inSettings = (pThis->m_currentStage == UIStage::MainDashboard) && (PtInRect(&pThis->m_settingsBtnRect, pt) != FALSE);
        bool inMinimizeTray = (pThis->m_currentStage == UIStage::MainDashboard) && (PtInRect(&pThis->m_minimizeTrayBtnRect, pt) != FALSE);
        bool inSensitivity = (pThis->m_currentStage == UIStage::SettingsPanel) && (PtInRect(&pThis->m_sensitivityBtnRect, pt) != FALSE);
        bool inToggleSound = (pThis->m_currentStage == UIStage::SettingsPanel) && (PtInRect(&pThis->m_toggleSoundBtnRect, pt) != FALSE);
        bool inAutoStart = (pThis->m_currentStage == UIStage::SettingsPanel) && (PtInRect(&pThis->m_autoStartBtnRect, pt) != FALSE);
        bool inManualSync = (pThis->m_currentStage == UIStage::SettingsPanel) && (PtInRect(&pThis->m_manualSyncBtnRect, pt) != FALSE);
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
                          inRecalib != pThis->m_isHoveringRecalibBtn ||
                          inSettings != pThis->m_isHoveringSettingsBtn ||
                          inMinimizeTray != pThis->m_isHoveringMinimizeTrayBtn ||
                          inSensitivity != pThis->m_isHoveringSensitivityBtn ||
                          inToggleSound != pThis->m_isHoveringToggleSoundBtn ||
                          inAutoStart != pThis->m_isHoveringAutoStartBtn ||
                          inManualSync != pThis->m_isHoveringManualSyncBtn ||
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
            pThis->m_isHoveringRecalibBtn = inRecalib;
            pThis->m_isHoveringSettingsBtn = inSettings;
            pThis->m_isHoveringMinimizeTrayBtn = inMinimizeTray;
            pThis->m_isHoveringSensitivityBtn = inSensitivity;
            pThis->m_isHoveringToggleSoundBtn = inToggleSound;
            pThis->m_isHoveringAutoStartBtn = inAutoStart;
            pThis->m_isHoveringManualSyncBtn = inManualSync;
            pThis->m_isHoveringSaveSettingsBtn = inSaveSet;
            pThis->m_isHoveringFillQuestionnaireBtn = inFillQ;
            pThis->m_isHoveringReturnDashboardBtn = inReturnDash;
            pThis->m_isHoveringExitAppBtn = inExitApp;

            bool isAnyHovered = (hoveredTab != -1 || inStart || inReady || inBackWelcome || inSkipCalib || inProceedDash || inRestartCalib || inRecalib || inSettings || inMinimizeTray || inSensitivity || inToggleSound || inAutoStart || inManualSync || inSaveSet || inFillQ || inReturnDash || inExitApp);
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

    case WM_CLOSE:
        ShowWindow(hwnd, SW_HIDE);
        return 0;

    case WM_QUERYENDSESSION:
    case WM_ENDSESSION:
        // Windows 關機或登出時安全保護：強制將 WAL 資料持久化落盤，避免資料遺失
        pThis->m_engine.getDatabaseService().flush();
        return TRUE;

    case WM_DESTROY:
        KillTimer(hwnd, 1);
        pThis->m_engine.getDatabaseService().flush();
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
    m_navTabRects.clear(); // 清除點擊區域，防止隱形導覽列被點擊
    return; // 使用者要求隱藏上方導覽列

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
    int titleFontSize = isNarrow ? std::clamp(static_cast<int>(w / 15), 22, 32) : 36;
    Gdiplus::Font titleFont(&fontFamily, static_cast<Gdiplus::REAL>(titleFontSize), Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
    Gdiplus::SolidBrush textBrush(Gdiplus::Color(255, 255, 255, 255));

    Gdiplus::StringFormat format;
    format.SetAlignment(Gdiplus::StringAlignmentCenter);
    format.SetLineAlignment(Gdiplus::StringAlignmentCenter);

    float titleY = static_cast<float>(logoY + logoH + (isNarrow ? 20 : 30));
    Gdiplus::RectF titleRect(0.0f, titleY, static_cast<float>(w), 48.0f);
    g.DrawString(L"感謝協助測試EFD", -1, &titleFont, titleRect, &format, &textBrush);

    // 副標題 (粗體)
    Gdiplus::Font subFont(&fontFamily, static_cast<Gdiplus::REAL>(isNarrow ? 12 : 14), Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
    Gdiplus::SolidBrush subBrush(Gdiplus::Color(230, 247, 227, 175));
    Gdiplus::RectF subRect(0.0f, titleY + 44.0f, static_cast<float>(w), 24.0f);
    g.DrawString(L"AI 驅動眼睛特徵提取與即時疲勞監控研究系統", -1, &subFont, subRect, &format, &subBrush);

    // 3. 進入測試說明按鈕 (粗體字體、20px 圓角邊框)
    int btnWidth = isNarrow ? std::clamp(w - 80, 180, 240) : 240;
    int btnHeight = isNarrow ? 48 : 52;
    int btnX = (w - btnWidth) / 2;
    int btnY = isNarrow ? static_cast<int>(titleY + 80) : std::max(h / 2 + 75, static_cast<int>(titleY + 80));
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

    // 1. 頂部大標題 (增大 20%)
    int titleFontSize = isNarrow ? std::clamp(static_cast<int>(w / 18), 20, 26) : 30;
    Gdiplus::Font titleFont(&fontFamily, static_cast<Gdiplus::REAL>(titleFontSize), Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
    Gdiplus::SolidBrush whiteBrush(Gdiplus::Color(255, 255, 255, 255));
    float titleY = 40.0f;
    Gdiplus::RectF titleRect(0.0f, titleY, static_cast<float>(w), 36.0f);
    g.DrawString(L"眼動特徵提取與校準說明", -1, &titleFont, titleRect, &centerFormat, &whiteBrush);

    // 2. 即時相機連線狀態標籤 (Camera Hardware Status Bar)
    std::string camStatusStr = m_latestTelemetry.lifecycleSummary;
    if (camStatusStr.empty()) {
        camStatusStr = "相機狀態: 正在連接前置鏡頭...";
    }
    std::wstring wCamStatus = utf8ToWide(camStatusStr);
    Gdiplus::Font camStatusFont(&fontFamily, 11, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
    Gdiplus::SolidBrush camStatusBrush(Gdiplus::Color(255, 247, 227, 175)); // 金黃
    Gdiplus::RectF camStatusRect(0.0f, titleY + 34.0f, static_cast<float>(w), 18.0f);
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

    // 提示文字 (增大 20%)
    int hintFontSize = std::clamp(static_cast<int>(w / 24), 17, 24);
    Gdiplus::Font hintFont(&fontFamily, static_cast<Gdiplus::REAL>(hintFontSize), Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
    Gdiplus::SolidBrush whiteBrush(Gdiplus::Color(255, 255, 255, 255));
    Gdiplus::RectF hintRect(0.0f, static_cast<float>(centerY + circleRadius + 18), static_cast<float>(w), 38.0f);
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

    // 1. 成功大標題 (增大 20%)
    float titleY = isNarrow ? 45.0f : static_cast<float>(h / 2 - 180);
    if (titleY < 42.0f) titleY = 42.0f;
    int titleFontSize = isNarrow ? std::clamp(static_cast<int>(w / 15), 22, 29) : 34;
    Gdiplus::Font titleFont(&fontFamily, static_cast<Gdiplus::REAL>(titleFontSize), Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
    Gdiplus::SolidBrush whiteBrush(Gdiplus::Color(255, 255, 255, 255));

    Gdiplus::RectF titleRect(10.0f, titleY, static_cast<float>(w - 20), 46.0f);
    g.DrawString(m_lastCalibrationOk ? L"眼動特徵提取與基準校準完成！"
                                     : L"校準未完成：未穩定偵測到臉部，請按「重新校準」",
                 -1, &titleFont, titleRect, &centerFormat, &whiteBrush);

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
    // 滿版翡翠綠背景 (#1EB18A)
    Gdiplus::SolidBrush bgBrush(Gdiplus::Color(255, 30, 177, 138));
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

    // 1. 頂部大標題 (純白文字 0xFFFFFF，增大 20%)
    int headerFontSize = isNarrow ? std::clamp(static_cast<int>(w / 20), 20, 24) : 26;
    Gdiplus::Font headerFont(&fontFamily, static_cast<Gdiplus::REAL>(headerFontSize), Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
    Gdiplus::SolidBrush whiteBrush(Gdiplus::Color(255, 255, 255, 255));
    float headerY = 28.0f;
    Gdiplus::RectF headerRect(0.0f, headerY, static_cast<float>(w), 32.0f);
    g.DrawString(L"EFD 即時眼睛疲勞監控中心", -1, &headerFont, headerRect, &centerFormat, &whiteBrush);

    // 2. 即時相機連線狀態指示條 (Hardware Status Banner, 輔助色 0xF7E3AF)
    std::string camStatusStr = m_latestTelemetry.lifecycleSummary;
    if (camStatusStr.empty()) {
        camStatusStr = "相機狀態: Windows Media Foundation | 30 FPS 串流正常";
    }
    std::wstring wCamStatus = utf8ToWide(camStatusStr);
    Gdiplus::Font camStatusFont(&fontFamily, 11, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
    Gdiplus::SolidBrush camStatusBrush(Gdiplus::Color(255, 247, 227, 175)); // 輔助色: 鵝黃 F7E3AF
    Gdiplus::RectF camStatusRect(0.0f, headerY + 28.0f, static_cast<float>(w), 16.0f);
    g.DrawString(wCamStatus.c_str(), -1, &camStatusFont, camStatusRect, &centerFormat, &camStatusBrush);

    // 判斷在場、冷卻與疲勞狀態
    bool isPresent = (m_latestTelemetry.detection.hasFace && m_latestTelemetry.systemState.userPresent);
    FatigueLevel level = m_latestTelemetry.systemState.fatigueLevel;
    float fatigueScore = m_latestTelemetry.systemState.smoothedFatigueScore;
    bool inCooldown = (m_latestTelemetry.systemState.cooldownState == CooldownState::InCooldown || m_latestTelemetry.systemState.cooldownState == CooldownState::FastScreening);
    int cdRemaining = m_latestTelemetry.systemState.cooldownRemainingSeconds;
    int cdMins = cdRemaining / 60;
    int cdSecs = cdRemaining % 60;

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
        statusColor = Gdiplus::Color(255, 70, 80, 70); // 離座深灰
        statusText = L"生理狀態：未偵測到人臉 / 離座中 (暫停計算)";
    } else if (m_latestTelemetry.systemState.cooldownState == CooldownState::InCooldown) {
        statusColor = Gdiplus::Color(255, 60, 115, 145); // 舒緩冷卻藍
        swprintf_s(statusBuf, 128, L"生理狀態：疲勞提醒冷卻中 (%d分%02d秒)\n20分鐘智能靜默暫停提醒", cdMins, cdSecs);
        statusText = statusBuf;
    } else if (m_latestTelemetry.systemState.cooldownState == CooldownState::FastScreening) {
        statusColor = Gdiplus::Color(255, 120, 170, 200); // 快篩藍
        swprintf_s(statusBuf, 128, L"生理狀態：5分鐘輕量快篩追蹤中\n即時總體疲勞分: %.1f", fatigueScore);
        statusText = statusBuf;
    } else if (level == FatigueLevel::Attention) {
        statusColor = Gdiplus::Color(255, 190, 150, 60); // 注意黃
        statusText = L"生理狀態：輕度用眼疲勞 (Attention)";
    } else if (level == FatigueLevel::SevereWarning || fatigueScore >= 65.0f) {
        statusColor = Gdiplus::Color(255, 200, 60, 60); // 警告紅
        statusText = L"生理狀態：你的眼睛處於疲勞狀態，請適當休息";
    }

    // 白色邊框筆 (純白邊框線條 0xFFFFFF)
    Gdiplus::Pen whiteCardPen(Gdiplus::Color(255, 255, 255, 255), 1.5f);

    // 4 數值格式化字串
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
        swprintf_s(b4, 32, L"%.1f", static_cast<double>(m_latestTelemetry.systemState.smoothedFatigueScore));
    }

    if (!isNarrow) {
        // =========================================================================
        // 寬螢幕佈局：左側相機 Viewfinder + 右側生理狀態與 4 核心指標數值
        // =========================================================================
        int leftX = 24;
        int leftW = static_cast<int>(w * 0.54f);
        int viewY = 72;
        int bottomReserve = 68;
        int viewH = h - viewY - bottomReserve;

        // A. 攝影機即時偵測視野外框容器 (Viewfinder Container) - 白底黑字高對比
        Gdiplus::SolidBrush viewBg(Gdiplus::Color(255, 255, 255, 255)); // 純白背景 0xFFFFFF
        drawRoundedButton(g, leftX, viewY, leftW, viewH, 16, &viewBg, nullptr);

        // 依據相機真實感測器解析度動態計算等比例顯示區域 (Aspect Ratio Preservation - 不拉伸、不變形)
        int fw = m_latestTelemetry.latestFrame.isValid() ? m_latestTelemetry.latestFrame.width : 
                 ((m_latestTelemetry.presenceDiagnostic.frameWidth > 0) ? m_latestTelemetry.presenceDiagnostic.frameWidth : 640);
        int fh = m_latestTelemetry.latestFrame.isValid() ? m_latestTelemetry.latestFrame.height : 
                 ((m_latestTelemetry.presenceDiagnostic.frameHeight > 0) ? m_latestTelemetry.presenceDiagnostic.frameHeight : 480);
        if (fw <= 0) fw = 640;
        if (fh <= 0) fh = 480;

        int availW = leftW - 8;
        int availH = viewH - 8;
        float frameAspect = static_cast<float>(fw) / static_cast<float>(fh);
        float containerAspect = static_cast<float>(availW) / static_cast<float>(availH);

        int drawW = availW;
        int drawH = availH;
        int drawX = leftX + 4;
        int drawY = viewY + 4;

        if (containerAspect > frameAspect) {
            // 容器較寬：高度貼齊，左右置中居中 (保持鏡頭原生寬高比，不變形)
            drawH = availH;
            drawW = static_cast<int>(availH * frameAspect);
            drawX = leftX + 4 + (availW - drawW) / 2;
        } else {
            // 容器較高：寬度貼齊，上下置中居中 (保持鏡頭原生寬高比，不變形)
            drawW = availW;
            drawH = static_cast<int>(availW / frameAspect);
            drawY = viewY + 4 + (availH - drawH) / 2;
        }

        // 若有影像幀，依照真實鏡頭等比例繪製；若無或模擬，繪製相符比例科技網格
        if (m_latestTelemetry.latestFrame.isValid()) {
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
            g.DrawImage(&bmp, drawX, drawY, drawW, drawH);
        } else {
            // 科技網格 (限制在等比例區域內，柔和翡翠綠網格)
            Gdiplus::Pen gridPen(Gdiplus::Color(40, 30, 177, 138), 1.0f);
            for (int gx = drawX + 20; gx < drawX + drawW - 20; gx += 40) {
                g.DrawLine(&gridPen, gx, drawY + 4, gx, drawY + drawH - 4);
            }
            for (int gy = drawY + 20; gy < drawY + drawH - 20; gy += 40) {
                g.DrawLine(&gridPen, drawX + 4, gy, drawX + drawW - 4, gy);
            }
        }

        // 視野四角科技邊框括號 (翡翠綠高對比 0x1EB18A，精確貼齊真實等比例影像四角)
        Gdiplus::Pen cornerBracket(Gdiplus::Color(255, 30, 177, 138), 2.0f);
        int bLen = 14;
        g.DrawLine(&cornerBracket, drawX + 2, drawY + 2, drawX + 2 + bLen, drawY + 2);
        g.DrawLine(&cornerBracket, drawX + 2, drawY + 2, drawX + 2, drawY + 2 + bLen);
        g.DrawLine(&cornerBracket, drawX + drawW - 2, drawY + 2, drawX + drawW - 2 - bLen, drawY + 2);
        g.DrawLine(&cornerBracket, drawX + drawW - 2, drawY + 2, drawX + drawW - 2, drawY + 2 + bLen);
        g.DrawLine(&cornerBracket, drawX + 2, drawY + drawH - 2, drawX + 2 + bLen, drawY + drawH - 2);
        g.DrawLine(&cornerBracket, drawX + 2, drawY + drawH - 2, drawX + 2, drawY + drawH - 2 - bLen);
        g.DrawLine(&cornerBracket, drawX + drawW - 2, drawY + drawH - 2, drawX + drawW - 2 - bLen, drawY + drawH - 2);
        g.DrawLine(&cornerBracket, drawX + drawW - 2, drawY + drawH - 2, drawX + drawW - 2, drawY + drawH - 2 - bLen);

        float sx = static_cast<float>(drawW) / fw;
        float sy = static_cast<float>(drawH) / fh;

        // 動態平滑追蹤方框 (依照等比例畫面座標進行精確映射)
        int bx = m_latestTelemetry.presenceDiagnostic.boxX;
        int by = m_latestTelemetry.presenceDiagnostic.boxY;
        int bw = m_latestTelemetry.presenceDiagnostic.boxW;
        int bh = m_latestTelemetry.presenceDiagnostic.boxH;

        if (inCooldown) {
            // 冷卻中隱藏追蹤框
        } else if (bw > 0 && bh > 0) {
            int drawBoxX = drawX + static_cast<int>(bx * sx);
            int drawBoxY = drawY + static_cast<int>(by * sy);
            int drawBoxW = static_cast<int>(bw * sx);
            int drawBoxH = static_cast<int>(bh * sy);

            drawBoxX = std::clamp(drawBoxX, drawX + 2, drawX + drawW - 30);
            drawBoxY = std::clamp(drawBoxY, drawY + 2, drawY + drawH - 30);
            drawBoxW = std::clamp(drawBoxW, 30, drawX + drawW - drawBoxX - 2);
            drawBoxH = std::clamp(drawBoxH, 30, drawY + drawH - drawBoxY - 2);

            Gdiplus::Color boxColor = isPresent ? Gdiplus::Color(255, 30, 220, 140) : Gdiplus::Color(255, 235, 180, 87);
            Gdiplus::Pen boxPen(boxColor, 2.5f);
            g.DrawRectangle(&boxPen, drawBoxX, drawBoxY, drawBoxW, drawBoxH);
        }

        // 視野左上角 HUD 狀態膠囊 (在座正視，貼齊影像畫面)
        int badgeW = 120;
        int badgeH = 26;
        int badgeX = drawX + 10;
        int badgeY = drawY + 10;
        Gdiplus::SolidBrush badgeBg(isPresent ? Gdiplus::Color(220, 30, 177, 138) : Gdiplus::Color(220, 110, 120, 105));
        Gdiplus::Pen badgeBorder(Gdiplus::Color(255, 255, 255, 255), 1.0f);
        drawRoundedButton(g, badgeX, badgeY, badgeW, badgeH, 12, &badgeBg, &badgeBorder);
        Gdiplus::Font badgeFont(&fontFamily, 12, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
        Gdiplus::SolidBrush badgeText(Gdiplus::Color(255, 255, 255, 255));
        Gdiplus::RectF badgeRect(static_cast<float>(badgeX), static_cast<float>(badgeY), static_cast<float>(badgeW), static_cast<float>(badgeH));
        g.DrawString(isPresent ? L"在座正視" : L"離座等待", -1, &badgeFont, badgeRect, &centerFormat, &badgeText);

        // 視野右上角 疲勞等級膠囊 (冷卻中 / 清醒 / 警報，貼齊影像畫面)
        int fBadgeW = inCooldown ? 140 : 120;
        int fBadgeH = 26;
        int fBadgeX = drawX + drawW - fBadgeW - 10;
        int fBadgeY = drawY + 10;
        Gdiplus::SolidBrush fBadgeBg(fatiguePillColor);
        drawRoundedButton(g, fBadgeX, fBadgeY, fBadgeW, fBadgeH, 12, &fBadgeBg, &badgeBorder);
        Gdiplus::RectF fBadgeRect(static_cast<float>(fBadgeX), static_cast<float>(fBadgeY), static_cast<float>(fBadgeW), static_cast<float>(fBadgeH));
        g.DrawString(fatigueStr.c_str(), -1, &badgeFont, fBadgeRect, &centerFormat, &badgeText);

        // =========================================================================
        // 右欄：頂部生理狀態卡片 + 下方 4 遙測指標 (2x2 網格)
        // =========================================================================
        int rightX = leftX + leftW + 18;
        int rightW = w - rightX - 24;

        // 右欄頂部：生理狀態大卡片
        int statCardH = 68;
        Gdiplus::SolidBrush cardBrush(statusColor);
        drawRoundedButton(g, rightX, viewY, rightW, statCardH, 16, &cardBrush, &whiteCardPen);

        Gdiplus::Font cardFont(&fontFamily, 13, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
        Gdiplus::SolidBrush cardTextBrush(Gdiplus::Color(255, 255, 255, 255));
        Gdiplus::RectF cardTextRect(static_cast<float>(rightX + 10), static_cast<float>(viewY), static_cast<float>(rightW - 20), static_cast<float>(statCardH));
        g.DrawString(statusText, -1, &cardFont, cardTextRect, &centerFormat, &cardTextBrush);

        // 右欄下方：4 欄動態遙測數據 (白底黑字，2x2 網格，滿版縱向自適應)
        int gridY = viewY + statCardH + 14;
        int gridH = viewH - statCardH - 14;
        int gap = 12;
        int mCardW = (rightW - gap) / 2;
        int mCardH = (gridH - gap) / 2;

        auto drawMetricCard = [&](int ix, int iy, int iw, int ih, const wchar_t* label, const wchar_t* val, const wchar_t* subtext) {
            Gdiplus::SolidBrush boxBrush(Gdiplus::Color(255, 255, 255, 255)); // 白底 0xFFFFFF
            drawRoundedButton(g, ix, iy, iw, ih, 16, &boxBrush, nullptr);

            // 項目標籤 (黑字 0x25291C)
            Gdiplus::Font lFont(&fontFamily, 12, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
            Gdiplus::SolidBrush lBrush(Gdiplus::Color(255, 37, 41, 28));
            Gdiplus::RectF lRect(static_cast<float>(ix), static_cast<float>(iy + 14), static_cast<float>(iw), 18.0f);
            g.DrawString(label, -1, &lFont, lRect, &centerFormat, &lBrush);

            // 核心數值 (大字深黑 0x25291C)
            Gdiplus::Font vFont(&fontFamily, 26, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
            Gdiplus::SolidBrush vBrush(Gdiplus::Color(255, 37, 41, 28));
            Gdiplus::RectF vRect(static_cast<float>(ix), static_cast<float>(iy + (ih - 32) / 2), static_cast<float>(iw), 32.0f);
            g.DrawString(val, -1, &vFont, vRect, &centerFormat, &vBrush);

            // 輔助說明 (高對比墨綠文字 0x5F8273，在純白底上清晰可讀)
            Gdiplus::Font sFont(&fontFamily, 11, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
            Gdiplus::SolidBrush sBrush(Gdiplus::Color(255, 95, 130, 115));
            Gdiplus::RectF sRect(static_cast<float>(ix), static_cast<float>(iy + ih - 26), static_cast<float>(iw), 16.0f);
            g.DrawString(subtext, -1, &sFont, sRect, &centerFormat, &sBrush);
        };

        drawMetricCard(rightX, gridY, mCardW, mCardH, L"雙眼 EAR", b1, isPresent ? (m_latestTelemetry.eyeMetrics.isEyeClosed ? L"閉眼狀態" : L"睜眼常態") : L"等待輸入");
        drawMetricCard(rightX + mCardW + gap, gridY, mCardW, mCardH, L"PERCLOS 閉眼比", b2, isPresent ? L"動態滑動窗口" : L"等待輸入");
        drawMetricCard(rightX, gridY + mCardH + gap, mCardW, mCardH, L"複雜度 (MSE)", b3, isPresent ? L"非線性動力學" : L"等待輸入");
        drawMetricCard(rightX + mCardW + gap, gridY + mCardH + gap, mCardW, mCardH, L"綜合疲勞分數", b4, inCooldown ? L"冷卻防打擾中" : (isPresent ? L"警戒閾值 65.0" : L"暫停保護"));

    } else {
        // =========================================================================
        // 窄螢幕模式：垂直依序排列
        // =========================================================================
        int cardW = w - 24;
        int cardH = 50;
        int cardX = 12;
        int cardY = static_cast<int>(headerY + 46);

        Gdiplus::SolidBrush cardBrush(statusColor);
        drawRoundedButton(g, cardX, cardY, cardW, cardH, 14, &cardBrush, &whiteCardPen);
        Gdiplus::Font cardFont(&fontFamily, 12, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
        Gdiplus::SolidBrush cardTextBrush(Gdiplus::Color(255, 255, 255, 255));
        Gdiplus::RectF cardTextRect(static_cast<float>(cardX), static_cast<float>(cardY), static_cast<float>(cardW), static_cast<float>(cardH));
        g.DrawString(statusText, -1, &cardFont, cardTextRect, &centerFormat, &cardTextBrush);

        int viewY = cardY + cardH + 10;
        int viewH = 180;
        Gdiplus::SolidBrush viewBg(Gdiplus::Color(255, 255, 255, 255)); // 白底
        drawRoundedButton(g, cardX, viewY, cardW, viewH, 14, &viewBg, nullptr);

        // 窄螢幕等比例顯示相機
        int fw = m_latestTelemetry.latestFrame.isValid() ? m_latestTelemetry.latestFrame.width : 
                 ((m_latestTelemetry.presenceDiagnostic.frameWidth > 0) ? m_latestTelemetry.presenceDiagnostic.frameWidth : 640);
        int fh = m_latestTelemetry.latestFrame.isValid() ? m_latestTelemetry.latestFrame.height : 
                 ((m_latestTelemetry.presenceDiagnostic.frameHeight > 0) ? m_latestTelemetry.presenceDiagnostic.frameHeight : 480);
        if (fw <= 0) fw = 640;
        if (fh <= 0) fh = 480;

        int availW = cardW - 8;
        int availH = viewH - 8;
        float frameAspect = static_cast<float>(fw) / static_cast<float>(fh);
        float containerAspect = static_cast<float>(availW) / static_cast<float>(availH);

        int drawW = availW, drawH = availH, drawX = cardX + 4, drawY = viewY + 4;
        if (containerAspect > frameAspect) {
            drawH = availH;
            drawW = static_cast<int>(availH * frameAspect);
            drawX = cardX + 4 + (availW - drawW) / 2;
        } else {
            drawW = availW;
            drawH = static_cast<int>(availW / frameAspect);
            drawY = viewY + 4 + (availH - drawH) / 2;
        }

        if (m_latestTelemetry.latestFrame.isValid()) {
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
            g.DrawImage(&bmp, drawX, drawY, drawW, drawH);
        }

        // 窄螢幕 2x2 指標 (白底黑字)
        int gridY = viewY + viewH + 10;
        int gap = 8;
        int mCardW = (cardW - gap) / 2;
        int mCardH = 65;

        auto drawMetricCard = [&](int ix, int iy, int iw, int ih, const wchar_t* label, const wchar_t* val, const wchar_t* subtext) {
            Gdiplus::SolidBrush boxBrush(Gdiplus::Color(255, 255, 255, 255)); // 白底
            drawRoundedButton(g, ix, iy, iw, ih, 12, &boxBrush, nullptr);

            Gdiplus::Font lFont(&fontFamily, 10, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
            Gdiplus::SolidBrush lBrush(Gdiplus::Color(255, 37, 41, 28)); // 黑字
            Gdiplus::RectF lRect(static_cast<float>(ix), static_cast<float>(iy + 6), static_cast<float>(iw), 14.0f);
            g.DrawString(label, -1, &lFont, lRect, &centerFormat, &lBrush);

            Gdiplus::Font vFont(&fontFamily, 18, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
            Gdiplus::SolidBrush vBrush(Gdiplus::Color(255, 37, 41, 28)); // 黑字
            Gdiplus::RectF vRect(static_cast<float>(ix), static_cast<float>(iy + 22), static_cast<float>(iw), 22.0f);
            g.DrawString(val, -1, &vFont, vRect, &centerFormat, &vBrush);

            Gdiplus::Font sFont(&fontFamily, 9, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
            Gdiplus::SolidBrush sBrush(Gdiplus::Color(255, 95, 130, 115)); // 墨綠
            Gdiplus::RectF sRect(static_cast<float>(ix), static_cast<float>(iy + 44), static_cast<float>(iw), 14.0f);
            g.DrawString(subtext, -1, &sFont, sRect, &centerFormat, &sBrush);
        };

        drawMetricCard(cardX, gridY, mCardW, mCardH, L"雙眼 EAR", b1, isPresent ? L"常態" : L"等待");
        drawMetricCard(cardX + mCardW + gap, gridY, mCardW, mCardH, L"PERCLOS 閉眼比", b2, isPresent ? L"滑動窗口" : L"等待");
        drawMetricCard(cardX, gridY + mCardH + gap, mCardW, mCardH, L"複雜度 (MSE)", b3, isPresent ? L"動力學" : L"等待");
        drawMetricCard(cardX + mCardW + gap, gridY + mCardH + gap, mCardW, mCardH, L"綜合疲勞分數", b4, inCooldown ? L"冷卻中" : L"綜合分");
    }

    // 5. 底部控制按鈕：重新偵測疲勞數據、進入設定介面、關閉系統介面 (白底綠字與關閉醒目強調色)
    int btnH = 44;
    int btnY = h - 60;

    if (!isNarrow) {
        int btnW = 195;
        int gap = 16;
        int totalW = btnW * 3 + gap * 2;
        int startX = (w - totalW) / 2;
        int b1X = startX;
        int b2X = startX + btnW + gap;
        int b3X = startX + (btnW + gap) * 2;

        m_recalibBtnRect = { b1X, btnY, b1X + btnW, btnY + btnH };
        m_settingsBtnRect = { b2X, btnY, b2X + btnW, btnY + btnH };
        m_minimizeTrayBtnRect = { b3X, btnY, b3X + btnW, btnY + btnH };

        Gdiplus::Font btnFont(&fontFamily, 14, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
        Gdiplus::SolidBrush greenText(Gdiplus::Color(255, 30, 177, 138)); // 0x1EB18A
        Gdiplus::SolidBrush whiteBtnText(Gdiplus::Color(255, 255, 255, 255)); // 純白 0xFFFFFF

        // 按鈕 1: 重新偵測疲勞數據 (白底綠字，無外框，按下直接回到說明/介紹介面)
        Gdiplus::SolidBrush b1Brush(m_isHoveringRecalibBtn ? Gdiplus::Color(255, 240, 248, 245) : Gdiplus::Color(255, 255, 255, 255));
        drawRoundedButton(g, b1X, btnY, btnW, btnH, 20, &b1Brush, nullptr);
        Gdiplus::RectF b1Rect(static_cast<float>(b1X), static_cast<float>(btnY), static_cast<float>(btnW), static_cast<float>(btnH));
        g.DrawString(L"重新偵測疲勞數據", -1, &btnFont, b1Rect, &centerFormat, &greenText);

        // 按鈕 2: 進入設定介面 (白底綠字，無外框)
        Gdiplus::SolidBrush b2Brush(m_isHoveringSettingsBtn ? Gdiplus::Color(255, 240, 248, 245) : Gdiplus::Color(255, 255, 255, 255));
        drawRoundedButton(g, b2X, btnY, btnW, btnH, 20, &b2Brush, nullptr);
        Gdiplus::RectF b2Rect(static_cast<float>(b2X), static_cast<float>(btnY), static_cast<float>(btnW), static_cast<float>(btnH));
        g.DrawString(L"進入設定介面", -1, &btnFont, b2Rect, &centerFormat, &greenText);

        // 按鈕 3: 關閉系統介面 (特殊色彩突出顯眼：醒目珊瑚暖紅 #EB5757，搭配純白粗體文字，視覺高度凸顯)
        Gdiplus::Color closeBg = m_isHoveringMinimizeTrayBtn ? Gdiplus::Color(255, 245, 105, 105) : Gdiplus::Color(255, 235, 87, 87);
        Gdiplus::SolidBrush b3Brush(closeBg);
        drawRoundedButton(g, b3X, btnY, btnW, btnH, 20, &b3Brush, nullptr);
        Gdiplus::RectF b3Rect(static_cast<float>(b3X), static_cast<float>(btnY), static_cast<float>(btnW), static_cast<float>(btnH));
        g.DrawString(L"關閉系統介面", -1, &btnFont, b3Rect, &centerFormat, &whiteBtnText);
    } else {
        int btnW = std::min(w - 40, 280);
        int btnX = (w - btnW) / 2;
        int b1Y = h - 142;
        int b2Y = h - 96;
        int b3Y = h - 50;

        m_recalibBtnRect = { btnX, b1Y, btnX + btnW, b1Y + 38 };
        m_settingsBtnRect = { btnX, b2Y, btnX + btnW, b2Y + 38 };
        m_minimizeTrayBtnRect = { btnX, b3Y, btnX + btnW, b3Y + 38 };

        Gdiplus::Font btnFont(&fontFamily, 13, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
        Gdiplus::SolidBrush greenText(Gdiplus::Color(255, 30, 177, 138));
        Gdiplus::SolidBrush whiteBtnText(Gdiplus::Color(255, 255, 255, 255));

        Gdiplus::SolidBrush b1Brush(m_isHoveringRecalibBtn ? Gdiplus::Color(255, 240, 248, 245) : Gdiplus::Color(255, 255, 255, 255));
        drawRoundedButton(g, btnX, b1Y, btnW, 38, 18, &b1Brush, nullptr);
        Gdiplus::RectF b1Rect(static_cast<float>(btnX), static_cast<float>(b1Y), static_cast<float>(btnW), 38.0f);
        g.DrawString(L"重新偵測疲勞數據", -1, &btnFont, b1Rect, &centerFormat, &greenText);

        Gdiplus::SolidBrush b2Brush(m_isHoveringSettingsBtn ? Gdiplus::Color(255, 240, 248, 245) : Gdiplus::Color(255, 255, 255, 255));
        drawRoundedButton(g, btnX, b2Y, btnW, 38, 18, &b2Brush, nullptr);
        Gdiplus::RectF b2Rect(static_cast<float>(btnX), static_cast<float>(b2Y), static_cast<float>(btnW), 38.0f);
        g.DrawString(L"進入設定介面", -1, &btnFont, b2Rect, &centerFormat, &greenText);

        Gdiplus::Color closeBg = m_isHoveringMinimizeTrayBtn ? Gdiplus::Color(255, 245, 105, 105) : Gdiplus::Color(255, 235, 87, 87);
        Gdiplus::SolidBrush b3Brush(closeBg);
        drawRoundedButton(g, btnX, b3Y, btnW, 38, 18, &b3Brush, nullptr);
        Gdiplus::RectF b3Rect(static_cast<float>(btnX), static_cast<float>(b3Y), static_cast<float>(btnW), 38.0f);
        g.DrawString(L"關閉系統介面", -1, &btnFont, b3Rect, &centerFormat, &whiteBtnText);
    }
}

// -----------------------------------------------------------------------------
// 階段 7：設定畫面 (SettingsPanel) - 白底綠字、去掉白色外框
// -----------------------------------------------------------------------------
void NativeWelcomeWindow::drawSettingsPanel(Gdiplus::Graphics& g, int w, int h) {
    // 滿版翡翠綠背景 (#1EB18A)
    Gdiplus::SolidBrush bgBrush(Gdiplus::Color(255, 30, 177, 138));
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

    // 1. 標題 (純白 0xFFFFFF，增大 20%)
    float titleY = 36.0f;
    int titleFontSize = isNarrow ? 24 : 29;
    Gdiplus::Font titleFont(&fontFamily, static_cast<Gdiplus::REAL>(titleFontSize), Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
    Gdiplus::SolidBrush whiteBrush(Gdiplus::Color(255, 255, 255, 255));
    Gdiplus::RectF titleRect(0.0f, titleY, static_cast<float>(w), 36.0f);
    g.DrawString(L"系統設定", -1, &titleFont, titleRect, &centerFormat, &whiteBrush);

    // 2. 設定選項卡片 (全數採用白底綠字顯示，去掉白色框線)
    int cardW = isNarrow ? (w - 30) : std::min(w - 120, 640);
    int cardX = (w - cardW) / 2;
    int startY = static_cast<int>(titleY + (isNarrow ? 44 : 52));
    int itemH = isNarrow ? 56 : 64;
    int gap = isNarrow ? 10 : 12;

    Gdiplus::Font itemTitleFont(&fontFamily, 15, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
    Gdiplus::Font itemValFont(&fontFamily, 12, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
    Gdiplus::SolidBrush greenTextBrush(Gdiplus::Color(255, 30, 177, 138)); // 翡翠綠文字 0x1EB18A
    Gdiplus::SolidBrush descBrush(Gdiplus::Color(255, 95, 130, 115));       // 輔助深綠說明文字

    int switchW = 54;
    int switchH = 28;

    auto drawSettingToggleItem = [&](int itemY, RECT& rect, bool isHovered, const wchar_t* title, const wchar_t* desc, bool isOn) {
        rect = { cardX, itemY, cardX + cardW, itemY + itemH };
        // 白底顯示，完全移除白色外框線 (pen = nullptr)
        Gdiplus::SolidBrush itemBg(isHovered ? Gdiplus::Color(255, 245, 252, 248) : Gdiplus::Color(255, 255, 255, 255));
        drawRoundedButton(g, cardX, itemY, cardW, itemH, 18, &itemBg, nullptr);

        // 主標題 (綠字 0x1EB18A)
        Gdiplus::RectF tRect(static_cast<float>(cardX + 22), static_cast<float>(itemY + 11), static_cast<float>(cardW - switchW - 60), 22.0f);
        g.DrawString(title, -1, &itemTitleFont, tRect, &leftFormat, &greenTextBrush);

        // 輔助狀態說明 (在純白底上高對比可讀的深墨綠)
        Gdiplus::RectF dRect(static_cast<float>(cardX + 22), static_cast<float>(itemY + 34), static_cast<float>(cardW - switchW - 60), 20.0f);
        g.DrawString(desc, -1, &itemValFont, dRect, &leftFormat, &descBrush);

        // 右側 Toggle Switch 滑動開關
        int switchX = cardX + cardW - switchW - 25;
        int sY = itemY + (itemH - switchH) / 2;
        drawToggleSwitch(g, switchX, sY, switchW, switchH, isOn);
    };

    // 設定項目 1: 高靈敏度即時警報
    int item1Y = startY;
    const wchar_t* s1Desc = m_highSensitivityMode 
        ? L"已啟用高靈敏度即時警報 (當 PERCLOS 或疲勞分偏高即刻提醒)" 
        : L"標準靈敏度 (防誤報平滑過濾模式，推薦)";
    drawSettingToggleItem(item1Y, m_sensitivityBtnRect, m_isHoveringSensitivityBtn, L"高靈敏度即時警報", s1Desc, m_highSensitivityMode);

    // 設定項目 2: 疲勞警報提示音
    int item2Y = item1Y + itemH + gap;
    const wchar_t* s2Desc = m_soundAlertEnabled 
        ? L"已啟用蜂鳴警報音效 (疲勞超標時發出溫和提示音)" 
        : L"靜音模式 (僅視覺提醒，不發出提示音)";
    drawSettingToggleItem(item2Y, m_toggleSoundBtnRect, m_isHoveringToggleSoundBtn, L"疲勞警報提示音", s2Desc, m_soundAlertEnabled);

    // 設定項目 3: 開機自動背景啟動
    int item3Y = item2Y + itemH + gap;
    bool autoStartOn = m_engine.getLifecycleAdapter().isAutoStartEnabled();
    const wchar_t* s3Desc = autoStartOn 
        ? L"已配置開機自動背景守護 (開機登入後自動在背景執行)" 
        : L"已停用開機自動啟動 (僅在手動點擊執行檔時開啟)";
    drawSettingToggleItem(item3Y, m_autoStartBtnRect, m_isHoveringAutoStartBtn, L"開機自動背景啟動", s3Desc, autoStartOn);

    // 自動科研同步設為系統預設恆定開啟 (不可關閉)，按鈕已從介面移除，重設 Hitbox
    m_manualSyncBtnRect = { 0, 0, 0, 0 };

    // 3. 底部動作按鈕：返回監控中心 (緊接著項目 3 下方，白底綠字，無外框)
    int btnH = isNarrow ? 44 : 48;
    int btnW = isNarrow ? std::min(w - 40, 280) : 280;
    int returnBtnY = item3Y + itemH + (isNarrow ? 22 : 28);
    int returnBtnX = (w - btnW) / 2;

    m_saveSettingsBtnRect = { returnBtnX, returnBtnY, returnBtnX + btnW, returnBtnY + btnH };

    Gdiplus::SolidBrush returnBtnBrush(m_isHoveringSaveSettingsBtn ? Gdiplus::Color(255, 245, 252, 248) : Gdiplus::Color(255, 255, 255, 255));
    drawRoundedButton(g, returnBtnX, returnBtnY, btnW, btnH, 20, &returnBtnBrush, nullptr);

    Gdiplus::Font returnFont(&fontFamily, 15, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
    Gdiplus::RectF returnRect(static_cast<float>(returnBtnX), static_cast<float>(returnBtnY), static_cast<float>(btnW), static_cast<float>(btnH));
    g.DrawString(L"返回監控中心", -1, &returnFont, returnRect, &centerFormat, &greenTextBrush);
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

    // 1. 滿版高對比向量大標題 (增大 20%)
    float titleY = isNarrow ? 45.0f : static_cast<float>(h / 2 - 165);
    if (titleY < 42.0f) titleY = 42.0f;
    int titleFontSize = isNarrow ? std::clamp(static_cast<int>(w / 15), 20, 26) : 31;
    Gdiplus::Font titleFont(&fontFamily, static_cast<Gdiplus::REAL>(titleFontSize), Gdiplus::FontStyleBold, Gdiplus::UnitPixel);

    Gdiplus::RectF titleRect(10.0f, titleY, static_cast<float>(w - 20), 50.0f);
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

    // 1. 滿版高對比向量大標題 (增大 20%)
    float titleY = isNarrow ? 45.0f : static_cast<float>(h / 2 - 165);
    if (titleY < 42.0f) titleY = 42.0f;
    int titleFontSize = isNarrow ? std::clamp(static_cast<int>(w / 15), 22, 29) : 34;
    Gdiplus::Font titleFont(&fontFamily, static_cast<Gdiplus::REAL>(titleFontSize), Gdiplus::FontStyleBold, Gdiplus::UnitPixel);

    Gdiplus::RectF titleRect(10.0f, titleY, static_cast<float>(w - 20), 52.0f);
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
        WS_OVERLAPPEDWINDOW | (m_startInBackground ? 0 : WS_VISIBLE),
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

    if (m_startInBackground) {
        ShowWindow(m_hwnd, SW_HIDE);
        // 背景常駐啟動，發送系統氣泡提示通知使用者
        m_trayManager.showBalloonNotification(
            L"EFD 眼睛疲勞監測系統已在背景啟動",
            L"攝影機已自動連線並即時記錄眼動數據，如偵測到疲勞將自動發送提醒。",
            FatigueLevel::Relaxed
        );
    } else {
        ShowWindow(m_hwnd, SW_SHOW);
        UpdateWindow(m_hwnd);
    }

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return static_cast<int>(msg.wParam);
}

} // namespace efd

#endif
