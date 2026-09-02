#include <iostream>
#include <vector>
#include <cmath>
#include <cassert>

#include "efd/types.hpp"
#include "analysis/FeatureExtractor.hpp"
#include "analysis/EmdCalculator.hpp"
#include "analysis/MseCalculator.hpp"
#include "analysis/AdaptiveBaseline.hpp"
#include "state/FatigueStateMachine.hpp"

void testEarCalculation() {
    std::cout << "[TEST] 1. 測試 EAR 幾何特徵計算...";
    // 定義一個水平寬度為 20、垂直高度為 6 的眼睛特徵點 (EAR 約 6 / 20 = 0.30)
    std::array<efd::Point3D, 6> eyePoints = {{
        {-10.0f,  0.0f, 0.0f}, // p1 (outer)
        { -5.0f,  3.0f, 0.0f}, // p2 (top-outer)
        {  5.0f,  3.0f, 0.0f}, // p3 (top-inner)
        { 10.0f,  0.0f, 0.0f}, // p4 (inner)
        {  5.0f, -3.0f, 0.0f}, // p5 (bottom-inner)
        { -5.0f, -3.0f, 0.0f}  // p6 (bottom-outer)
    }};

    float ear = efd::FeatureExtractor::calculateSingleEar(eyePoints);
    assert(std::fabs(ear - 0.30f) < 0.01f);
    std::cout << " [PASS] (EAR = " << ear << ")\n";
}

void testAdaptiveBaseline() {
    std::cout << "[TEST] 2. 測試動態滑動窗口基準自適應...";
    efd::AdaptiveBaseline baseline(0.30f, 0.7f, 1.5f, 100);
    
    // 模擬漸進式環境光線變暗，平均 EAR 緩步漂移至 0.28
    for (int i = 0; i < 50; ++i) {
        baseline.update(0.28f);
    }
    float th = baseline.getCurrentThreshold();
    assert(th >= 0.12f && th <= 0.30f);
    std::cout << " [PASS] (動態閾值 = " << th << ")\n";
}

void testMseComplexity() {
    std::cout << "[TEST] 3. 測試 MSE 多尺度熵與 CI 複雜度計算...";
    efd::MseCalculator mse(3, 2, 0.15f);

    // 建立 100 筆正弦合成序列
    std::vector<float> signal(100);
    for (size_t i = 0; i < 100; ++i) {
        signal[i] = 0.30f + 0.03f * std::sin(i * 0.2f);
    }

    efd::ComplexityMetrics metrics = mse.calculateComplexity(signal);
    assert(metrics.sampleEntropyScales.size() == 3);
    assert(metrics.complexityIndex >= 0.0f);
    std::cout << " [PASS] (CI = " << metrics.complexityIndex << ")\n";
}

void testEmdDecomposition() {
    std::cout << "[TEST] 4. 測試 EMD (經驗模態分解)...";
    efd::EmdCalculator emd(3, 10, 0.05f);

    // 建立合成混合波 (低頻 + 高頻)
    std::vector<float> signal(120);
    for (size_t i = 0; i < 120; ++i) {
        signal[i] = std::sin(i * 0.05f) + 0.5f * std::sin(i * 0.4f);
    }

    efd::EmdResult result = emd.decompose(signal);
    assert(!result.imfs.empty());
    std::cout << " [PASS] (成功分解出 " << result.imfs.size() << " 個 IMF)\n";
}

void testFatigueStateMachine() {
    std::cout << "[TEST] 5. 測試 20/5/5 防打擾狀態機轉移...";
    efd::FatigueStateMachine fsm(1200, 300, 300);

    // 1. 正常狀態
    efd::SystemState s1 = fsm.update(true, 0.05f, 16.0f, 4.5f, 1.0f);
    assert(s1.fatigueLevel == efd::FatigueLevel::Relaxed);
    assert(s1.cooldownState == efd::CooldownState::NormalTracking);

    // 2. 觸發疲勞
    efd::SystemState s2 = fsm.update(true, 0.35f, 4.0f, 1.8f, 1.0f);
    assert(s2.fatigueLevel == efd::FatigueLevel::Attention);
    assert(s2.cooldownState == efd::CooldownState::InCooldown);

    // 3. 離座 5 分鐘重置
    efd::SystemState s3 = fsm.update(false, 0.0f, 0.0f, 0.0f, 305.0f);
    assert(s3.fatigueLevel == efd::FatigueLevel::UserAway);
    assert(s3.cooldownState == efd::CooldownState::AwayPaused);

    std::cout << " [PASS]\n";
}

int main() {
    std::cout << "========================================\n";
    std::cout << "  EFD C++ 核心演算法單元測試\n";
    std::cout << "========================================\n";

    testEarCalculation();
    testAdaptiveBaseline();
    testMseComplexity();
    testEmdDecomposition();
    testFatigueStateMachine();

    std::cout << "\n[ALL TESTS PASSED] 所有單元測試皆順利通過！\n";
    return 0;
}

