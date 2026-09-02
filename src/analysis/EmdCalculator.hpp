#pragma once

#include <vector>
#include <cstddef>

namespace efd {

struct EmdResult {
    std::vector<std::vector<float>> imfs; // Extracted Intrinsic Mode Functions
    std::vector<float> residue;          // Monotonic residue trend
    float highToLowEnergyRatio = 0.0f;    // Energy ratio (IMF1+IMF2) / (Total Energy)
};

class EmdCalculator {
public:
    explicit EmdCalculator(size_t maxImfs = 5, size_t maxSiftingIters = 20, float siftingStopThreshold = 0.05f);

    // 執行經驗模態分解 (EMD)
    EmdResult decompose(const std::vector<float>& signal) const;

    // 計算特定 IMF 的能量
    static float calculateEnergy(const std::vector<float>& signal);

private:
    size_t m_maxImfs;
    size_t m_maxSiftingIters;
    float  m_siftingStopThreshold;

    // 尋找局部極大與極小值索引
    void findExtrema(const std::vector<float>& signal,
                     std::vector<size_t>& maxIndices,
                     std::vector<size_t>& minIndices) const;

    // 三次樣條 / 線性插值生成包絡線 (Envelope)
    std::vector<float> interpolateEnvelope(const std::vector<float>& signal,
                                           const std::vector<size_t>& extremaIndices) const;
};

} // namespace efd

