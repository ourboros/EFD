#pragma once

#include "efd/types.hpp"
#include <deque>
#include <chrono>

namespace efd {

// 以「時間」而非「幀數」定義的特徵窗口：
// - PERCLOS = 窗口內閉眼時間 / 窗口總時間 (預設 60 秒)
// - 眨眼 = 連續閉眼 60~800 ms 的片段 (與幀率無關)
// - 眨眼率需窗口累積 >= 30 秒才輸出，之前回傳 0 (代表不計分)
class FeatureExtractor {
public:
    static constexpr float kMinBlinkMs = 60.0f;
    static constexpr float kMaxBlinkMs = 800.0f;
    static constexpr float kMinBlinkRateWindowSec = 30.0f;

    explicit FeatureExtractor(float windowSeconds = 60.0f, float defaultThreshold = 0.21f);

    // 從 6 個關鍵點計算單眼 EAR (Eye Aspect Ratio)
    static float calculateSingleEar(const std::array<Point3D, 6>& eyePoints);

    // 處理新的一幀特徵點 (468 點特徵陣列)；fps 為實測幀率，決定此樣本代表的時間長度
    EyeMetrics processFrame(const std::vector<Point3D>& landmarks, float fps = 30.0f);

    // 處理手動輸入的 EAR 值 (用於測試與模擬串流)
    EyeMetrics processEar(float earLeft, float earRight, float fps = 30.0f);

    // 設定 EAR 閉眼判斷閾值
    void setEyeClosedThreshold(float threshold);
    float getEyeClosedThreshold() const;

    // 取得窗口內 EAR 序列
    std::vector<float> getEarHistory() const;

    // 依時間重新取樣為固定頻率序列 (供 EMD / MSE 使用，消除幀率差異)
    std::vector<float> getEarHistoryResampled(float targetHz) const;

    // 窗口目前涵蓋的秒數
    float getWindowDurationSeconds() const;

    // 重置累積統計
    void reset();

    // 重置單幀暫態計數 (當人臉脫離視野時調用，防止殘留閉眼幀污染下次進入)
    void resetTemporaryCounters();

private:
    struct Sample {
        float ear = 0.0f;
        bool closed = false;
        float durationMs = 0.0f;
    };

    float m_threshold = 0.21f;
    float m_windowMs = 60000.0f;

    std::deque<Sample> m_samples;
    float m_windowTotalMs = 0.0f;
    float m_windowClosedMs = 0.0f;

    bool  m_wasClosed = false;
    float m_currentClosedMs = 0.0f;
    int   m_totalBlinks = 0;
    float m_totalBlinkDurationMs = 0.0f;
};

} // namespace efd
