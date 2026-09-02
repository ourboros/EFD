#include "MseCalculator.hpp"
#include <cmath>
#include <numeric>
#include <algorithm>

namespace efd {

MseCalculator::MseCalculator(int maxScale, int embeddingDim, float toleranceRatio)
    : m_maxScale(maxScale),
      m_embeddingDim(embeddingDim),
      m_toleranceRatio(toleranceRatio) {
}

float MseCalculator::calculateStdDev(const std::vector<float>& signal, float mean) {
    if (signal.size() < 2) return 0.0f;
    float sumSq = 0.0f;
    for (float val : signal) {
        float diff = val - mean;
        sumSq += diff * diff;
    }
    return std::sqrt(sumSq / static_cast<float>(signal.size() - 1));
}

std::vector<float> MseCalculator::coarseGrain(const std::vector<float>& signal, int scale) {
    if (scale <= 1) return signal;
    size_t n = signal.size();
    size_t coarseLen = n / scale;
    std::vector<float> coarse(coarseLen, 0.0f);

    for (size_t j = 0; j < coarseLen; ++j) {
        float sum = 0.0f;
        for (int k = 0; k < scale; ++k) {
            sum += signal[j * scale + k];
        }
        coarse[j] = sum / static_cast<float>(scale);
    }
    return coarse;
}

float MseCalculator::calculateSampleEntropy(const std::vector<float>& signal, int m, float r) {
    const size_t N = signal.size();
    if (N <= static_cast<size_t>(m + 1)) {
        return 0.0f;
    }

    // 計算模板長度為 m 的匹配次數 B，以及長度為 m+1 的匹配次數 A
    auto countMatches = [&](int dim) -> long long {
        long long matches = 0;
        size_t numVectors = N - dim;

        for (size_t i = 0; i < numVectors; ++i) {
            for (size_t j = i + 1; j < numVectors; ++j) {
                float maxDist = 0.0f;
                for (int k = 0; k < dim; ++k) {
                    float dist = std::fabs(signal[i + k] - signal[j + k]);
                    if (dist > maxDist) {
                        maxDist = dist;
                    }
                    if (maxDist > r) break;
                }
                if (maxDist <= r) {
                    matches++;
                }
            }
        }
        return matches;
    };

    long long B = countMatches(m);
    long long A = countMatches(m + 1);

    if (B == 0 || A == 0) {
        // 沒有足夠匹配時返回 0
        return 0.0f;
    }

    return -std::log(static_cast<float>(A) / static_cast<float>(B));
}

ComplexityMetrics MseCalculator::calculateComplexity(const std::vector<float>& signal) const {
    ComplexityMetrics metrics;
    const size_t n = signal.size();
    if (n < 20) {
        return metrics;
    }

    float mean = std::accumulate(signal.begin(), signal.end(), 0.0f) / static_cast<float>(n);
    float stdDev = calculateStdDev(signal, mean);
    float r = m_toleranceRatio * stdDev;

    if (r < 1e-5f) {
        r = 1e-3f; // 防止標準差為 0
    }

    metrics.sampleEntropyScales.reserve(m_maxScale);
    float ciSum = 0.0f;

    for (int scale = 1; scale <= m_maxScale; ++scale) {
        std::vector<float> coarseSignal = coarseGrain(signal, scale);
        float sampEn = calculateSampleEntropy(coarseSignal, m_embeddingDim, r);
        metrics.sampleEntropyScales.push_back(sampEn);
        ciSum += sampEn;
    }

    metrics.complexityIndex = ciSum;
    return metrics;
}

} // namespace efd

