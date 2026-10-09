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
    m_currentThreshold = std::clamp(m_currentThreshold, m_baselineEar * 0.20f, m_baselineEar * 0.98f);
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
    m_currentThreshold = std::clamp(m_currentThreshold, m_baselineEar * 0.20f, m_baselineEar * 0.98f);
}

float AdaptiveBaseline::update(float currentEar, bool isEyeClosed) {
    if (isEyeClosed) {
        m_consecutiveLowFrames++;
    } else {
        m_consecutiveLowFrames = 0;
    }

    // 改良點 1: 清醒樣本選擇性更新 (Selective Updating)
    // 若當前判定為閉眼或顯著低於閾值，不納入清醒基準線滑動窗口，避免微睡眠拖垮判定閾值
    bool isLikelyAwakeSample = (!isEyeClosed) && 
                               (currentEar >= m_currentThreshold * 0.85f) && 
                               (currentEar < 0.60f);

    // 防卡死與抗微睡眠污染機制：
    // 若長時間判定為閉眼 (例如沉睡、閉目養神或姿勢大幅移動)，不得無限制拉垮基準線。
    // 即使角度改變，m_baselineEar 衰減下限保護在至少 0.18f (或初始校準之 65%)，
    // 杜絕閾值下陷至 0.103 導致閉眼無法被偵測的致命缺陷。
    if (m_consecutiveLowFrames > 90) { // 延長至 3 秒才緩慢微調
        if (currentEar >= 0.18f) {
            isLikelyAwakeSample = true;
            m_baselineEar = std::max(0.18f, m_baselineEar * 0.995f + currentEar * 0.005f);
        }
    }

    if (isLikelyAwakeSample) {
        m_slidingWindow.push_back(currentEar);
        if (m_slidingWindow.size() > m_windowSize) {
            m_slidingWindow.pop_front();
        }
    }

    if (m_slidingWindow.size() >= 20) {
        float mean = std::accumulate(m_slidingWindow.begin(), m_slidingWindow.end(), 0.0f) /
                     static_cast<float>(m_slidingWindow.size());

        // 將閉眼閾值自適應設定在清醒時平均 EAR 的 80% (標準文獻常見之 EAR 萎縮比 75%~85%)
        float dynamicTarget = mean * 0.80f;
        
        // 安全邊界限制：嚴格限制閉眼閾值下限 >= 0.18f，上限 <= m_baselineEar * 0.90f
        // 確保永遠與閉眼下限 (0.100) 保持至少 0.08 的辨別餘裕，杜絕閾值下陷癱瘓閉眼判定
        m_currentThreshold = std::clamp(dynamicTarget, 0.18f, std::max(0.20f, m_baselineEar * 0.90f));
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
