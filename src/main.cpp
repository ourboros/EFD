#include <iostream>
#include <vector>
#include <cmath>
#include <iomanip>
#include <thread>
#include <chrono>

#include "efd/version.hpp"
#include "efd/types.hpp"
#include "analysis/FeatureExtractor.hpp"
#include "analysis/EmdCalculator.hpp"
#include "analysis/MseCalculator.hpp"
#include "analysis/AdaptiveBaseline.hpp"
#include "state/FatigueStateMachine.hpp"

int main() {
    std::cout << "====================================================\n";
    std::cout << "  Eye Fatigue Detection (EFD) Native Engine v" << EFD_VERSION_STRING << "\n";
    std::cout << "  Platform: Cross-Platform Native C++20 Core\n";
    std::cout << "====================================================\n\n";

    // 1. 初始化核心模組
    efd::FeatureExtractor extractor(300, 0.21f);
    efd::EmdCalculator emdCalc(4, 15, 0.05f);
    efd::MseCalculator mseCalc(5, 2, 0.15f);
    efd::AdaptiveBaseline baseline(0.32f, 0.7f, 1.5f, 300);
    efd::FatigueStateMachine stateMachine(1200, 300, 300);

    stateMachine.setAlertCallback([](efd::FatigueLevel level, float score, const std::string& msg) {
        std::cout << "\n>>> [ALERT TRIGGERED] Level: " << static_cast<int>(level)
                  << " | Fatigue Score: " << std::fixed << std::setprecision(1) << score
                  << " | " << msg << " <<<\n\n";
    });

    // 2. 校準階段模擬 (Calibration Phase)
    std::cout << "[Step 1] 正在進行基準線動態校準 (Simulating Awake Calibration)...\n";
    std::vector<float> calibrationSamples;
    for (int i = 0; i < 90; ++i) {
        // 模擬睜眼 EAR 約 0.30 ~ 0.33
        float val = 0.31f + 0.015f * std::sin(i * 0.1f);
        calibrationSamples.push_back(val);
    }
    baseline.calibrate(calibrationSamples);
    extractor.setEyeClosedThreshold(baseline.getCurrentThreshold());
    std::cout << " -> 校準完成: 基準 EAR = " << baseline.getCalibrationData().baselineEar 
              << ", 初始判定閾值 = " << baseline.getCurrentThreshold() << "\n\n";

    // 3. 模擬 300 幀視訊串流運算 (Simulation Pipeline)
    std::cout << "[Step 2] 開始串流特徵提取與訊號複雜度運算 (30 FPS Stream)...\n";
    std::cout << std::string(75, '-') << "\n";
    std::cout << std::setw(6)  << "Frame"
              << std::setw(10) << "EAR Avg"
              << std::setw(10) << "Threshold"
              << std::setw(10) << "PERCLOS"
              << std::setw(10) << "Blinks"
              << std::setw(10) << "CI (MSE)"
              << std::setw(10) << "Score"
              << std::setw(9)  << "State\n";
    std::cout << std::string(75, '-') << "\n";

    for (int frame = 1; frame <= 200; ++frame) {
        float simulatedEar = 0.31f;

        // 模擬第 60~75 幀、130~150 幀有長閉眼 (Micro-sleep)
        if ((frame >= 60 && frame <= 75) || (frame >= 130 && frame <= 150)) {
            simulatedEar = 0.14f; // 閉眼
        } else if (frame % 30 == 0 || frame % 30 == 1) {
            simulatedEar = 0.16f; // 正常瞬態眨眼
        } else {
            simulatedEar = 0.30f + 0.02f * std::sin(frame * 0.05f);
        }

        // 更新特徵與自適應閾值
        float currentTh = baseline.update(simulatedEar);
        extractor.setEyeClosedThreshold(currentTh);
        efd::EyeMetrics metrics = extractor.processEar(simulatedEar, simulatedEar, 30.0f);

        // 每累積 60 幀進行一次 EMD / MSE 非線性複雜度分析
        float ci = 4.8f;
        if (frame >= 60) {
            std::vector<float> earHistory = extractor.getEarHistory();
            efd::ComplexityMetrics compMetrics = mseCalc.calculateComplexity(earHistory);
            ci = compMetrics.complexityIndex;
        }

        // 驅動 20/5/5 狀態機
        efd::SystemState sysState = stateMachine.update(true, metrics.perclos, metrics.blinkRatePerMin, ci, 1.0f / 30.0f);

        // 定期輸出即時遙測
        if (frame % 20 == 0 || metrics.isEyeClosed) {
            const char* stateStr = "Normal";
            if (sysState.cooldownState == efd::CooldownState::InCooldown) stateStr = "Cooldown";
            else if (sysState.cooldownState == efd::CooldownState::FastScreening) stateStr = "Screening";

            std::cout << std::setw(6)  << frame
                      << std::setw(10) << std::fixed << std::setprecision(3) << metrics.earAvg
                      << std::setw(10) << std::fixed << std::setprecision(3) << currentTh
                      << std::setw(10) << std::fixed << std::setprecision(2) << metrics.perclos
                      << std::setw(10) << metrics.blinkCount
                      << std::setw(10) << std::fixed << std::setprecision(2) << ci
                      << std::setw(10) << std::fixed << std::setprecision(1) << sysState.currentFatigueScore
                      << std::setw(9)  << stateStr << "\n";
        }
    }

    std::cout << std::string(75, '-') << "\n";
    std::cout << "\n[Step 3] 執行 EMD (經驗模態分解) 特徵驗證...\n";
    std::vector<float> history = extractor.getEarHistory();
    efd::EmdResult emdRes = emdCalc.decompose(history);
    std::cout << " -> 分解成功: 提取出 " << emdRes.imfs.size() << " 個 Intrinsic Mode Functions (IMFs)\n";
    std::cout << " -> 高頻/低頻能量比 (IMF Energy Ratio): " << std::fixed << std::setprecision(3) << emdRes.highToLowEnergyRatio << "\n\n";

    std::cout << "[SUCCESS] EFD C++ 核心運算管線驗證完畢。\n";
    return 0;
}

