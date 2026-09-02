#include "EmdCalculator.hpp"
#include <cmath>
#include <numeric>
#include <algorithm>

namespace efd {

EmdCalculator::EmdCalculator(size_t maxImfs, size_t maxSiftingIters, float siftingStopThreshold)
    : m_maxImfs(maxImfs),
      m_maxSiftingIters(maxSiftingIters),
      m_siftingStopThreshold(siftingStopThreshold) {
}

float EmdCalculator::calculateEnergy(const std::vector<float>& signal) {
    float sumSq = 0.0f;
    for (float val : signal) {
        sumSq += (val * val);
    }
    return sumSq;
}

void EmdCalculator::findExtrema(const std::vector<float>& signal,
                               std::vector<size_t>& maxIndices,
                               std::vector<size_t>& minIndices) const {
    maxIndices.clear();
    minIndices.clear();
    const size_t n = signal.size();
    if (n < 3) return;

    for (size_t i = 1; i < n - 1; ++i) {
        if (signal[i] > signal[i - 1] && signal[i] >= signal[i + 1]) {
            maxIndices.push_back(i);
        } else if (signal[i] < signal[i - 1] && signal[i] <= signal[i + 1]) {
            minIndices.push_back(i);
        }
    }
}

// 自然三次樣條或分段線性插值構造包絡線
std::vector<float> EmdCalculator::interpolateEnvelope(const std::vector<float>& signal,
                                                     const std::vector<size_t>& extremaIndices) const {
    const size_t n = signal.size();
    std::vector<float> envelope(n, 0.0f);
    if (extremaIndices.empty()) {
        return envelope;
    }

    // 擴展端點以防止邊界效應 (Mirroring / Boundary extension)
    std::vector<size_t> x = extremaIndices;
    std::vector<float> y;
    y.reserve(x.size() + 2);

    if (x.front() != 0) {
        x.insert(x.begin(), 0);
    }
    if (x.back() != n - 1) {
        x.push_back(n - 1);
    }

    for (size_t idx : x) {
        y.push_back(signal[idx]);
    }

    // 分段線性插值 (保證數值穩定性)
    for (size_t k = 0; k < x.size() - 1; ++k) {
        size_t x0 = x[k];
        size_t x1 = x[k + 1];
        float y0 = y[k];
        float y1 = y[k + 1];
        float dx = static_cast<float>(x1 - x0);

        if (dx <= 0.0f) continue;

        for (size_t i = x0; i <= x1; ++i) {
            float t = static_cast<float>(i - x0) / dx;
            envelope[i] = y0 + t * (y1 - y0);
        }
    }

    return envelope;
}

EmdResult EmdCalculator::decompose(const std::vector<float>& signal) const {
    EmdResult result;
    const size_t n = signal.size();
    if (n < 4) {
        result.residue = signal;
        return result;
    }

    std::vector<float> currentResidue = signal;
    float totalSignalEnergy = calculateEnergy(signal);

    for (size_t imfIdx = 0; imfIdx < m_maxImfs; ++imfIdx) {
        std::vector<float> h = currentResidue;
        bool isImf = false;

        for (size_t iter = 0; iter < m_maxSiftingIters; ++iter) {
            std::vector<size_t> maxIdx, minIdx;
            findExtrema(h, maxIdx, minIdx);

            if (maxIdx.size() < 2 || minIdx.size() < 2) {
                // 極值點不足，無法再篩選
                break;
            }

            std::vector<float> upperEnv = interpolateEnvelope(h, maxIdx);
            std::vector<float> lowerEnv = interpolateEnvelope(h, minIdx);

            std::vector<float> meanEnv(n, 0.0f);
            float siftingDiff = 0.0f;
            float hEnergy = 0.0f;

            for (size_t i = 0; i < n; ++i) {
                meanEnv[i] = 0.5f * (upperEnv[i] + lowerEnv[i]);
                float newH = h[i] - meanEnv[i];
                siftingDiff += (meanEnv[i] * meanEnv[i]);
                hEnergy += (h[i] * h[i]);
                h[i] = newH;
            }

            // 檢查停機條件 (Standard Deviation criteria)
            if (hEnergy > 1e-8f && (siftingDiff / hEnergy) < m_siftingStopThreshold) {
                isImf = true;
                break;
            }
        }

        // 檢查提取出的 h 是否具備有效能量
        float imfEnergy = calculateEnergy(h);
        if (imfEnergy < 1e-6f) {
            break;
        }

        result.imfs.push_back(h);

        // 更新剩餘信號 (Residue)
        for (size_t i = 0; i < n; ++i) {
            currentResidue[i] -= h[i];
        }

        // 檢查剩餘信號是否已為單調
        std::vector<size_t> resMax, resMin;
        findExtrema(currentResidue, resMax, resMin);
        if (resMax.size() + resMin.size() <= 2) {
            break;
        }
    }

    result.residue = currentResidue;

    // 計算高頻能量比 (IMF1 + IMF2 能量佔總能量比例)
    float highFreqEnergy = 0.0f;
    for (size_t i = 0; i < std::min<size_t>(2, result.imfs.size()); ++i) {
        highFreqEnergy += calculateEnergy(result.imfs[i]);
    }

    if (totalSignalEnergy > 1e-6f) {
        result.highToLowEnergyRatio = highFreqEnergy / totalSignalEnergy;
    } else {
        result.highToLowEnergyRatio = 0.0f;
    }

    return result;
}

} // namespace efd

