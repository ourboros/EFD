#include "analysis/GazeTracker.hpp"
#include <algorithm>
#include <cmath>

namespace efd {

namespace {
// MediaPipe 索引：右眼 (受試者) 外眼角 33、內眼角 133、虹膜中心 468；左眼內眼角 362、外眼角 263、虹膜中心 473
// 兩眼的眼角向量都由受試者右側指向左側，方向一致，可直接平均
constexpr int kEyeA[2] = {33, 362};
constexpr int kEyeB[2] = {133, 263};
constexpr int kIris[2] = {468, 473};
constexpr size_t kRequiredPoints = 478;

bool eyeGaze(const std::vector<Point3D>& lm, int a, int b, int iris, float& gx, float& gy) {
    const float ux = lm[b].x - lm[a].x;
    const float uy = lm[b].y - lm[a].y;
    const float w = std::hypot(ux, uy);
    if (w < 4.0f) return false;   // 眼寬不到 4 像素：臉太小或點位異常
    const float mx = 0.5f * (lm[a].x + lm[b].x);
    const float my = 0.5f * (lm[a].y + lm[b].y);
    const float dx = lm[iris].x - mx;
    const float dy = lm[iris].y - my;
    // 投影到眼角連線 (水平) 與其垂直方向，以眼寬為單位
    gx = (dx * ux + dy * uy) / (w * w);
    gy = (-dx * uy + dy * ux) / (w * w);
    return std::isfinite(gx) && std::isfinite(gy);
}

float median3(float a, float b, float c) {
    return std::max(std::min(a, b), std::min(std::max(a, b), c));
}
} // namespace

bool GazeTracker::computeGaze(const std::vector<Point3D>& landmarks, float& gx, float& gy) {
    if (landmarks.size() < kRequiredPoints) return false;
    float sx = 0.0f, sy = 0.0f;
    for (int e = 0; e < 2; ++e) {
        float x = 0.0f, y = 0.0f;
        if (!eyeGaze(landmarks, kEyeA[e], kEyeB[e], kIris[e], x, y)) return false;
        sx += x;
        sy += y;
    }
    gx = 0.5f * sx;
    gy = 0.5f * sy;
    return true;
}

void GazeTracker::addFrame(const std::vector<Point3D>& landmarks, bool eyeOpen, float dtSeconds) {
    float gx = 0.0f, gy = 0.0f;
    const bool valid = eyeOpen && computeGaze(landmarks, gx, gy);
    addSample(valid, gx, gy, dtSeconds);
}

void GazeTracker::addSample(bool valid, float gx, float gy, float dtSeconds) {
    if (dtSeconds <= 0.0f) return;
    m_samples.push_back({valid, gx, gy, dtSeconds});
    m_totalSeconds += dtSeconds;
    while (m_samples.size() > 1 && m_totalSeconds - m_samples.front().dt >= kWindowSeconds) {
        m_totalSeconds -= m_samples.front().dt;
        m_samples.pop_front();
    }
}

GazeTracker::Metrics GazeTracker::compute() const {
    Metrics m;
    if (m_samples.empty()) return m;

    // 1. 依時間重新取樣為 10 Hz (桶內有效樣本取平均；沒有有效樣本的桶標記為無效)，消除各平台幀率差異
    struct Bucket { bool valid; float x; float y; };
    std::vector<Bucket> buckets;
    const float bucketSec = 1.0f / kResampleHz;
    float t = 0.0f, bucketEnd = bucketSec;
    float sx = 0.0f, sy = 0.0f, validSec = 0.0f;
    int n = 0;
    for (const Sample& s : m_samples) {
        t += s.dt;
        if (s.valid) {
            sx += s.gx;
            sy += s.gy;
            n++;
            validSec += s.dt;
        }
        while (t >= bucketEnd) {
            if (n > 0) {
                buckets.push_back({true, sx / n, sy / n});
            } else {
                buckets.push_back({false, 0.0f, 0.0f});
            }
            sx = sy = 0.0f;
            n = 0;
            bucketEnd += bucketSec;
        }
    }
    m.validSeconds = validSec;
    if (validSec < kMinValidSeconds || buckets.size() < 3) return m;

    // 2. 3 點中位數濾波 (只在前後都有效時) 去除單幀特徵點跳動
    std::vector<Bucket> f = buckets;
    for (size_t i = 1; i + 1 < buckets.size(); ++i) {
        if (buckets[i - 1].valid && buckets[i].valid && buckets[i + 1].valid) {
            f[i].x = median3(buckets[i - 1].x, buckets[i].x, buckets[i + 1].x);
            f[i].y = median3(buckets[i - 1].y, buckets[i].y, buckets[i + 1].y);
        }
    }

    // 3. 相鄰有效桶的位移：扣除抖動後加總為移動量；超過門檻記為視線轉移 (連續超過只算一次)
    float path = 0.0f;
    int pairs = 0;
    int shifts = 0;
    bool inShift = false;
    for (size_t i = 1; i < f.size(); ++i) {
        if (!f[i - 1].valid || !f[i].valid) {
            inShift = false;
            continue;
        }
        const float d = std::hypot(f[i].x - f[i - 1].x, f[i].y - f[i - 1].y);
        path += std::max(0.0f, d - kJitter);
        pairs++;
        if (d > kShiftThreshold) {
            if (!inShift) shifts++;
            inShift = true;
        } else {
            inShift = false;
        }
    }
    if (pairs == 0) return m;
    const float pairSeconds = static_cast<float>(pairs) / kResampleHz;
    m.speed = path / pairSeconds;
    m.shiftsPerMin = static_cast<float>(shifts) / pairSeconds * 60.0f;
    m.ready = true;
    return m;
}

void GazeTracker::reset() {
    m_samples.clear();
    m_totalSeconds = 0.0f;
}

} // namespace efd
