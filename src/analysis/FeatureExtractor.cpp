#include "FeatureExtractor.hpp"
#include <cmath>
#include <numeric>

namespace efd {

namespace {
    inline float distance3D(const Point3D& a, const Point3D& b) {
        float dx = a.x - b.x;
        float dy = a.y - b.y;
        float dz = a.z - b.z;
        return std::sqrt(dx * dx + dy * dy + dz * dz);
    }
}

FeatureExtractor::FeatureExtractor(size_t windowSize, float defaultThreshold)
    : m_threshold(defaultThreshold), m_maxHistorySize(windowSize) {
}

float FeatureExtractor::calculateSingleEar(const std::array<Point3D, 6>& p) {
    // MediaPipe 6-point EAR Formula:
    // EAR = (|p2 - p6| + |p3 - p5|) / (2.0 * |p1 - p4|)
    float vertical1 = distance3D(p[1], p[5]); // p2 - p6
    float vertical2 = distance3D(p[2], p[4]); // p3 - p5
    float horizontal = distance3D(p[0], p[3]); // p1 - p4

    if (horizontal < 1e-6f) {
        return 0.0f;
    }
    return (vertical1 + vertical2) / (2.0f * horizontal);
}

EyeMetrics FeatureExtractor::processFrame(const std::vector<Point3D>& landmarks, float fps) {
    if (landmarks.size() < 468) {
        return {};
    }

    std::array<Point3D, 6> leftPoints{};
    for (size_t i = 0; i < 6; ++i) {
        leftPoints[i] = landmarks[EyeLandmarkIndices::LEFT_EYE[i]];
    }

    std::array<Point3D, 6> rightPoints{};
    for (size_t i = 0; i < 6; ++i) {
        rightPoints[i] = landmarks[EyeLandmarkIndices::RIGHT_EYE[i]];
    }

    float earL = calculateSingleEar(leftPoints);
    float earR = calculateSingleEar(rightPoints);

    return processEar(earL, earR, fps);
}

EyeMetrics FeatureExtractor::processEar(float earLeft, float earRight, float fps) {
    float earAvg = (earLeft + earRight) * 0.5f;
    bool isClosed = (earAvg < m_threshold);

    // 更新歷史序列
    m_earHistory.push_back(earAvg);
    if (m_earHistory.size() > m_maxHistorySize) {
        m_earHistory.pop_front();
    }

    m_closedHistory.push_back(isClosed);
    if (m_closedHistory.size() > m_maxHistorySize) {
        m_closedHistory.pop_front();
    }

    // 眨眼檢測狀態機
    float frameDurationMs = (fps > 0.0f) ? (1000.0f / fps) : 33.33f;
    if (isClosed) {
        m_currentClosedFrames++;
    } else {
        if (m_wasClosed) {
            // 一次眨眼完成 (需超過 1 幀以避免雜訊，通常 2~15 幀)
            if (m_currentClosedFrames >= 2 && m_currentClosedFrames <= 25) {
                m_totalBlinks++;
                m_totalBlinkDurationMs += (m_currentClosedFrames * frameDurationMs);
            }
            m_currentClosedFrames = 0;
        }
    }
    m_wasClosed = isClosed;

    // 計算 PERCLOS (閉眼幀數佔窗口比例)
    float perclos = 0.0f;
    if (!m_closedHistory.empty()) {
        int closedCount = 0;
        for (bool c : m_closedHistory) {
            if (c) closedCount++;
        }
        perclos = static_cast<float>(closedCount) / static_cast<float>(m_closedHistory.size());
    }

    // 計算估計眨眼率 (次/分鐘)
    float windowDurationSec = (m_closedHistory.size() > 0 && fps > 0.0f)
                                  ? (static_cast<float>(m_closedHistory.size()) / fps)
                                  : 1.0f;
    float blinkRatePerMin = (windowDurationSec > 0.0f)
                                ? ((static_cast<float>(m_totalBlinks) / windowDurationSec) * 60.0f)
                                : 0.0f;

    float avgDurationMs = (m_totalBlinks > 0)
                              ? (m_totalBlinkDurationMs / static_cast<float>(m_totalBlinks))
                              : 0.0f;

    EyeMetrics metrics;
    metrics.earLeft = earLeft;
    metrics.earRight = earRight;
    metrics.earAvg = earAvg;
    metrics.perclos = perclos;
    metrics.blinkCount = m_totalBlinks;
    metrics.blinkRatePerMin = blinkRatePerMin;
    metrics.avgBlinkDurationMs = avgDurationMs;
    metrics.isEyeClosed = isClosed;

    return metrics;
}

void FeatureExtractor::setEyeClosedThreshold(float threshold) {
    m_threshold = threshold;
}

float FeatureExtractor::getEyeClosedThreshold() const {
    return m_threshold;
}

std::vector<float> FeatureExtractor::getEarHistory() const {
    return std::vector<float>(m_earHistory.begin(), m_earHistory.end());
}

void FeatureExtractor::reset() {
    m_earHistory.clear();
    m_closedHistory.clear();
    m_wasClosed = false;
    m_currentClosedFrames = 0;
    m_totalBlinks = 0;
    m_totalBlinkDurationMs = 0.0f;
}

} // namespace efd

