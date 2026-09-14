#include "vision/VisionPipeline.hpp"
#include <chrono>

namespace efd {

VisionPipeline::VisionPipeline()
    : m_camera(std::make_unique<CameraService>(640, 480, 30.0f)),
      m_landmarker(std::make_unique<FaceLandmarker>()),
      m_extractor(300, 0.21f),
      m_baseline(0.31f, 0.7f, 1.5f, 300),
      m_emdCalculator(4, 15, 0.05f),
      m_mseCalculator(5, 2, 0.15f),
      m_stateMachine(1200, 300, 300) {
    m_landmarker->initialize();
}

VisionPipeline::~VisionPipeline() {
    stop();
}

bool VisionPipeline::start() {
    if (m_isRunning.load()) {
        return true;
    }

    m_isRunning.store(true);
    m_processedFrameCount.store(0);

    m_inferenceWorker = std::thread(&VisionPipeline::inferenceLoop, this);

    m_camera->setFrameCallback([this](const RawFrame& frame) {
        this->onCameraFrameReceived(frame);
    });
    m_camera->start();

    return true;
}

void VisionPipeline::stop() {
    if (m_isRunning.load()) {
        m_isRunning.store(false);
        m_queueCv.notify_all();

        if (m_camera) {
            m_camera->stop();
        }

        if (m_inferenceWorker.joinable()) {
            m_inferenceWorker.join();
        }

        std::lock_guard<std::mutex> lock(m_queueMutex);
        while (!m_frameQueue.empty()) {
            m_frameQueue.pop();
        }
    }
}

bool VisionPipeline::isRunning() const {
    return m_isRunning.load();
}

void VisionPipeline::setTelemetryCallback(TelemetryCallback callback) {
    m_telemetryCallback = std::move(callback);
}

void VisionPipeline::setAlertCallback(AlertCallback callback) {
    m_stateMachine.setAlertCallback(std::move(callback));
}

void VisionPipeline::setSimulatedEyeOpenness(float openness) {
    if (m_landmarker) {
        m_landmarker->setSimulatedEyeOpenness(openness);
    }
}

void VisionPipeline::calibrate(float durationSeconds) {
    std::vector<float> sampleList;
    int samples = static_cast<int>(durationSeconds * 30.0f);
    for (int i = 0; i < samples; ++i) {
        sampleList.push_back(0.31f);
    }
    m_baseline.calibrate(sampleList);
    m_extractor.setEyeClosedThreshold(m_baseline.getCurrentThreshold());
}

void VisionPipeline::onCameraFrameReceived(const RawFrame& frame) {
    std::lock_guard<std::mutex> lock(m_queueMutex);
    while (m_frameQueue.size() >= MAX_QUEUE_SIZE) {
        m_frameQueue.pop();
    }
    m_frameQueue.push(frame);
    m_queueCv.notify_one();
}

void VisionPipeline::inferenceLoop() {
    while (m_isRunning.load()) {
        RawFrame currentFrame;
        {
            std::unique_lock<std::mutex> lock(m_queueMutex);
            m_queueCv.wait(lock, [this]() {
                return !m_frameQueue.empty() || !m_isRunning.load();
            });

            if (!m_isRunning.load()) {
                break;
            }

            currentFrame = std::move(m_frameQueue.front());
            m_frameQueue.pop();
        }

        LandmarkDetectionResult detection = m_landmarker->detect(currentFrame);
        EyeMetrics eyeMetrics = m_extractor.processFrame(detection.landmarks, m_camera->getFps());
        float currentThreshold = m_baseline.update(eyeMetrics.earAvg, eyeMetrics.isEyeClosed);
        m_extractor.setEyeClosedThreshold(currentThreshold);

        ComplexityMetrics complexityMetrics;
        int64_t count = ++m_processedFrameCount;
        if (count >= 30 && count % 15 == 0) {
            std::vector<float> earHistory = m_extractor.getEarHistory();
            complexityMetrics = m_mseCalculator.calculateComplexity(earHistory);
        } else if (count < 30) {
            complexityMetrics.complexityIndex = 4.5f;
        }

        float deltaSec = 1.0f / m_camera->getFps();
        SystemState state = m_stateMachine.update(
            detection.hasFace,
            eyeMetrics.perclos,
            eyeMetrics.blinkRatePerMin,
            complexityMetrics.complexityIndex,
            deltaSec
        );

        if (m_telemetryCallback) {
            PipelineTelemetry telemetry;
            telemetry.latestFrame = std::move(currentFrame);
            telemetry.detection = std::move(detection);
            telemetry.eyeMetrics = eyeMetrics;
            telemetry.complexityMetrics = complexityMetrics;
            telemetry.systemState = state;
            telemetry.currentThreshold = currentThreshold;
            telemetry.totalFramesProcessed = count;

            m_telemetryCallback(telemetry);
        }
    }
}

} // namespace efd

