#pragma once

#include "efd/types.hpp"
#include <string>
#include <functional>
#include <memory>

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

class FatigueFloatingIndicator {
public:
    using RestoreCallback = std::function<void()>;

    explicit FatigueFloatingIndicator(int width = 168, int height = 56);
    ~FatigueFloatingIndicator();

    // 建立並顯示原生置頂懸浮視窗
    bool create(HWND parentHwnd = nullptr);
    void show();
    void hide();
    void toggle();
    bool isVisible() const;

    // 即時更新遙測數據 (線程安全)
    void updateMetrics(float ear, float fatigueScore, FatigueLevel level, const std::string& statusMsg);

    // 註冊雙擊還原主視窗回呼
    void setRestoreCallback(RestoreCallback callback);

private:
#ifdef _WIN32
    HWND m_hwnd = nullptr;
    int m_width;
    int m_height;
    bool m_visible = false;

    float m_ear = 0.312f;
    float m_fatigueScore = 5.0f;
    FatigueLevel m_level = FatigueLevel::Relaxed;
    std::string m_statusMsg = "Relaxed";
    float m_pulseAnim = 0.0f;

    RestoreCallback m_restoreCallback;

    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    void onPaint(HWND hwnd);
    void onTimerTick();
#endif
};

} // namespace efd

