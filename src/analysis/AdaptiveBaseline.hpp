#pragma once

#include "efd/types.hpp"
#include <deque>

namespace efd {

class AdaptiveBaseline {
public:
    explicit AdaptiveBaseline(float initialBaseline = 0.30f, 
                             float alpha = 0.7f, 
                             float k = 1.5f, 
                             size_t windowSize = 600);

    // 進行初始校準 (例如取使用者最初 3~5 秒靜態清醒睜眼平均)
    void calibrate(const std::vector<float>& awakeEarSamples);

    // 新增每秒特徵值並更新動態自適應閾值
    float update(float currentEar);

    // 取得當前動態閾值
    float getCurrentThreshold() const;

    // 取得校準資料
    CalibrationData getCalibrationData() const;

    // 重置
    void reset();

private:
    float m_baselineEar = 0.30f;
    float m_alpha = 0.7f;       // 歷史基準衰減係數
    float m_k = 1.5f;           // 標準差倍率
    float m_currentThreshold = 0.21f;
    bool  m_isCalibrated = false;

    size_t m_windowSize = 600;  // 滑動窗口大小
    std::deque<float> m_slidingWindow;
};

} // namespace efd

