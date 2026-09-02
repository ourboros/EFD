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
    if (m_currentThreshold < 0.12f) {
        m_currentThreshold = 0.12f;
    }
    m_isCalibrated = true;
}

float AdaptiveBaseline::update(float currentEar) {
    // 僅過濾極端異常值 (例如遮擋鏡頭)
    if (currentEar > 0.05f && currentEar < 0.60f) {
        m_slidingWindow.push_back(currentEar);
        if (m_slidingWindow.size() > m_windowSize) {
            m_slidingWindow.pop_front();
        }
    }

    if (m_slidingWindow.size() >= 30) {
        float mean = std::accumulate(m_slidingWindow.begin(), m_slidingWindow.end(), 0.0f) /
                     static_cast<float>(m_slidingWindow.size());

        float sumSq = 0.0f;
        for (float val : m_slidingWindow) {
            float diff = val - mean;
            sumSq += diff * diff;
        }
        float stdDev = std::sqrt(sumSq / static_cast<float>(m_slidingWindow.size()));

        // 自適應公式: EAR_th(t) = alpha * Baseline + (1 - alpha) * mean - k * stdDev
        float dynamicTarget = (m_alpha * m_baselineEar) + ((1.0f - m_alpha) * mean) - (m_k * stdDev);

        // 安全邊界限制 (避免閾值過高或過低)
        m_currentThreshold = std::clamp(dynamicTarget, 0.12f, m_baselineEar * 0.85f);
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

