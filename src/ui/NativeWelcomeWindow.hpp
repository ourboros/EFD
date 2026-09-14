#pragma once

#include "Theme.hpp"
#include "engine/AsyncPipelineEngine.hpp"
#include <string>
#include <memory>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>
#endif

namespace efd {

enum class UIStage : uint8_t {
    Welcome,            // 1. 歡迎介面 (Logo + 感謝協助測試EFD + 開始按鈕)
    CalibrationGuide,   // 2. 眼動數據提取說明 (中央黃點 + 請凝視畫面上的黃點並跟隨他移動)
    ActiveCalibration,  // 3. 眼動數據動態提取 (黃點沿多點平滑移動採樣)
    MainDashboard       // 4. 即時疲勞監控儀表板 (狀態卡片 + 實時 EAR / PERCLOS / 遙測)
};

class NativeWelcomeWindow {
public:
    explicit NativeWelcomeWindow(int width = 960, int height = 640);
    ~NativeWelcomeWindow();

    int run();
    AsyncPipelineEngine& getEngine() { return m_engine; }

private:
    int m_width;
    int m_height;
    AsyncPipelineEngine m_engine;
    UIStage m_currentStage = UIStage::Welcome;

    // 即時遙測數據快照 (用於儀表板繪製)
    EngineTelemetry m_latestTelemetry;
    std::string m_dashboardMessage = "EFD 系統即時監控中...";

#ifdef _WIN32
    HWND m_hwnd = nullptr;
    ULONG_PTR m_gdiplusToken = 0;
    std::unique_ptr<Gdiplus::Image> m_logoImage;

    // 動畫與校準座標計算
    float m_animTimeSec = 0.0f;
    float m_targetDotX = 0.5f; // 0.0 ~ 1.0
    float m_targetDotY = 0.5f; // 0.0 ~ 1.0
    int   m_calibrationPhase = 0;
    float m_calibrationProgress = 0.0f;

    // 按鈕區域
    RECT m_startBtnRect{};
    RECT m_recalibBtnRect{};
    bool m_isHoveringStartBtn = false;
    bool m_isHoveringRecalibBtn = false;

    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    void onPaint(HWND hwnd);
    void onTimerTick();
    void handleMouseClick(int x, int y);

    // 各階段專屬繪製函式 (GDI+)
    void drawWelcomeScreen(Gdiplus::Graphics& g, int w, int h);
    void drawCalibrationGuide(Gdiplus::Graphics& g, int w, int h);
    void drawActiveCalibration(Gdiplus::Graphics& g, int w, int h);
    void drawMainDashboard(Gdiplus::Graphics& g, int w, int h);
#endif
};

} // namespace efd
