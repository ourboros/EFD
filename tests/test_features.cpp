#include <iostream>
#include <vector>
#include <cmath>
#include <cassert>
#include <chrono>
#include <thread>
#include <atomic>

#include "efd/types.hpp"
#include "analysis/FeatureExtractor.hpp"
#include "analysis/EmdCalculator.hpp"
#include "analysis/MseCalculator.hpp"
#include "analysis/AdaptiveBaseline.hpp"
#include "state/FatigueStateMachine.hpp"
#include "vision/RawFrame.hpp"
#include "vision/FaceLandmarker.hpp"
#include "vision/CameraService.hpp"
#include "vision/VisionPipeline.hpp"
#include "vision/drivers/SyntheticCameraDriver.hpp"
#include "platform/PlatformLifecycleAdapter.hpp"
#include "engine/AsyncPipelineEngine.hpp"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

void testEarCalculation() {
    std::cout << "[TEST] 1. 測試 EAR 幾何特徵計算...";
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
    std::cout << "[TEST] 2. 測試動態滑動窗口基準自適應與選擇性更新...";
    efd::AdaptiveBaseline baseline(0.30f, 0.7f, 1.5f, 100);
    
    // 正常睜眼樣本更新
    for (int i = 0; i < 50; ++i) {
        baseline.update(0.29f, false);
    }
    float thNormal = baseline.getCurrentThreshold();
    assert(thNormal >= 0.15f && thNormal <= 0.30f);

    // 模擬微睡眠 (連續閉眼 EAR = 0.100) -> 改良驗證: 閾值不應被閉眼樣本崩塌拉低
    for (int i = 0; i < 50; ++i) {
        baseline.update(0.10f, true); // 標記為閉眼
    }
    float thAfterClosed = baseline.getCurrentThreshold();
    // 驗證選擇性更新生效: 閾值依然穩固在清醒基準附近，未崩塌至 0.12
    assert(std::fabs(thAfterClosed - thNormal) < 0.03f);

    // 測試熱重啟快速再校準 (Fast Recalibrate)
    std::vector<float> resumeSamples(30, 0.33f);
    baseline.fastRecalibrate(resumeSamples);
    assert(baseline.getCalibrationData().baselineEar > 0.30f);

    std::cout << " [PASS] (微睡眠抗污染正常, 閾值=" << thAfterClosed << ")\n";
}

void testMseComplexity() {
    std::cout << "[TEST] 3. 測試 MSE 多尺度熵與 CI 複雜度計算...";
    efd::MseCalculator mse(3, 2, 0.15f);

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

    std::vector<float> signal(120);
    for (size_t i = 0; i < 120; ++i) {
        signal[i] = std::sin(i * 0.05f) + 0.5f * std::sin(i * 0.4f);
    }

    efd::EmdResult result = emd.decompose(signal);
    assert(!result.imfs.empty());
    std::cout << " [PASS] (成功分解出 " << result.imfs.size() << " 個 IMF)\n";
}

void testFatigueStateMachineFullCycle() {
    std::cout << "[TEST] 5. 測試 20/5/5 防打擾與警報升級全週期狀態機...";
    efd::FatigueStateMachine fsm(1200, 300, 300);

    bool alertTriggered = false;
    efd::FatigueLevel lastAlertLevel = efd::FatigueLevel::Relaxed;
    fsm.setAlertCallback([&](efd::FatigueLevel level, float, const std::string&) {
        alertTriggered = true;
        lastAlertLevel = level;
    });

    // 1. 正常清醒狀態
    efd::SystemState s1 = fsm.update(true, 0.05f, 16.0f, 4.5f, 1.0f);
    assert(s1.fatigueLevel == efd::FatigueLevel::Relaxed);
    assert(s1.cooldownState == efd::CooldownState::NormalTracking);

    // 2. 觸發一級疲勞警報 (PERCLOS 升高，CI 降低)
    alertTriggered = false;
    efd::SystemState s2 = fsm.update(true, 0.30f, 6.0f, 2.0f, 1.0f);
    assert(s2.fatigueLevel == efd::FatigueLevel::Attention);
    assert(s2.cooldownState == efd::CooldownState::InCooldown);
    assert(alertTriggered && lastAlertLevel == efd::FatigueLevel::Attention);

    // 3. 冷卻期內經過 5 分鐘進入快篩 (Fast Screening)
    efd::SystemState s3 = fsm.update(true, 0.20f, 14.0f, 3.5f, 301.0f);
    assert(s3.cooldownState == efd::CooldownState::FastScreening);

    // 4. 快篩期內疲勞持續惡化 (PERCLOS 0.35, CI 1.5) -> 警報升級至 SevereWarning
    alertTriggered = false;
    efd::SystemState s4 = fsm.update(true, 0.35f, 4.0f, 1.5f, 1.0f);
    assert(s4.fatigueLevel == efd::FatigueLevel::SevereWarning);
    assert(alertTriggered && lastAlertLevel == efd::FatigueLevel::SevereWarning);

    // 5. 離座 5 分鐘自動清零重置
    efd::SystemState s5 = fsm.update(false, 0.0f, 0.0f, 0.0f, 305.0f);
    assert(s5.fatigueLevel == efd::FatigueLevel::UserAway);
    assert(s5.cooldownState == efd::CooldownState::AwayPaused);

    // 6. 使用者回座，自動重啟正常追蹤
    efd::SystemState s6 = fsm.update(true, 0.05f, 16.0f, 4.5f, 1.0f);
    assert(s6.fatigueLevel == efd::FatigueLevel::Relaxed);
    assert(s6.cooldownState == efd::CooldownState::NormalTracking);

    std::cout << " [PASS] (涵蓋 Attention -> Cooldown -> Screening -> SevereWarning -> AwayReset)\n";
}

