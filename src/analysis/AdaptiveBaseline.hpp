#pragma once

#include "efd/types.hpp"
#include <deque>
#include <vector>

namespace efd {

class AdaptiveBaseline {
public:
    explicit AdaptiveBaseline(float initialBaseline = 0.30f, 
                             float alpha = 0.7f, 
                             float k = 1.5f, 
                             size_t windowSize = 600);

    // 進行初始基準線校準 (例如取使用者最初 3~5 秒靜態清醒睜眼平均)
    void calibrate(const std::vector<float>& awakeEarSamples);

    // 系統休眠喚醒 (Hot-Resume) 快速再校準 (1 秒內快速重同步光線與角度基準)
    void fastRecalibrate(const std::vector<float>& resumeSamples);

    // 選擇性更新動態自適應閾值 (排除閉眼與微睡眠樣本污染基準線)
    float update(float currentEar, bool isEyeClosed = false);

    // 取得當前動態閾值
    float getCurrentThreshold() const;

    // 取得校準基準資料
    CalibrationData getCalibrationData() const;

    // 重置滑動窗口與基準
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
