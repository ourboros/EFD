#include "FeatureExtractor.hpp"
#include <cmath>
#include <numeric>
#include <algorithm>

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
    const float sampleSec = (fps > 0.0f) ? (1.0f / fps) : (1.0f / 30.0f);
    m_elapsedSec += sampleSec;

    // EAR 序列 (供 EMD / MSE)
    m_earHistory.push_back(earAvg);
    if (m_earHistory.size() > m_maxHistorySize) {
        m_earHistory.pop_front();
    }

    // 60 秒時間窗口
    m_samples.push_back({isClosed, sampleSec});
    m_windowSec += sampleSec;
    if (isClosed) m_closedSec += sampleSec;
    while (!m_samples.empty() && m_windowSec - m_samples.front().durationSec >= kPerclosWindowSec) {
        m_windowSec -= m_samples.front().durationSec;
        if (m_samples.front().closed) m_closedSec -= m_samples.front().durationSec;
        m_samples.pop_front();
    }

    // 眨眼以持續時間判斷：60 ms ~ 800 ms 為有效眨眼；更長的閉眼只計入 PERCLOS
    if (isClosed) {
        m_currentClosedSec += sampleSec;
    } else if (m_wasClosed) {
        if (m_currentClosedSec >= kMinBlinkSec - 1e-4f && m_currentClosedSec <= kMaxBlinkSec + 1e-4f) {
            m_totalBlinks++;
            m_totalBlinkDurationMs += m_currentClosedSec * 1000.0f;
            m_blinkTimes.push_back(m_elapsedSec);
        }
        m_currentClosedSec = 0.0f;
    }
    m_wasClosed = isClosed;

    while (!m_blinkTimes.empty() && m_elapsedSec - m_blinkTimes.front() > kPerclosWindowSec) {
        m_blinkTimes.pop_front();
    }

    float perclos = (m_windowSec > 0.0f) ? static_cast<float>(std::clamp(m_closedSec / m_windowSec, 0.0, 1.0)) : 0.0f;

    float blinkRatePerMin = 0.0f;
    if (m_windowSec >= kBlinkRateMinWindowSec) {
        blinkRatePerMin = static_cast<float>(std::clamp(static_cast<double>(m_blinkTimes.size()) / m_windowSec * 60.0, 0.0, 60.0));
    }

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
    m_samples.clear();
    m_blinkTimes.clear();
    m_windowSec = 0.0;
    m_closedSec = 0.0;
    m_elapsedSec = 0.0;
    m_wasClosed = false;
    m_currentClosedSec = 0.0f;
    m_totalBlinks = 0;
    m_totalBlinkDurationMs = 0.0f;
}

void FeatureExtractor::resetTemporaryCounters() {
    m_wasClosed = false;
    m_currentClosedSec = 0.0f;
}

} // namespace efd

