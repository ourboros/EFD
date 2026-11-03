#include "analysis/RollingOpenBaseline.hpp"
#include <algorithm>
#include <vector>

namespace efd {

RollingOpenBaseline::RollingOpenBaseline(float initialOpenEar)
    : m_initialOpenEar(initialOpenEar), m_openEar(initialOpenEar) {
}

void RollingOpenBaseline::setInitialOpenEar(float ear) {
    if (ear <= kClosedEarFloor) return;
    m_initialOpenEar = ear;
    if (m_totalSeconds < kMinSeconds) m_openEar = ear;
}

void RollingOpenBaseline::add(float ear, float dtSeconds) {
    if (ear <= 0.0f || dtSeconds <= 0.0f) return;
    // 已達閉眼門檻的幀不納入睜眼基準：A71 實測長時間閉眼 (超過窗口 20%) 會把基準拉低，
    // 門檻從 0.093 降到 0.054，閉眼的幀反而不再被判為閉眼
    if (ear < perclosThreshold()) return;
    m_samples.push_back({ear, dtSeconds});
    m_totalSeconds += dtSeconds;
    while (m_samples.size() > 1 && m_totalSeconds - m_samples.front().dt >= kWindowSeconds) {
        m_totalSeconds -= m_samples.front().dt;
        m_samples.pop_front();
    }
    // 每 0.5 秒重算一次中位數即可 (窗口最多約 1,800 幀)
    m_sinceRecompute += dtSeconds;
    if (m_sinceRecompute >= 0.5f) {
        m_sinceRecompute = 0.0f;
        recompute();
    }
}

void RollingOpenBaseline::recompute() {
    if (m_totalSeconds < kMinSeconds || m_samples.size() < 10) {
        m_openEar = m_initialOpenEar;
        return;
    }
    std::vector<float> ears;
    ears.reserve(m_samples.size());
    for (const Sample& s : m_samples) ears.push_back(s.ear);
    std::sort(ears.begin(), ears.end());
    // 排除最低 20% (眨眼、閉眼) 後取中位數作為睜眼 EAR
    const size_t start = static_cast<size_t>(ears.size() * kExcludeLowestFraction);
    const size_t mid = start + (ears.size() - start) / 2;
    m_openEar = std::max(ears[mid], kClosedEarFloor + 0.02f);
}

void RollingOpenBaseline::reset() {
    m_samples.clear();
    m_totalSeconds = 0.0f;
    m_sinceRecompute = 0.0f;
    m_openEar = m_initialOpenEar;
}

float RollingOpenBaseline::perclosThreshold() const {
    return kClosedEarFloor + (1.0f - kPerclosClosure) * (m_openEar - kClosedEarFloor);
}

float RollingOpenBaseline::blinkThreshold() const {
    return kClosedEarFloor + (1.0f - kBlinkClosure) * (m_openEar - kClosedEarFloor);
}

} // namespace efd
