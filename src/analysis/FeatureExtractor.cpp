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

    inline bool isBlinkDuration(float ms) {
        return ms >= FeatureExtractor::kMinBlinkMs && ms <= FeatureExtractor::kMaxBlinkMs;
    }
}

FeatureExtractor::FeatureExtractor(float windowSeconds, float defaultThreshold)
    : m_threshold(defaultThreshold), m_windowMs(std::max(1.0f, windowSeconds) * 1000.0f) {
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
    float sampleMs = (fps > 0.0f) ? (1000.0f / fps) : 33.33f;

    // 更新時間窗口
    m_samples.push_back({earAvg, isClosed, sampleMs});
    m_windowTotalMs += sampleMs;
    if (isClosed) m_windowClosedMs += sampleMs;
    while (m_samples.size() > 1 && m_windowTotalMs - m_samples.front().durationMs >= m_windowMs) {
        const Sample& old = m_samples.front();
        m_windowTotalMs -= old.durationMs;
        if (old.closed) m_windowClosedMs -= old.durationMs;
        m_samples.pop_front();
    }

    // 眨眼檢測狀態機 (以閉眼持續時間判斷)
    if (isClosed) {
        m_currentClosedMs += sampleMs;
    } else if (m_wasClosed) {
        if (isBlinkDuration(m_currentClosedMs)) {
            m_totalBlinks++;
            m_totalBlinkDurationMs += m_currentClosedMs;
        }
        m_currentClosedMs = 0.0f;
    }
    m_wasClosed = isClosed;

    float perclos = (m_windowTotalMs > 0.0f)
        ? std::clamp(m_windowClosedMs / m_windowTotalMs, 0.0f, 1.0f) : 0.0f;

    // 窗口內完整眨眼段落計數
    int windowBlinkCount = 0;
    float segMs = 0.0f;
    for (const Sample& s : m_samples) {
        if (s.closed) {
            segMs += s.durationMs;
        } else {
            if (isBlinkDuration(segMs)) {
                windowBlinkCount++;
            }
            segMs = 0.0f;
        }
    }
    float windowSec = m_windowTotalMs / 1000.0f;
    float rawRate = (windowSec >= kMinBlinkRateWindowSec)
        ? (static_cast<float>(windowBlinkCount) / windowSec) * 60.0f
        : 0.0f;

    EyeMetrics metrics;
    metrics.earLeft = earLeft;
    metrics.earRight = earRight;
    metrics.earAvg = earAvg;
    metrics.perclos = perclos;
    metrics.blinkCount = m_totalBlinks;
    metrics.blinkRatePerMin = std::clamp(rawRate, 0.0f, 60.0f);
    metrics.avgBlinkDurationMs = (m_totalBlinks > 0)
        ? (m_totalBlinkDurationMs / static_cast<float>(m_totalBlinks)) : 0.0f;
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
    std::vector<float> out;
    out.reserve(m_samples.size());
    for (const Sample& s : m_samples) out.push_back(s.ear);
    return out;
}

std::vector<float> FeatureExtractor::getEarHistoryResampled(float targetHz) const {
    std::vector<float> out;
    if (m_samples.empty() || targetHz <= 0.0f) return out;

    // 以固定時間桶 (1/targetHz) 取平均，桶內無樣本時沿用前一值
    const float bucketMs = 1000.0f / targetHz;
    float bucketEnd = bucketMs;
    float t = 0.0f;
    float sum = 0.0f;
    int n = 0;
    float last = m_samples.front().ear;
    for (const Sample& s : m_samples) {
        t += s.durationMs;
        sum += s.ear;
        n++;
        while (t >= bucketEnd) {
            if (n > 0) {
                last = sum / static_cast<float>(n);
                sum = 0.0f;
                n = 0;
            }
            out.push_back(last);
            bucketEnd += bucketMs;
        }
    }
    return out;
}

float FeatureExtractor::getWindowDurationSeconds() const {
    return m_windowTotalMs / 1000.0f;
}

void FeatureExtractor::reset() {
    m_samples.clear();
    m_windowTotalMs = 0.0f;
    m_windowClosedMs = 0.0f;
    m_wasClosed = false;
    m_currentClosedMs = 0.0f;
    m_totalBlinks = 0;
    m_totalBlinkDurationMs = 0.0f;
}

void FeatureExtractor::resetTemporaryCounters() {
    m_wasClosed = false;
    m_currentClosedMs = 0.0f;
}

} // namespace efd
