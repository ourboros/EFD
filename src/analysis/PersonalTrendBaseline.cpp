#include "analysis/PersonalTrendBaseline.hpp"
#include <algorithm>
#include <cmath>
#include <vector>

namespace efd {

PersonalTrendBaseline::PersonalTrendBaseline(size_t maxMinutes, size_t minMinutes, float percentile)
    : m_maxMinutes(std::max<size_t>(1, maxMinutes)),
      m_minMinutes(std::max<size_t>(1, minMinutes)),
      m_percentile(std::clamp(percentile, 0.0f, 1.0f)) {
}

void PersonalTrendBaseline::addMinute(float value) {
    if (!std::isfinite(value) || value <= 0.0f) return;
    m_values.push_back(value);
    while (m_values.size() > m_maxMinutes) m_values.pop_front();
    std::vector<float> sorted(m_values.begin(), m_values.end());
    std::sort(sorted.begin(), sorted.end());
    const size_t idx = static_cast<size_t>(m_percentile * static_cast<float>(sorted.size() - 1) + 0.5f);
    m_value = sorted[std::min(idx, sorted.size() - 1)];
}

void PersonalTrendBaseline::reset() {
    m_values.clear();
    m_value = 0.0f;
}

} // namespace efd
