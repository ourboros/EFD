#pragma once

#include "efd/types.hpp"
#include "vision/RawFrame.hpp"
#include "vision/CameraService.hpp"
#include "vision/FaceLandmarker.hpp"
#include "analysis/FeatureExtractor.hpp"
#include "analysis/AdaptiveBaseline.hpp"
#include "analysis/EmdCalculator.hpp"
#include "analysis/MseCalculator.hpp"
#include "state/FatigueStateMachine.hpp"
#include "platform/PlatformLifecycleAdapter.hpp"

#include <memory>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <thread>
#include <atomic>
#include <functional>

namespace efd {

struct EngineTelemetry {
    RawFrame latestFrame;
    LandmarkDetectionResult detection;
    EyeMetrics eyeMetrics;
    ComplexityMetrics complexityMetrics;
    SystemState systemState;
    float currentThreshold = 0.21f;
    int64_t totalFramesProcessed = 0;
    std::string lifecycleSummary;
};

class AsyncPipelineEngine {
public:
    using TelemetryCallback = std::function<void(const EngineTelemetry& telemetry)>;
    using AlertCallback = FatigueStateMachine::AlertCallback;

    explicit AsyncPipelineEngine(PlatformType platform = PlatformType::Windows);
    ~AsyncPipelineEngine();

    // 啟動五執行緒非同步管線
    bool start();

    // 暫停管線 (進入背景或休眠時)
    void pause();

    // 恢復管線 (從背景或休眠喚醒時)
    void resume();

    // 完全停止管線
    void stop();

    // 查詢狀態
    bool isRunning() const;
    bool isPaused() const;

    // 註冊即時遙測回呼 (UI Thread 安全接收)
    void setTelemetryCallback(TelemetryCallback callback);

    // 註冊疲勞警報回呼
    void setAlertCallback(AlertCallback callback);

    // 執行初始基準校準
    void calibrate(float durationSeconds = 3.0f);

    // 設定模擬眼睛開合狀態 (供測試與驗證)
    void setSimulatedEyeOpenness(float openness);

    // 存取各核心模組
    PlatformLifecycleAdapter& getLifecycleAdapter() { return m_lifecycle; }
    FatigueStateMachine& getStateMachine() { return m_stateMachine; }
    AdaptiveBaseline& getAdaptiveBaseline() { return m_baseline; }
    FeatureExtractor& getFeatureExtractor() { return m_extractor; }
    CameraService& getCameraService() { return *m_camera; }

private:
    PlatformLifecycleAdapter m_lifecycle;

    std::unique_ptr<CameraService>  m_camera;
    std::unique_ptr<FaceLandmarker> m_landmarker;
    FeatureExtractor m_extractor;
    AdaptiveBaseline m_baseline;
    EmdCalculator    m_emdCalculator;
    MseCalculator    m_mseCalculator;
    FatigueStateMachine m_stateMachine;

    std::atomic<bool> m_isRunning{false};
    std::atomic<bool> m_isPaused{false};

    // Thread 2: 視覺推論執行緒
    std::thread m_inferenceThread;
    // Thread 3: 訊號與複雜度計算執行緒
    std::thread m_signalProcessingThread;

    // 影像佇列 (Thread 1 -> Thread 2)
    std::queue<RawFrame> m_frameQueue;
    std::mutex m_frameQueueMutex;
    std::condition_variable m_frameQueueCv;
    static constexpr size_t MAX_FRAME_QUEUE = 3;

    // 特徵點佇列 (Thread 2 -> Thread 3)
    struct InferencePackage {
        RawFrame frame;
        LandmarkDetectionResult detection;
    };
    std::queue<InferencePackage> m_inferenceQueue;
    std::mutex m_inferenceQueueMutex;
    std::condition_variable m_inferenceQueueCv;
    static constexpr size_t MAX_INFERENCE_QUEUE = 3;

    TelemetryCallback m_telemetryCallback;
    std::atomic<int64_t> m_processedFrameCount{0};
    ComplexityMetrics m_cachedComplexity;

    void inferenceWorkerLoop();
    void signalProcessingWorkerLoop();
    void handleLifecycleEvent(const LifecycleEvent& event);
};

} // namespace efd

