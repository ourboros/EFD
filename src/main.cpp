#include <iostream>
#include <vector>
#include <cmath>
#include <iomanip>
#include <thread>
#include <chrono>
#include <atomic>
#include <string>

#include "efd/version.hpp"
#include "efd/types.hpp"
#include "analysis/FeatureExtractor.hpp"
#include "analysis/EmdCalculator.hpp"
#include "analysis/MseCalculator.hpp"
#include "analysis/AdaptiveBaseline.hpp"
#include "state/FatigueStateMachine.hpp"
#include "platform/PlatformLifecycleAdapter.hpp"
#include "engine/AsyncPipelineEngine.hpp"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include "platform/windows/SystemTrayManager.hpp"
#include "ui/NativeWelcomeWindow.hpp"
#endif

// CLI 模擬測試模式
int runCliSimulation() {
    std::cout << "====================================================\n";
    std::cout << "  Eye Fatigue Detection (EFD) Engine v" << EFD_VERSION_STRING << "\n";
    std::cout << "  Phase 4: Multi-Platform Polish, Floating HUD & Cloud Sync\n";
    std::cout << "  Platform: Cross-Platform (Windows/macOS/Android/iOS)\n";
    std::cout << "====================================================\n\n";

    efd::AsyncPipelineEngine engine(efd::PlatformType::Windows);
#ifdef _WIN32
    efd::SystemTrayManager tray;
    tray.initialize(nullptr);

    engine.setAlertCallback([&tray](efd::FatigueLevel level, float score, const std::string& msg) {
        std::cout << "\n>>> [ALERT TRIGGERED] Level: " << static_cast<int>(level)
                  << " | Fatigue Score: " << std::fixed << std::setprecision(1) << score
                  << " | " << msg << " <<<\n";

        if (level == efd::FatigueLevel::SevereWarning) {
            std::cout << " -> [Windows 系統通知處已發送] 標題：【你的眼睛處於疲勞狀態，請適當休息】\n\n";
            tray.showBalloonNotification(
                L"【你的眼睛處於疲勞狀態，請適當休息】",
                L"你的眼睛處於疲勞狀態，請適當休息",
                level
            );
        } else if (level == efd::FatigueLevel::Attention) {
            std::cout << " -> [Windows 系統通知處已發送] 標題：【EFD 用眼疲勞提醒】\n\n";
            tray.showBalloonNotification(
                L"【EFD 用眼疲勞提醒】",
                L"偵測到用眼疲勞，建議休息或遠眺放鬆。",
                level
            );
        }
    });
#else
    engine.setAlertCallback([](efd::FatigueLevel level, float score, const std::string& msg) {
        std::cout << "\n>>> [ALERT TRIGGERED] Level: " << static_cast<int>(level)
                  << " | Fatigue Score: " << std::fixed << std::setprecision(1) << score
                  << " | " << msg << " <<<\n";
    });
#endif

    std::atomic<int> printCounter{0};
    engine.setTelemetryCallback([&printCounter](const efd::EngineTelemetry& telemetry) {
        int count = ++printCounter;
        if (count % 15 == 0 || telemetry.eyeMetrics.isEyeClosed) {
            const char* stateStr = "Normal";
            if (telemetry.systemState.cooldownState == efd::CooldownState::InCooldown) stateStr = "Cooldown";
            else if (telemetry.systemState.cooldownState == efd::CooldownState::FastScreening) stateStr = "Screening";
            else if (telemetry.systemState.cooldownState == efd::CooldownState::AwayPaused) stateStr = "Away";

            std::cout << std::setw(6)  << telemetry.totalFramesProcessed
                      << std::setw(10) << std::fixed << std::setprecision(3) << telemetry.eyeMetrics.earAvg
                      << std::setw(10) << std::fixed << std::setprecision(3) << telemetry.currentThreshold
                      << std::setw(10) << std::fixed << std::setprecision(2) << telemetry.eyeMetrics.perclos
                      << std::setw(10) << telemetry.eyeMetrics.blinkCount
                      << std::setw(10) << std::fixed << std::setprecision(2) << telemetry.complexityMetrics.complexityIndex
                      << std::setw(10) << std::fixed << std::setprecision(1) << telemetry.systemState.currentFatigueScore
                      << std::setw(9)  << stateStr << "\n";
        }
    });

    std::cout << "[Step 1] 進行動態自適應基準校準 (Baseline Calibration)...\n";
    engine.calibrate(3.0f);
    std::cout << " -> 校準完成: 初始判定閾值 = " 
              << engine.getAdaptiveBaseline().getCurrentThreshold() << "\n\n";

    std::cout << "[Step 2] 啟動非同步多執行緒管線 (Thread 1: Capture, Thread 2: Inference, Thread 3: Signal/State)...\n";
    std::cout << " -> 平台生命週期適配: " << engine.getLifecycleAdapter().getStatusSummary() << "\n";
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

    engine.start();

    // 階段 A: 正常清醒睜眼
    engine.setSimulatedEyeOpenness(1.0f);
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    // 階段 B: 模擬微睡眠閉眼
    std::cout << "\n[Event 1] 模擬使用者連續閉眼 (Micro-Sleep) -> 觸發 Level 1 警報並進入 20m 冷卻...\n";
    engine.setSimulatedEyeOpenness(0.0f);
    std::this_thread::sleep_for(std::chrono::milliseconds(1500));

    // 階段 C: 模擬系統休眠
    std::cout << "\n[Event 2] 模擬系統休眠事件 (App Suspended / Screen Lock)...\n";
    engine.pause();
    std::cout << " -> 管線已暫停: " << engine.getLifecycleAdapter().getStatusSummary() << "\n";
    std::this_thread::sleep_for(std::chrono::milliseconds(800));

    // 階段 D: 模擬系統喚醒與熱重啟
    std::cout << "\n[Event 3] 模擬系統喚醒與熱重啟 (Hot-Resume)...\n";
    engine.resume();
    engine.setSimulatedEyeOpenness(1.0f);
    std::cout << " -> 管線已恢復: " << engine.getLifecycleAdapter().getStatusSummary() << "\n";
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    // 階段 D2: 模擬嚴重用眼疲勞升級至紅色危險狀態
    std::cout << "\n[Event 4] 模擬嚴重用眼疲勞 (Severe Warning / 紅色危險狀態) -> 觸發 Windows Toast 氣泡通知...\n";
    engine.getStateMachine().reset();
    engine.getStateMachine().update(true, 0.45f, 3.0f, 1.0f, 1.0f);
    std::this_thread::sleep_for(std::chrono::milliseconds(1500));

    engine.stop();
    std::cout << std::string(75, '-') << "\n\n";

    // 階段 E: EMD 訊號分解驗證
    std::cout << "[Step 3] 執行 EMD (經驗模態分解) 訊號分解驗證...\n";
    std::vector<float> sampleEarSignal;
    for (int i = 0; i < 60; ++i) {
        float t = static_cast<float>(i) / 30.0f;
        float val = 0.30f + 0.05f * std::sin(2.0f * 3.14159f * 0.2f * t) + 0.02f * std::sin(2.0f * 3.14159f * 2.5f * t);
        sampleEarSignal.push_back(val);
    }
    efd::EmdCalculator emd(4, 15, 0.05f);
    auto emdResult = emd.decompose(sampleEarSignal);
    std::cout << " -> 分解成功: 提取出 " << emdResult.imfs.size() << " 個 Intrinsic Mode Functions (IMFs)\n";
    std::cout << " -> 高頻/低頻能量比 (IMF Energy Ratio): " << std::fixed << std::setprecision(3) << emdResult.highToLowEnergyRatio << "\n\n";

    // 階段 F: 第四階段非同步科研資料同步與門禁驗證
    std::cout << "[Step 4] 執行第四階段 (Phase 4) 非同步科研資料同步與校驗碼生成...\n";
    std::atomic<bool> syncDone{false};
    std::string signedToken;
    engine.getSyncWorker().triggerSync("SUBJ-SIM-2026", 14, "Q1:5,Q2:VerySatisfied,Q3:NoDiscomfort", [&](const efd::SyncResult& res) {
        signedToken = res.unlockToken;
        syncDone = true;
    });

    for (int i = 0; i < 100 && !syncDone.load(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    std::cout << " -> 科研資料同步完成: 解鎖憑證 = " << signedToken << "\n\n";

    std::cout << "[SUCCESS] 第四階段 (Phase 4) 懸浮指標 HUD、系統托盤、雲端同步與 14 天科研門禁驗證全部完成！\n";
    return 0;
}

int main(int argc, char* argv[]) {
#ifdef _WIN32
    // 設定 Windows 終端機為 UTF-8 編碼 (字碼頁 65001)，消除繁體中文亂碼
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif

    // 若命令列帶有 --cli 參數，執行終端機模擬模式
    if (argc > 1 && std::string(argv[1]) == "--cli") {
        return runCliSimulation();
    }

    // 預設模式：Windows 下啟動原生視窗，其他系統執行命令列模擬
#ifdef _WIN32
    std::cout << "[EFD System] 啟動 EFD 七階段視覺校準、疲勞監控與科研後測圖形介面系統...\n";
    efd::NativeWelcomeWindow window(960, 640);
    return window.run();
#else
    return runCliSimulation();
#endif
}
