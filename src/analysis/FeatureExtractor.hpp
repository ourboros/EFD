#pragma once

#include "efd/types.hpp"
#include <deque>
#include <chrono>

namespace efd {

class FeatureExtractor {
public:
    // windowSize: 保留供 EMD/MSE 分析的 EAR 樣本數
    // PERCLOS 與眨眼率則以時間定義的 60 秒窗口計算，與幀率無關
    explicit FeatureExtractor(size_t windowSize = 300, float defaultThreshold = 0.21f);

    static constexpr float kPerclosWindowSec   = 60.0f;
    static constexpr float kBlinkRateMinWindowSec = 30.0f; // 窗口累積滿 30 秒才輸出眨眼率
    static constexpr float kMinBlinkSec        = 0.06f;    // 有效眨眼 60 ms ~ 800 ms
    static constexpr float kMaxBlinkSec        = 0.80f;

    // 從 6 個關鍵點計算單眼 EAR (Eye Aspect Ratio)
    static float calculateSingleEar(const std::array<Point3D, 6>& eyePoints);

    // 處理新的一幀特徵點 (468 點特徵陣列)
    // fps: 實測幀率，每個樣本代表 1/fps 秒
    EyeMetrics processFrame(const std::vector<Point3D>& landmarks, float fps = 30.0f);

    // 處理手動輸入的 EAR 值 (用於測試與模擬串流)
    EyeMetrics processEar(float earLeft, float earRight, float fps = 30.0f);

    // 設定 EAR 閉眼判斷閾值
    void setEyeClosedThreshold(float threshold);
    float getEyeClosedThreshold() const;

    // 取得歷史 EAR 序列 (供 EMD 與 MSE 演算法分析)
    std::vector<float> getEarHistory() const;

    // 重置累積統計
    void reset();

    // 重置單幀暫態計數 (當人臉脫離視野時調用，防止殘留閉眼幀污染下次進入)
    void resetTemporaryCounters();

private:
    float m_threshold = 0.21f;
    size_t m_maxHistorySize = 300; // 預設保留 10 秒 @ 30 FPS (或 300 筆)
    
    struct Sample {
        bool  closed;
        float durationSec;
    };

    std::deque<float>  m_earHistory;
    std::deque<Sample> m_samples;          // 60 秒 PERCLOS 窗口
    double m_windowSec = 0.0;               // m_samples 涵蓋的總時間
    double m_closedSec = 0.0;               // m_samples 中閉眼的總時間
    double m_elapsedSec = 0.0;             // 自 reset 起的累計時間
    std::deque<double> m_blinkTimes;       // 有效眨眼結束時刻 (m_elapsedSec)

    bool  m_wasClosed = false;
    float m_currentClosedSec = 0.0f;
    int   m_totalBlinks = 0;
    float m_totalBlinkDurationMs = 0.0f;
};

} // namespace efd

