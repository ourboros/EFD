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

#include <memory>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <thread>
#include <atomic>
#include <functional>

namespace efd {

struct PipelineTelemetry {
    RawFrame latestFrame;
    LandmarkDetectionResult detection;
    EyeMetrics eyeMetrics;
    ComplexityMetrics complexityMetrics;
    SystemState systemState;
    float currentThreshold = 0.21f;
    int64_t totalFramesProcessed = 0;
};

class VisionPipeline {
public:
    using TelemetryCallback = std::function<void(const PipelineTelemetry& telemetry)>;
    using AlertCallback = FatigueStateMachine::AlertCallback;

    VisionPipeline();
    ~VisionPipeline();

    bool start();
    void stop();
    bool isRunning() const;
    void setTelemetryCallback(TelemetryCallback callback);
    void setAlertCallback(AlertCallback callback);
    void calibrate(float durationSeconds = 3.0f);
    void setSimulatedEyeOpenness(float openness);

    FeatureExtractor& getFeatureExtractor() { return m_extractor; }
    AdaptiveBaseline& getAdaptiveBaseline() { return m_baseline; }
    FatigueStateMachine& getStateMachine() { return m_stateMachine; }

private:
    std::unique_ptr<CameraService>  m_camera;
    std::unique_ptr<FaceLandmarker> m_landmarker;
    FeatureExtractor m_extractor;
    AdaptiveBaseline m_baseline;
    EmdCalculator    m_emdCalculator;
    MseCalculator    m_mseCalculator;
    FatigueStateMachine m_stateMachine;

    std::atomic<bool> m_isRunning{false};
    std::thread m_inferenceWorker;

    std::queue<RawFrame> m_frameQueue;
    std::mutex m_queueMutex;
    std::condition_variable m_queueCv;
    static constexpr size_t MAX_QUEUE_SIZE = 3;

    TelemetryCallback m_telemetryCallback;
    std::atomic<int64_t> m_processedFrameCount{0};

    void inferenceLoop();
    void onCameraFrameReceived(const RawFrame& frame);
};

} // namespace efd

