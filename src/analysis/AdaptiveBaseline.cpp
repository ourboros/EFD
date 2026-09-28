#include "AdaptiveBaseline.hpp"
#include <cmath>
#include <numeric>
#include <algorithm>

namespace efd {

AdaptiveBaseline::AdaptiveBaseline(float initialBaseline, float alpha, float k, size_t windowSize)
    : m_baselineEar(initialBaseline),
      m_alpha(alpha),
      m_k(k),
      m_currentThreshold(initialBaseline * 0.70f),
      m_isCalibrated(false),
      m_windowSize(windowSize) {
}

void AdaptiveBaseline::calibrate(const std::vector<float>& awakeEarSamples) {
    if (awakeEarSamples.empty()) return;

    float sum = std::accumulate(awakeEarSamples.begin(), awakeEarSamples.end(), 0.0f);
    m_baselineEar = sum / static_cast<float>(awakeEarSamples.size());

    float sumSq = 0.0f;
    for (float val : awakeEarSamples) {
        float diff = val - m_baselineEar;
        sumSq += diff * diff;
    }
    float stdDev = std::sqrt(sumSq / static_cast<float>(awakeEarSamples.size()));

    m_currentThreshold = m_baselineEar - (m_k * stdDev);
    if (m_currentThreshold < 0.15f) {
        m_currentThreshold = 0.15f;
    }
    m_isCalibrated = true;

    // 預填充滑動窗口
    m_slidingWindow.clear();
    for (float val : awakeEarSamples) {
        m_slidingWindow.push_back(val);
    }
}

void AdaptiveBaseline::fastRecalibrate(const std::vector<float>& resumeSamples) {
    if (resumeSamples.empty()) return;

    float newAvg = std::accumulate(resumeSamples.begin(), resumeSamples.end(), 0.0f) / 
                   static_cast<float>(resumeSamples.size());

    // 平滑融合新環境光線/角度基準 (50% 歷史 + 50% 新環境)
    m_baselineEar = (m_baselineEar * 0.5f) + (newAvg * 0.5f);

    // 重新調整滑動窗口以快速適應
    m_slidingWindow.clear();
    for (float val : resumeSamples) {
        m_slidingWindow.push_back(val);
    }

    m_currentThreshold = m_baselineEar * 0.70f;
    if (m_currentThreshold < 0.15f) {
        m_currentThreshold = 0.15f;
    }
}

float AdaptiveBaseline::update(float currentEar, bool isEyeClosed) {
    // 改良點 1: 清醒樣本選擇性更新 (Selective Updating)
    // 若當前判定為閉眼或顯著低於閾值，不納入清醒基準線滑動窗口，避免微睡眠拖垮判定閾值
    bool isLikelyAwakeSample = (!isEyeClosed) && 
                               (currentEar >= m_currentThreshold * 0.85f) && 
                               (currentEar < 0.60f);

    if (isLikelyAwakeSample) {
        m_slidingWindow.push_back(currentEar);
        if (m_slidingWindow.size() > m_windowSize) {
            m_slidingWindow.pop_front();
        }
    }

    if (m_slidingWindow.size() >= 20) {
        float mean = std::accumulate(m_slidingWindow.begin(), m_slidingWindow.end(), 0.0f) /
                     static_cast<float>(m_slidingWindow.size());

        float sumSq = 0.0f;
        for (float val : m_slidingWindow) {
            float diff = val - mean;
            sumSq += diff * diff;
        }
        float stdDev = std::sqrt(sumSq / static_cast<float>(m_slidingWindow.size()));

        // 動態自適應公式: EAR_th(t) = alpha * Baseline + (1 - alpha) * mean - k * stdDev
        float dynamicTarget = (m_alpha * m_baselineEar) + ((1.0f - m_alpha) * mean) - (m_k * stdDev);

        // 安全邊界限制 (保持在合理區間，防止崩塌)
        m_currentThreshold = std::clamp(dynamicTarget, 0.15f, m_baselineEar * 0.85f);
    }

    return m_currentThreshold;
}

float AdaptiveBaseline::getCurrentThreshold() const {
    return m_currentThreshold;
}

CalibrationData AdaptiveBaseline::getCalibrationData() const {
    CalibrationData data;
    data.baselineEar = m_baselineEar;
    data.thresholdEar = m_currentThreshold;
    data.isCalibrated = m_isCalibrated;
    return data;
}

void AdaptiveBaseline::reset() {
    m_slidingWindow.clear();
    m_isCalibrated = false;
    m_currentThreshold = m_baselineEar * 0.70f;
}

} // namespace efd
