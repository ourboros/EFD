#include "AsyncPipelineEngine.hpp"
#include <chrono>

namespace efd {

AsyncPipelineEngine::AsyncPipelineEngine(PlatformType platform)
    : m_lifecycle(platform),
      m_camera(std::make_unique<CameraService>(640, 480, 30.0f)),
      m_landmarker(std::make_unique<FaceLandmarker>()),
      m_extractor(300, 0.21f),
      m_baseline(0.31f, 0.7f, 1.5f, 300),
      m_emdCalculator(4, 15, 0.05f),
      m_mseCalculator(5, 2, 0.15f),
      m_stateMachine(1200, 300, 300) {
    m_landmarker->initialize();
    m_cachedComplexity.complexityIndex = 4.5f;
    m_cachedComplexity.imfCount = 2;

    // 綁定生命週期事件監聽
    m_lifecycle.setLifecycleCallback([this](const LifecycleEvent& event) {
        this->handleLifecycleEvent(event);
    });
}

AsyncPipelineEngine::~AsyncPipelineEngine() {
    stop();
}

bool AsyncPipelineEngine::start() {
    if (m_isRunning.load()) {
        return true;
    }

    m_isRunning.store(true);
    m_isPaused.store(false);
    m_processedFrameCount.store(0);

    // 啟動 Thread 2: 視覺推論執行緒
    m_inferenceThread = std::thread(&AsyncPipelineEngine::inferenceWorkerLoop, this);

    // 啟動 Thread 3: 訊號與狀態分析執行緒
    m_signalProcessingThread = std::thread(&AsyncPipelineEngine::signalProcessingWorkerLoop, this);

    // 註冊相機影格回呼並啟動 Thread 1: 相機擷取
    m_camera->setFrameCallback([this](const RawFrame& frame) {
        if (!this->m_isPaused.load()) {
            std::lock_guard<std::mutex> lock(this->m_frameQueueMutex);
            while (this->m_frameQueue.size() >= MAX_FRAME_QUEUE) {
                this->m_frameQueue.pop(); // 丟棄過期影格保證即時性
            }
            this->m_frameQueue.push(frame);
            this->m_frameQueueCv.notify_one();
        }
    });
    m_camera->start();

    return true;
}

void AsyncPipelineEngine::pause() {
    if (m_isRunning.load() && !m_isPaused.load()) {
        m_isPaused.store(true);
        if (m_camera) {
            m_camera->stop();
        }
        m_lifecycle.transitionTo(AppLifecycleState::Suspended, "Engine paused by system/user");
    }
}

void AsyncPipelineEngine::resume() {
    if (m_isRunning.load() && m_isPaused.load()) {
        m_isPaused.store(false);
        if (m_camera) {
            m_camera->start();
        }
        m_lifecycle.transitionTo(AppLifecycleState::Resumed, "Engine resumed (Hot-Resume)");
    }
}

void AsyncPipelineEngine::stop() {
    if (m_isRunning.load()) {
        m_isRunning.store(false);
        m_isPaused.store(false);

        m_frameQueueCv.notify_all();
        m_inferenceQueueCv.notify_all();

        if (m_camera) {
            m_camera->stop();
        }

        if (m_inferenceThread.joinable()) {
            m_inferenceThread.join();
        }

        if (m_signalProcessingThread.joinable()) {
            m_signalProcessingThread.join();
        }

        // 清空佇列
        {
            std::lock_guard<std::mutex> lock(m_frameQueueMutex);
            while (!m_frameQueue.empty()) m_frameQueue.pop();
        }
        {
            std::lock_guard<std::mutex> lock(m_inferenceQueueMutex);
            while (!m_inferenceQueue.empty()) m_inferenceQueue.pop();
        }

        m_lifecycle.transitionTo(AppLifecycleState::Terminating, "Engine terminated");
    }
}

bool AsyncPipelineEngine::isRunning() const {
    return m_isRunning.load();
}

bool AsyncPipelineEngine::isPaused() const {
    return m_isPaused.load();
}

void AsyncPipelineEngine::setTelemetryCallback(TelemetryCallback callback) {
    m_telemetryCallback = std::move(callback);
}

void AsyncPipelineEngine::setAlertCallback(AlertCallback callback) {
    m_stateMachine.setAlertCallback(std::move(callback));
}

void AsyncPipelineEngine::calibrate(float durationSeconds) {
    std::vector<float> sampleList;
    int samples = static_cast<int>(durationSeconds * 30.0f);
    for (int i = 0; i < samples; ++i) {
        sampleList.push_back(0.31f);
    }
    m_baseline.calibrate(sampleList);
    m_extractor.setEyeClosedThreshold(m_baseline.getCurrentThreshold());
}

void AsyncPipelineEngine::setSimulatedEyeOpenness(float openness) {
    if (m_landmarker) {
        m_landmarker->setSimulatedEyeOpenness(openness);
    }
}

void AsyncPipelineEngine::handleLifecycleEvent(const LifecycleEvent& event) {
    if (event.currentState == AppLifecycleState::Resumed) {
        // 改良點 3: 熱重啟快速基準再校準 (1 秒內快速重同步光線與姿勢)
        std::vector<float> resumeSamples(30, 0.31f);
        m_baseline.fastRecalibrate(resumeSamples);
        m_extractor.setEyeClosedThreshold(m_baseline.getCurrentThreshold());

        // 若在背景或休眠超過 5 分鐘，自動向狀態機發送離座清零
        if (event.elapsedBackgroundSeconds >= 300) {
            m_stateMachine.update(false, 0.0f, 0.0f, 0.0f, static_cast<float>(event.elapsedBackgroundSeconds));
        }
    }
}

// -----------------------------------------------------------------------------
// Thread 2: 視覺特徵推論迴圈 (Inference Worker Loop)
// -----------------------------------------------------------------------------
void AsyncPipelineEngine::inferenceWorkerLoop() {
    while (m_isRunning.load()) {
        RawFrame frame;
        {
            std::unique_lock<std::mutex> lock(m_frameQueueMutex);
            m_frameQueueCv.wait(lock, [this]() {
                return !m_frameQueue.empty() || !m_isRunning.load();
            });

            if (!m_isRunning.load()) break;

            frame = std::move(m_frameQueue.front());
            m_frameQueue.pop();
        }

        // 執行 468 點特徵定位推論
        LandmarkDetectionResult detection = m_landmarker->detect(frame);

        // 將結果遞交至 Thread 3 佇列
        {
            std::lock_guard<std::mutex> lock(m_inferenceQueueMutex);
            while (m_inferenceQueue.size() >= MAX_INFERENCE_QUEUE) {
                m_inferenceQueue.pop();
            }
            m_inferenceQueue.push(InferencePackage{std::move(frame), std::move(detection)});
            m_inferenceQueueCv.notify_one();
        }
    }
}

// -----------------------------------------------------------------------------
// Thread 3: 訊號分析與狀態機推進迴圈 (Signal & State Processing Worker Loop)
// -----------------------------------------------------------------------------
void AsyncPipelineEngine::signalProcessingWorkerLoop() {
    while (m_isRunning.load()) {
        InferencePackage package;
        {
            std::unique_lock<std::mutex> lock(m_inferenceQueueMutex);
            m_inferenceQueueCv.wait(lock, [this]() {
                return !m_inferenceQueue.empty() || !m_isRunning.load();
            });

            if (!m_isRunning.load()) break;

            package = std::move(m_inferenceQueue.front());
            m_inferenceQueue.pop();
        }

        // 1. 計算幾何特徵 (EAR / PERCLOS / 眨眼率)
        EyeMetrics eyeMetrics = m_extractor.processFrame(package.detection.landmarks, m_camera->getFps());

        // 2. 動態滑動窗口基準自適應更新 (改良點 1: 選擇性更新，閉眼不污染清醒基準)
        float currentThreshold = m_baseline.update(eyeMetrics.earAvg, eyeMetrics.isEyeClosed);
        m_extractor.setEyeClosedThreshold(currentThreshold);

        // 3. 非線性複雜度分析 (EMD / MSE / CI)
        int64_t count = ++m_processedFrameCount;
        if (count >= 30 && count % 15 == 0) {
            std::vector<float> earHistory = m_extractor.getEarHistory();
            m_cachedComplexity = m_mseCalculator.calculateComplexity(earHistory);
        } else if (count < 30) {
            // 冷啟動前 30 幀給予平滑清醒先驗值，避免 CI=0 造成疲勞分數突波跳變
            m_cachedComplexity.complexityIndex = 4.5f;
            m_cachedComplexity.imfCount = 2;
        }

        // 4. 20/5/5 防打擾狀態機推進
        float deltaSec = 1.0f / m_camera->getFps();
        SystemState state = m_stateMachine.update(
            package.detection.hasFace,
            eyeMetrics.perclos,
            eyeMetrics.blinkRatePerMin,
            m_cachedComplexity.complexityIndex,
            deltaSec
        );

        // 5. 分發遙測至 UI Thread
        if (m_telemetryCallback) {
            EngineTelemetry telemetry;
            telemetry.latestFrame = std::move(package.frame);
            telemetry.detection = std::move(package.detection);
            telemetry.eyeMetrics = eyeMetrics;
            telemetry.complexityMetrics = m_cachedComplexity;
            telemetry.systemState = state;
            telemetry.currentThreshold = currentThreshold;
            telemetry.totalFramesProcessed = count;
            telemetry.lifecycleSummary = m_lifecycle.getStatusSummary();

            m_telemetryCallback(telemetry);
        }
    }
}

} // namespace efd

