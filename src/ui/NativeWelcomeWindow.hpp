#pragma once

#include "Theme.hpp"
#include "engine/AsyncPipelineEngine.hpp"
#include "platform/windows/SystemTrayManager.hpp"
#include "ui/FatigueFloatingIndicator.hpp"
#include <string>
#include <memory>
#include <vector>
#include <algorithm>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <windowsx.h>
#include <objidl.h>
#include <gdiplus.h>
#endif

namespace efd {

enum class UIStage : uint8_t {
    Welcome,                // 階段 1: 歡迎介面 (Logo + 感謝協助測試EFD + 開始按鈕)
    CalibrationInstruction, // 階段 2: 說明與演示預覽介面 (預覽動畫視窗 + 3大操作指南 + 「我準備好了」按鈕)
    CountdownWait,          // 階段 3: 3 秒倒數計時等待 (3 -> 2 -> 1 -> 開始！)
    ActiveCalibration,      // 階段 4: 實際多點眼動提取 (黃點全螢幕多點移動採樣)
    CalibrationResult,      // 階段 5: 測驗完成提示 (個人化基準數值卡片 + 進入監控中心)
    MainDashboard,          // 階段 6: 即時疲勞監控中心 (動態跳動實時數據)
    SettingsPanel,          // 階段 7: 設定與重測眼動數據畫面 (靈敏度、相機切換、立即重測)
    StudyCompletedGate,     // 階段 8: 施測結束門禁 (資產 5.png: 「施測結束，請填寫後測問卷並解除安裝系統」)
    QuestionnaireSubmitted  // 階段 9: 後測問卷填寫完成 (資產 6.png: 「填寫成功!感謝您協助施測」)
};

class NativeWelcomeWindow {
public:
    explicit NativeWelcomeWindow(int width = 960, int height = 640);
    ~NativeWelcomeWindow();

    int run();
    AsyncPipelineEngine& getEngine() { return m_engine; }

    void showMainWindow();
    void toggleFloatingHUD();
    void minimizeToTray();
    void setStage(UIStage stage);

private:
    int m_width;
    int m_height;
    AsyncPipelineEngine m_engine;
    UIStage m_currentStage = UIStage::Welcome;

    // 即時遙測數據快照 (用於儀表板繪製)
    EngineTelemetry m_latestTelemetry;
    std::string m_dashboardMessage = "EFD 系統即時監控中...";

    // 系統設定狀態
    bool m_soundAlertEnabled = true;
    int m_sensitivityLevel = 1; // 0: 低, 1: 標準, 2: 高
    bool m_syntheticCameraMode = false;

#ifdef _WIN32
    HWND m_hwnd = nullptr;
    ULONG_PTR m_gdiplusToken = 0;
    std::unique_ptr<Gdiplus::Image> m_logoImage;   // 資產 4.png
    std::unique_ptr<Gdiplus::Image> m_asset5Image; // 資產 5.png (施測結束)
    std::unique_ptr<Gdiplus::Image> m_asset6Image; // 資產 6.png (填寫成功)

    // 原生系統托盤與置頂懸浮指標 HUD
    SystemTrayManager m_trayManager;
    FatigueFloatingIndicator m_floatingIndicator;

    // 動畫與時間計算
    float m_animTimeSec = 0.0f;
    float m_stageTimeSec = 0.0f;

    // 階段 2 預覽演示動畫座標
    float m_demoDotX = 0.5f;
    float m_demoDotY = 0.5f;

    // 階段 4 實際校準動畫座標與進度
    float m_targetDotX = 0.5f;
    float m_targetDotY = 0.5f;
    float m_calibrationProgress = 0.0f;

    // 頂部導覽列按鈕區域 (Tab Navigation)
    std::vector<RECT> m_navTabRects;
    int m_hoveredNavTab = -1;

    // 按鈕區域
    RECT m_startBtnRect{};
    RECT m_readyBtnRect{};
    RECT m_backWelcomeBtnRect{};
    RECT m_skipCalibBtnRect{};
    RECT m_proceedDashboardBtnRect{};
    RECT m_closeBgResultBtnRect{};
    RECT m_restartCalibBtnRect{};
    RECT m_recalibBtnRect{};
    RECT m_settingsBtnRect{};
    RECT m_toggleFloatingBtnRect{};
    RECT m_minimizeTrayBtnRect{};
    RECT m_endStudyBtnRect{};
    RECT m_fillQuestionnaireBtnRect{};
    RECT m_returnDashboardBtnRect{};
    RECT m_exitAppBtnRect{};
    RECT m_saveSettingsBtnRect{};
    RECT m_recalibFromSettingsBtnRect{};
    RECT m_toggleSoundBtnRect{};
    RECT m_toggleCamModeBtnRect{};
    RECT m_sensitivityBtnRect{};
    RECT m_testAlertBtnRect{};

    bool m_isHoveringStartBtn = false;
    bool m_isHoveringReadyBtn = false;
    bool m_isHoveringBackWelcomeBtn = false;
    bool m_isHoveringSkipCalibBtn = false;
    bool m_isHoveringProceedDashboardBtn = false;
    bool m_isHoveringCloseBgResultBtn = false;
    bool m_isHoveringRestartCalibBtn = false;
    bool m_isHoveringRecalibBtn = false;
    bool m_isHoveringSettingsBtn = false;
    bool m_isHoveringToggleFloatingBtn = false;
    bool m_isHoveringMinimizeTrayBtn = false;
    bool m_isHoveringEndStudyBtn = false;
    bool m_isHoveringFillQuestionnaireBtn = false;
    bool m_isHoveringReturnDashboardBtn = false;
    bool m_isHoveringExitAppBtn = false;
    bool m_isHoveringSaveSettingsBtn = false;
    bool m_isHoveringRecalibFromSettingsBtn = false;
    bool m_isHoveringToggleSoundBtn = false;
    bool m_isHoveringToggleCamModeBtn = false;
    bool m_isHoveringSensitivityBtn = false;
    bool m_isHoveringTestAlertBtn = false;

    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    void onPaint(HWND hwnd);
    void onTimerTick();
    void handleMouseClick(int x, int y);

    // 繪製頂部全局快速導覽列
    void drawTopNavigationBar(Gdiplus::Graphics& g, int w, int h);

    // 各階段專屬 GDI+ 繪製函式
    void drawWelcomeScreen(Gdiplus::Graphics& g, int w, int h);
    void drawCalibrationInstruction(Gdiplus::Graphics& g, int w, int h);
    void drawCountdownWait(Gdiplus::Graphics& g, int w, int h);
    void drawActiveCalibration(Gdiplus::Graphics& g, int w, int h);
    void drawCalibrationResult(Gdiplus::Graphics& g, int w, int h);
    void drawMainDashboard(Gdiplus::Graphics& g, int w, int h);
    void drawSettingsPanel(Gdiplus::Graphics& g, int w, int h);
    void drawStudyCompletedGate(Gdiplus::Graphics& g, int w, int h);
    void drawQuestionnaireSubmitted(Gdiplus::Graphics& g, int w, int h);
#endif
};

} // namespace efd
