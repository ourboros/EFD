#include "AsyncPipelineEngine.hpp"
#include <chrono>
#include <algorithm>
#include <ctime>
#include <fstream>
#include <iostream>
#include <sstream>

namespace efd {

namespace {

constexpr float kComplexityResampleHz = 10.0f;
constexpr float kComplexityMinWindowSec = 30.0f;
constexpr float kComplexityPriorCi = 4.5f;      // 資料不足時的清醒先驗值 (不計複雜度分)
constexpr float kAbsentHistoryResetSec = 3.0f;  // 離開超過 3 秒清空特徵歷史
constexpr int64_t kSyncRetryDelayMs = 10 * 60 * 1000;

int64_t steadyNowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

int64_t epochNowSec() {
    return std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

std::tm localTimeOf(std::time_t t) {
    std::tm tmOut{};
#ifdef _WIN32
    localtime_s(&tmOut, &t);
#else
    localtime_r(&t, &tmOut);
#endif
    return tmOut;
}

int toDateInt(const std::tm& tmValue) {
    return (tmValue.tm_year + 1900) * 10000 + (tmValue.tm_mon + 1) * 100 + tmValue.tm_mday;
}

std::string joinPath(const std::string& dir, const std::string& file) {
    if (dir.empty()) return file;
    char last = dir.back();
    if (last == '/' || last == '\\') return dir + file;
    return dir + "/" + file;
}

} // anonymous namespace

AsyncPipelineEngine::AsyncPipelineEngine(PlatformType platform)
    : m_lifecycle(platform),
      m_syncWorker(m_database),
      m_camera(std::make_unique<CameraService>(640, 480, 30.0f)),
      m_landmarker(std::make_unique<FaceLandmarker>()),
      m_extractor(60.0f, 0.21f),
      m_baseline(0.31f, 0.7f, 1.5f, 300),
      m_emdCalculator(4, 15, 0.05f),
      m_mseCalculator(5, 2, 0.15f),
      m_stateMachine(1200, 300, 300) {
    m_landmarker->initialize();
    m_database.initialize();
    m_studyTracker.initialize();
    m_syncWorker.setEndpointUrl(m_studyTracker.getCloudSyncEndpoint());
    m_cachedComplexity.complexityIndex = kComplexityPriorCi;
    restoreFromStorage();

    // 綁定生命週期事件監聽
    m_lifecycle.setLifecycleCallback([this](const LifecycleEvent& event) {
        this->handleLifecycleEvent(event);
    });
}

AsyncPipelineEngine::~AsyncPipelineEngine() {
    stop();
}

void AsyncPipelineEngine::setDataDirectory(const std::string& directory, const std::string& configFileName) {
    if (directory.empty()) return;
    m_studyTracker.setConfigPath(joinPath(directory, configFileName));
    m_database.setDatabasePath(joinPath(directory, "efd_study_data.dat"));
    m_syncWorker.setEndpointUrl(m_studyTracker.getCloudSyncEndpoint());
    m_syncWorker.setDumpPath(joinPath(directory, "efd_daily_sync.json"));
    m_fsmStatePath = joinPath(directory, "efd_fsm_state.txt");
    restoreFromStorage();
}

void AsyncPipelineEngine::restoreFromStorage() {
    CalibrationData saved = m_studyTracker.getSavedCalibration();
    {
        std::lock_guard<std::mutex> lock(m_analysisMutex);
        if (saved.isCalibrated) {
            m_baseline.restoreCalibration(saved);
            m_extractor.setEyeClosedThreshold(m_baseline.getCurrentThreshold());
        }
    }

    std::ifstream in(m_fsmStatePath);
    if (in.is_open()) {
        std::stringstream buffer;
        buffer << in.rdbuf();
        m_stateMachine.restore(buffer.str(), epochNowSec());
    }
}

void AsyncPipelineEngine::persistStateMachine() {
    std::ofstream out(m_fsmStatePath, std::ios::trunc);
    if (out.is_open()) {
        out << m_stateMachine.serialize(epochNowSec());
    }
}

bool AsyncPipelineEngine::start() {
    if (m_isRunning.load()) {
        return true;
    }

    m_isRunning.store(true);
    m_isPaused.store(false);
    m_processedFrameCount.store(0);
    m_resetFrameTiming.store(true);

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

    // 啟動時立即檢查是否有錯過的同步 (補傳)
    checkAndTriggerNightlySync();
    return true;
}

void AsyncPipelineEngine::pause() {
    if (m_isRunning.load() && !m_isPaused.load()) {
        m_isPaused.store(true);
        if (m_camera) {
            m_camera->stop();
        }
        persistStateMachine();
        m_database.flush();
        m_lifecycle.transitionTo(AppLifecycleState::Suspended, "Engine paused by system/user");
    }
}

void AsyncPipelineEngine::resume() {
    if (m_isRunning.load() && m_isPaused.load()) {
        m_resetFrameTiming.store(true);
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

        persistStateMachine();
        m_database.flush();
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

void AsyncPipelineEngine::beginCalibration() {
    std::lock_guard<std::mutex> lock(m_calibrationMutex);
    m_isCalibrating = true;
    m_calibrationSamples.clear();
    m_calibrationTotalFrames = 0;
    // 睜眼暗部基準由推論執行緒持有，透過旗標請它在下一幀重置
    m_requestEyeBaselineReset.store(true);
}

bool AsyncPipelineEngine::finishCalibration() {
    std::vector<float> samples;
    size_t totalFrames = 0;
    {
        std::lock_guard<std::mutex> lock(m_calibrationMutex);
        m_isCalibrating = false;
        samples.swap(m_calibrationSamples);
        totalFrames = m_calibrationTotalFrames;
        m_lastCalibrationValidRatio = (totalFrames > 0)
            ? static_cast<float>(samples.size()) / static_cast<float>(totalFrames) : 0.0f;
        if (samples.size() < kMinCalibrationSamples || m_lastCalibrationValidRatio < kMinCalibrationValidRatio) {
            std::cout << "[校準] 有效人臉幀不足 (" << samples.size() << "/" << totalFrames
                      << ")，保留原基準，請使用者重新校準\n";
            return false;
        }
    }

    CalibrationData data;
    {
        std::lock_guard<std::mutex> lock(m_analysisMutex);
        m_baseline.calibrate(samples);
        m_extractor.reset(); // 校準前的窗口資料以舊閾值判定，不沿用
        m_extractor.setEyeClosedThreshold(m_baseline.getCurrentThreshold());
        data = m_baseline.getCalibrationData();
    }
    m_studyTracker.markInitialSetupCompleted(data);
    std::cout << "[校準] 完成：基準 EAR=" << data.baselineEar << "，閉眼閾值=" << data.thresholdEar
              << "，有效幀 " << samples.size() << "/" << totalFrames << "\n";
    return true;
}

bool AsyncPipelineEngine::isCalibrating() const {
    std::lock_guard<std::mutex> lock(m_calibrationMutex);
    return m_isCalibrating;
}

float AsyncPipelineEngine::getLastCalibrationValidRatio() const {
    std::lock_guard<std::mutex> lock(m_calibrationMutex);
    return m_lastCalibrationValidRatio;
}

bool AsyncPipelineEngine::hasSavedCalibration() const {
    return m_studyTracker.getSavedCalibration().isCalibrated;
}

void AsyncPipelineEngine::setSimulatedEyeOpenness(float openness) {
    if (m_landmarker) {
        m_landmarker->setSimulatedEyeOpenness(openness);
    }
}

bool AsyncPipelineEngine::triggerManualDataSync() {
    m_database.flush();
    return m_syncWorker.triggerDailySync(
        m_studyTracker.getSubjectUuid(),
        m_studyTracker.getCurrentDay(),
        nullptr
    );
}

void AsyncPipelineEngine::setAutoNightlySyncEnabled(bool enabled) {
    m_autoNightlySync.store(enabled);
}

bool AsyncPipelineEngine::checkAndTriggerNightlySync() {
    if (!m_autoNightlySync.load() || m_syncInFlight.load()) {
        return false;
    }
    if (steadyNowMs() < m_nextSyncRetrySteadyMs.load()) {
        return false; // 上次失敗，等待重試時間
    }

    std::time_t nowT = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm tmNow = localTimeOf(nowT);
    const int todayDate = toDateInt(tmNow);
    const int yesterdayDate = toDateInt(localTimeOf(nowT - 24 * 60 * 60));
    const int lastSynced = m_studyTracker.getLastSyncedDate();

    std::tm tmMidnight = tmNow;
    tmMidnight.tm_hour = 0;
    tmMidnight.tm_min = 0;
    tmMidnight.tm_sec = 0;
    const int64_t todayMidnightMs = static_cast<int64_t>(std::mktime(&tmMidnight)) * 1000;

    int markDate = 0;
    if (tmNow.tm_hour >= 21 && lastSynced < todayDate) {
        markDate = todayDate;                     // 每晚 21:00 例行同步
    } else if (lastSynced < yesterdayDate) {
        // 補傳：昨天 (含) 以前的資料尚未成功上傳 (例如 21:00 時 App 未開啟)
        int64_t oldest = m_database.getOldestRecordTimestampMs();
        if (oldest > 0 && oldest < todayMidnightMs) {
            markDate = yesterdayDate;
        }
    }
    if (markDate == 0) {
        return false;
    }

    bool expected = false;
    if (!m_syncInFlight.compare_exchange_strong(expected, true)) {
        return false;
    }

    std::cout << "[科研同步] 觸發上傳 (標記日期 " << markDate << "，實驗第 "
              << m_studyTracker.getCurrentDay() << " 天)\n";
    m_database.flush();
    bool queued = m_syncWorker.triggerDailySync(
        m_studyTracker.getSubjectUuid(),
        m_studyTracker.getCurrentDay(),
        [this, markDate](const SyncResult& result) {
            if (result.success) {
                // 上傳成功才標記，失敗不會被誤記為已同步
                m_studyTracker.setLastSyncedDate(markDate);
                m_nextSyncRetrySteadyMs.store(0);
            } else {
                m_nextSyncRetrySteadyMs.store(steadyNowMs() + kSyncRetryDelayMs);
            }
            m_syncInFlight.store(false);
        });
    if (!queued) {
        m_syncInFlight.store(false);
        return false;
    }
    return true;
}

void AsyncPipelineEngine::handleLifecycleEvent(const LifecycleEvent& event) {
    if (event.currentState != AppLifecycleState::Resumed) {
        return;
    }
    {
        // 熱重啟快速基準再校準 (以最近 1 秒樣本重同步光線與姿勢)
        std::lock_guard<std::mutex> lock(m_analysisMutex);
        std::vector<float> resumeSamples = m_extractor.getEarHistory();
        if (resumeSamples.size() > 30) {
            resumeSamples.erase(resumeSamples.begin(), resumeSamples.begin() + static_cast<std::ptrdiff_t>(resumeSamples.size() - 30));
        }
        if (!resumeSamples.empty()) {
            m_baseline.fastRecalibrate(resumeSamples);
            m_extractor.setEyeClosedThreshold(m_baseline.getCurrentThreshold());
        }
    }

    // 背景期間無法觀察使用者：視同離座，滿 5 分鐘即清除冷卻
    if (event.elapsedBackgroundSeconds > 0) {
        m_stateMachine.update(false, 0.0f, 0.0f, 0.0f, static_cast<float>(event.elapsedBackgroundSeconds));
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

        if (m_requestEyeBaselineReset.exchange(false)) {
            m_landmarker->resetEyeBaseline();
        }

        // 臉部定位與眼部開合推論
        LandmarkDetectionResult detection = m_landmarker->detect(frame);

        // 將結果遞交至 Thread 3 佇列
        {
            std::lock_guard<std::mutex> lock(m_inferenceQueueMutex);
            while (m_inferenceQueue.size() >= MAX_INFERENCE_QUEUE) {
                m_inferenceQueue.pop();
            }
            m_inferenceQueue.push(InferencePackage{std::move(frame), std::move(detection), m_landmarker->getLatestPresenceDiagnostic()});
            m_inferenceQueueCv.notify_one();
        }
    }
}

void AsyncPipelineEngine::updateComplexityUnlocked() {
    // 依時間重新取樣為 10 Hz，消除各平台幀率差異
    std::vector<float> signal = m_extractor.getEarHistoryResampled(kComplexityResampleHz);
    if (static_cast<float>(signal.size()) < kComplexityMinWindowSec * kComplexityResampleHz) {
        m_cachedComplexity = ComplexityMetrics{};
        m_cachedComplexity.complexityIndex = kComplexityPriorCi;
        return;
    }

    // EMD 去除最高頻 IMF (感測雜訊) 後再計算多尺度熵
    EmdResult emd = m_emdCalculator.decompose(signal);
    std::vector<float> denoised = signal;
    if (!emd.imfs.empty() && emd.imfs.front().size() == signal.size()) {
        const std::vector<float>& noiseImf = emd.imfs.front();
        for (size_t i = 0; i < denoised.size(); ++i) {
            denoised[i] -= noiseImf[i];
        }
    }
    ComplexityMetrics metrics = m_mseCalculator.calculateComplexity(denoised);
    metrics.emdEnergyRatio = emd.highToLowEnergyRatio;
    metrics.imfCount = static_cast<int>(emd.imfs.size());
    m_cachedComplexity = metrics;
}

// -----------------------------------------------------------------------------
// Thread 3: 訊號分析與狀態機推進迴圈 (Signal & State Processing Worker Loop)
// -----------------------------------------------------------------------------
void AsyncPipelineEngine::signalProcessingWorkerLoop() {
    auto lastFrameTime = std::chrono::steady_clock::now();

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

        // 真實經過時間：啟動或暫停恢復後的第一幀以實測幀率估算，避免把暫停時間算進樣本
        auto nowFrameTime = std::chrono::steady_clock::now();
        float deltaSec = std::chrono::duration<float>(nowFrameTime - lastFrameTime).count();
        lastFrameTime = nowFrameTime;
        if (m_resetFrameTiming.exchange(false)) {
            deltaSec = 1.0f / m_measuredFps;
        }
        deltaSec = std::clamp(deltaSec, 0.001f, 2.0f);
        // 實測處理幀率 (每個樣本代表的時間長度)，手機實際常只有 10~15 FPS
        m_measuredFps = std::clamp(m_measuredFps * 0.9f + (1.0f / deltaSec) * 0.1f, 5.0f, 60.0f);

        const bool hasFace = package.detection.hasFace;
        EyeMetrics eyeMetrics;
        float currentThreshold = 0.0f;
        ComplexityMetrics complexity;
        {
            std::lock_guard<std::mutex> lock(m_analysisMutex);

            // 1. 幾何特徵 (EAR / PERCLOS / 眨眼率)：僅在偵測到正對鏡頭人臉時計算
            if (hasFace) {
                m_absentSeconds = 0.0f;
                eyeMetrics = m_extractor.processFrame(package.detection.landmarks, m_measuredFps);
            } else {
                // 人臉未偵測到或離座：重置暫態計數器，幾何指標全部歸零，杜絕假疲勞數據
                m_extractor.resetTemporaryCounters();
                m_absentSeconds += deltaSec;
                if (m_absentSeconds >= kAbsentHistoryResetSec) {
                    m_extractor.reset();
                }
            }

            // 2. 動態滑動窗口基準自適應更新 (未偵測到人臉時不污染基準)
            currentThreshold = hasFace
                ? m_baseline.update(eyeMetrics.earAvg, eyeMetrics.isEyeClosed)
                : m_baseline.getCurrentThreshold();
            m_extractor.setEyeClosedThreshold(currentThreshold);

            // 3. 非線性複雜度分析 (EMD / MSE / CI)，每秒更新一次
            if (hasFace) {
                m_complexityTimer += deltaSec;
                if (m_complexityTimer >= 1.0f) {
                    m_complexityTimer = 0.0f;
                    updateComplexityUnlocked();
                }
            } else {
                m_complexityTimer = 0.0f;
                m_cachedComplexity.complexityIndex = 0.0f;
            }
            complexity = m_cachedComplexity;
        }

        // 3.5 校準期間收集清醒睜眼樣本
        {
            std::lock_guard<std::mutex> lock(m_calibrationMutex);
            if (m_isCalibrating) {
                m_calibrationTotalFrames++;
                if (hasFace) {
                    m_calibrationSamples.push_back(eyeMetrics.earAvg);
                }
            }
        }

        // 4. 20/5/5 防打擾狀態機推進
        SystemState state = m_stateMachine.update(
            hasFace,
            eyeMetrics.perclos,
            eyeMetrics.blinkRatePerMin,
            complexity.complexityIndex,
            deltaSec
        );
        int64_t count = ++m_processedFrameCount;

        // 5. 每秒落盤一筆 (僅在人臉在座時記錄有效特徵)
        //    模擬相機 (實體鏡頭無畫面時的備援) 的影格不是真實受試者資料，不寫入研究資料庫
        m_recordTimer += deltaSec;
        if (m_recordTimer >= 1.0f) {
            m_recordTimer = 0.0f;
            if (hasFace && !package.frame.isSynthetic) {
                FatigueRecord record;
                record.timestampMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count();
                record.subjectUuid = m_studyTracker.getSubjectUuid();
                record.ear = eyeMetrics.earAvg;
                record.perclos = eyeMetrics.perclos;
                record.complexityIndex = complexity.complexityIndex;
                record.fatigueScore = state.smoothedFatigueScore;
                record.alertLevel = state.fatigueLevel;
                m_database.logRecord(record);
            }
        }

        // 5.1 每 10 秒保存冷卻狀態；每 30 秒檢查夜間同步/補傳 (非阻塞)
        m_persistTimer += deltaSec;
        if (m_persistTimer >= 10.0f) {
            m_persistTimer = 0.0f;
            persistStateMachine();
        }
        m_syncCheckTimer += deltaSec;
        if (m_syncCheckTimer >= 30.0f) {
            m_syncCheckTimer = 0.0f;
            checkAndTriggerNightlySync();
        }

        // 6. 分發遙測至 UI Thread
        if (m_telemetryCallback) {
            EngineTelemetry telemetry;
            telemetry.latestFrame = std::move(package.frame);
            telemetry.detection = std::move(package.detection);
            telemetry.presenceDiagnostic = std::move(package.presenceDiagnostic);
            telemetry.eyeMetrics = eyeMetrics;
            telemetry.complexityMetrics = complexity;
            telemetry.systemState = state;
            telemetry.currentThreshold = currentThreshold;
            telemetry.measuredFps = m_measuredFps;
            telemetry.totalFramesProcessed = count;
            telemetry.currentStudyDay = m_studyTracker.getCurrentDay();
            telemetry.studyStatus = m_studyTracker.getStatus();
            telemetry.subjectUuid = m_studyTracker.getSubjectUuid();

            std::string driverName = m_camera->getActiveDriverName();
            bool isSynthetic = m_camera->isUsingSyntheticFallback();
            std::string statusLabel = isSynthetic ? "模擬測試相機 (Synthetic)" : driverName;
            std::ostringstream summary;
            summary << "相機狀態: " << statusLabel << " | 實測 " << static_cast<int>(m_measuredFps + 0.5f)
                    << " FPS (累計 " << count << " 幀)";
            telemetry.lifecycleSummary = summary.str();

            m_telemetryCallback(telemetry);
        }
    }
}

} // namespace efd
