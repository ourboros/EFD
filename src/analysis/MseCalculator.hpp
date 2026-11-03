#pragma once

#include "efd/types.hpp"
#include <vector>
#include <cstddef>

namespace efd {

class MseCalculator {
public:
    explicit MseCalculator(int maxScale = 5, int embeddingDim = 2, float toleranceRatio = 0.15f);

    // 計算單一尺度樣本熵 (Sample Entropy, SampEn)
    static float calculateSampleEntropy(const std::vector<float>& signal, int m = 2, float r = 0.15f);

    // 計算多尺度熵 (Multiscale Entropy, MSE) 並返回各尺度數值與綜合複雜度指標 CI
    ComplexityMetrics calculateComplexity(const std::vector<float>& signal) const;

    // 以固定容許誤差 r 計算 (不隨窗口標準差縮放)：長時間閉眼時 EAR 幾乎是平線，
    // 若 r = 0.15 x 窗口標準差，r 會縮到雜訊大小，雜訊反而被算成高複雜度
    ComplexityMetrics calculateComplexity(const std::vector<float>& signal, float absoluteTolerance) const;

    // 粗粒化序列 (Coarse-Graining Procedure)
    static std::vector<float> coarseGrain(const std::vector<float>& signal, int scale);

    // 計算信號標準差
    static float calculateStdDev(const std::vector<float>& signal, float mean);

private:
    int   m_maxScale;
    int   m_embeddingDim;
    float m_toleranceRatio;
};

} // namespace efd

