#pragma once

#include "efd/types.hpp"
#include <deque>
#include <chrono>

namespace efd {

class FeatureExtractor {
public:
    explicit FeatureExtractor(size_t windowSize = 300, float defaultThreshold = 0.21f);

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

    // 重置累積統計
    void reset();

private:
    float m_threshold = 0.21f;
    size_t m_maxHistorySize = 300; // 預設保留 10 秒 @ 30 FPS (或 300 筆)
    
    std::deque<float> m_earHistory;
    std::deque<bool>  m_closedHistory; // 供 PERCLOS 計算之閉眼布林序列

    bool m_wasClosed = false;
    int  m_currentClosedFrames = 0;
    int  m_totalBlinks = 0;
    float m_totalBlinkDurationMs = 0.0f;
};

} // namespace efd