void testPlatformLifecycleAdapter() {
    std::cout << "[TEST] 6. 測試跨平台生命週期適配器 (PlatformLifecycleAdapter)...";
    efd::PlatformLifecycleAdapter adapter(efd::PlatformType::Windows);
    assert(adapter.getPlatformType() == efd::PlatformType::Windows);
    assert(adapter.supportsSystemTray());

    bool lifecycleCallbackFired = false;
    efd::AppLifecycleState targetState = efd::AppLifecycleState::Active;
    adapter.setLifecycleCallback([&](const efd::LifecycleEvent& event) {
        lifecycleCallbackFired = true;
        targetState = event.currentState;
    });

    // 模擬休眠掛起
    adapter.notifySystemSleep();
    assert(adapter.getCurrentState() == efd::AppLifecycleState::Suspended);
    assert(lifecycleCallbackFired && targetState == efd::AppLifecycleState::Suspended);

    // 模擬休眠喚醒 (Hot Resume)
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    adapter.notifySystemWake();
    assert(adapter.getCurrentState() == efd::AppLifecycleState::Resumed);
    assert(targetState == efd::AppLifecycleState::Resumed);

    std::cout << " [PASS] (" << adapter.getStatusSummary() << ")\n";
}

void testAsyncPipelineEngine() {
    std::cout << "[TEST] 7. 測試五執行緒非同步管線引擎 (AsyncPipelineEngine)...";
    efd::AsyncPipelineEngine engine(efd::PlatformType::Windows);

    std::atomic<int> telemetryCount{0};
    engine.setTelemetryCallback([&](const efd::EngineTelemetry& t) {
        if (t.detection.hasFace) {
            telemetryCount++;
        }
    });

    // 啟動五執行緒管線 (在單元測試環境中使用 Synthetic 測試相機)
    engine.getCameraService().startSynthetic();
    assert(engine.start());
    assert(engine.isRunning());
    assert(!engine.isPaused());

    // 執行 250ms
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
    int countBeforePause = telemetryCount.load();
    assert(countBeforePause > 0);

    // 測試暫停 (Pause)
    engine.pause();
    assert(engine.isPaused());
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // 測試恢復 (Resume)
    engine.resume();
    assert(!engine.isPaused());
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    assert(telemetryCount.load() > countBeforePause);

    // 正常停止
    engine.stop();
    assert(!engine.isRunning());

    std::cout << " [PASS] (共非同步處理 " << telemetryCount.load() << " 幀，Pause/Resume 正常)\n";
}

void testCameraDriversAndHal() {
    std::cout << "[TEST] 8. 測試跨平台相機硬體抽象層 (Camera HAL & Drivers)...";
    efd::CameraService cameraService(640, 480, 30.0f);

    // 1. 測試設備列舉 (包含實體與虛擬鏡頭)
    auto devices = cameraService.enumerateDevices();
    assert(!devices.empty());

    // 2. 測試 Synthetic 驅動取幀
    efd::SyntheticCameraDriver synthetic;
    std::atomic<int> frameCount{0};
    synthetic.setFrameCallback([&frameCount](const efd::RawFrame& frame) {
        if (frame.isValid()) {
            frameCount++;
        }
    });

    efd::CameraConfig config;
    config.width = 640;
    config.height = 480;
    config.fps = 30.0f;
    assert(synthetic.open(config));
    assert(synthetic.isOpened());

    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    assert(frameCount.load() > 0);
    synthetic.close();
    assert(!synthetic.isOpened());

    // 3. 測試 CameraService 整合啟動 (自動偵測 Front / 智慧回退)
    assert(cameraService.start(0, efd::CameraFacing::Front));
    assert(cameraService.isRunning());
    std::string driverName = cameraService.getActiveDriverName();
    cameraService.stop();
    assert(!cameraService.isRunning());

    std::cout << " [PASS] (驅動: " << driverName << ", 列舉到 " << devices.size() << " 個設備)\n";
}

int main() {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif

    std::cout << "========================================================\n";
    std::cout << "  EFD 跨平台核心與相機驅動 (Camera HAL) 整合單元測試\n";
    std::cout << "  - 5-Thread Pipeline Engine\n";
    std::cout << "  - Full 20/5/5 State Machine & Escalation\n";
    std::cout << "  - Platform Lifecycle (Sleep/Wake/Hot-Resume)\n";
    std::cout << "  - Camera HAL & Drivers (WMF / Camera2 / Synthetic)\n";
    std::cout << "========================================================\n";

    testEarCalculation();
    testAdaptiveBaseline();
    testMseComplexity();
    testEmdDecomposition();
    testFatigueStateMachineFullCycle();
    testPlatformLifecycleAdapter();
    testAsyncPipelineEngine();
    testCameraDriversAndHal();

    std::cout << "\n[ALL TESTS PASSED] 全部 8 項測試順利通過！\n";
    return 0;
}
