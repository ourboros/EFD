#pragma once

#include "efd/types.hpp"
#include <deque>
#include <chrono>

namespace efd {

class FeatureExtractor {
public:
    explicit FeatureExtractor(size_t windowSize = 900, float defaultThreshold = 0.21f);
    // windowSize 預設 900 = 30 FPS * 30 秒視窗，PERCLOS 在 30 秒視窗內計算（文獻標準）

    // 從 6 個關鍵點計算單眼 EAR (Eye Aspect Ratio)
    static float calculateSingleEar(const std::array<Point3D, 6>& eyePoints);

    // 處理新的一幀特徵點 (468 點特徵陣列)
    EyeMetrics processFrame(const std::vector<Point3D>& landmarks, float fps = 30.0f);

    // 處理手動輸入的 EAR 值 (用於測試與模擬串流)
    EyeMetrics processEar(float earLeft, float earRight, float fps = 30.0f);

    // 設定 EAR 閉眼判斷閾值
    void setEyeClosedThreshold(float threshold);
    float getEyeClosedThreshold() const;

    // 取得歷史 EAR 序列 (供 EMD 與 MSE 演算法分析)
    std::vector<float> getEarHistory() const;

    // 取得最近一次有效眨眼的平均持續時間 (ms)
    float getLastBlinkDurationMs() const { return m_lastBlinkDurationMs; }

    // 重置累積統計
    void reset();

private:
    float m_threshold = 0.21f;
    size_t m_maxHistorySize = 900; // 30 秒 @ 30 FPS

    std::deque<float> m_earHistory;
    std::deque<bool>  m_closedHistory; // 供 PERCLOS 計算之閉眼布林序列（30 秒視窗）

    bool  m_wasClosed = false;
    int   m_currentClosedFrames = 0;
    int   m_totalBlinks = 0;
    float m_totalBlinkDurationMs = 0.0f;

    // 60 秒滾動眨眼時間戳記（用於準確計算眨眼率）
    std::deque<float> m_blinkTimestamps; // 各眨眼事件發生時的累計秒數
    float m_elapsedSeconds = 0.0f;       // 已處理的累計秒數

    // 最近一次眨眼平均持續時間（ms），供 FatigueStateMachine 使用
    float m_lastBlinkDurationMs = 150.0f;
};

} // namespace efd
