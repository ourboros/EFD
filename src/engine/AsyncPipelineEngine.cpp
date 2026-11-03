#include "AsyncPipelineEngine.hpp"
#include "vision/FaceLandmarker.hpp"
#include "efd/version.hpp"
#include <chrono>
#include <algorithm>
#include <ctime>
#include <fstream>
#include <iostream>
#include <numeric>
#include <sstream>

namespace efd {

namespace {

constexpr float kComplexityResampleHz = 10.0f;
constexpr float kComplexityMinWindowSec = 30.0f;
constexpr float kComplexityPriorCi = 2.0f;      // 資料不足時的清醒先驗值 (不計複雜度分)
// MSE 容許誤差 r = 0.15 x 個人睜眼 EAR 動態範圍 (睜眼基準 - 閉眼下限)，各窗口用同一把尺
constexpr float kComplexityToleranceOfRange = 0.15f;
// 連續 10 秒沒偵測到臉時每 500 ms 才偵測一次 (與 Android 相同)：A71 實測無臉時全速偵測佔一顆核心約 98%
constexpr int64_t kNoFaceSlowdownAfterMs = 10000;
constexpr int64_t kNoFaceDetectIntervalMs = 500;
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
    m_platformName = (platform == PlatformType::Android) ? "Android" : "Windows";

    // 交叉設計：不提醒的那一週仍照常判定與冷卻，只是不把提醒交給介面 (記錄為「未顯示」)
    m_stateMachine.setAlertCallback([this](FatigueLevel level, float score, const std::string& msg) {
        const bool enabled = m_studyTracker.areAlertsEnabledToday();
        m_minute.alert = enabled ? "shown" : "suppressed";
        if (!enabled) {
            std::cout << "[交叉設計] 本週只收數據：提醒未顯示 (分數 " << score << ")\n";
            return;
        }
        AlertCallback callback;
        {
            std::lock_guard<std::mutex> lock(m_alertCallbackMutex);
            callback = m_platformAlertCallback;
        }
        if (callback) callback(level, score, msg);
    });

    // 每日上傳附上最近 3 天的每分鐘摘要與提醒自評 (Apps Script 會略過已寫入的時間點)
    m_syncWorker.setExtraPayloadProvider([this](const std::string& subjectId) {
        return buildStudyLogJson(subjectId);
    });

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
    m_studyLog.setDirectory(directory);
    restoreFromStorage();
}

void AsyncPipelineEngine::restoreFromStorage() {
    CalibrationData saved = m_studyTracker.getSavedCalibration();
    {
        std::lock_guard<std::mutex> lock(m_analysisMutex);
        if (saved.isCalibrated) {
            m_baseline.restoreCalibration(saved);
            m_extractor.setEyeClosedThreshold(m_baseline.getCurrentThreshold());
            m_rollingBaseline.setInitialOpenEar(saved.baselineEar);
        }
    }
    m_stateMachine.setSensitivity(m_studyTracker.getSensitivityLevel());

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
    m_cameraSuspended.store(false);

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
        // 冷卻中關閉的相機維持關閉，冷卻結束時由訊號執行緒開啟
        if (m_camera && !m_cameraSuspended.load() && !m_externalCameraControl.load()) {
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
        // 訊號執行緒已結束：寫出最後一個未滿的分鐘
        flushMinute();

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
    std::lock_guard<std::mutex> lock(m_alertCallbackMutex);
    m_platformAlertCallback = std::move(callback);
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
        const float meanEar = std::accumulate(samples.begin(), samples.end(), 0.0f) / static_cast<float>(samples.size());
        if (meanEar < kMinCalibrationMeanEar) {
            std::cout << "[校準] 睜眼平均 EAR 過低 (" << meanEar << ")，眼睛開合未被量到，保留原基準\n";
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
        // 模型偵測器的滾動睜眼基準：校準時 (環境、姿勢可能改變) 重新累積，累積不足前以校準值為準
        m_rollingBaseline.reset();
        m_rollingBaseline.setInitialOpenEar(data.baselineEar);
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

void AsyncPipelineEngine::getCalibrationProgress(size_t& validSamples, size_t& totalFrames) const {
    std::lock_guard<std::mutex> lock(m_calibrationMutex);
    validSamples = m_calibrationSamples.size();
    totalFrames = m_calibrationTotalFrames;
}

bool AsyncPipelineEngine::hasSavedCalibration() const {
    const CalibrationData saved = m_studyTracker.getSavedCalibration();
    // 門檻過低的舊校準不會被沿用 (AdaptiveBaseline::restoreCalibration)，視同尚未校準
    return saved.isCalibrated && saved.thresholdEar > AdaptiveBaseline::kMinUsableThreshold;
}

void AsyncPipelineEngine::setSensitivity(int level) {
    m_stateMachine.setSensitivity(level);
    m_studyTracker.setSensitivityLevel(m_stateMachine.getSensitivity());
}

int AsyncPipelineEngine::getSensitivity() const {
    return m_stateMachine.getSensitivity();
}

void AsyncPipelineEngine::setSimulatedEyeOpenness(float openness) {
    if (m_landmarker) {
        m_landmarker->setSimulatedEyeOpenness(openness);
    }
}

bool AsyncPipelineEngine::setLandmarkDetector(std::unique_ptr<ILandmarkDetector> detector) {
    // 執行中更換會與 Thread 2 競爭同一個偵測器
    if (m_isRunning.load() || !detector || !detector->isReady()) {
        return false;
    }
    m_landmarker = std::move(detector);
    std::cout << "[Vision] 臉部特徵點偵測器: " << m_landmarker->name() << "\n";
    return true;
}

void AsyncPipelineEngine::submitExternalDetection(RawFrame frameInfo, LandmarkDetectionResult detection,
                                                  PresenceDiagnostic diagnostic) {
    if (!m_isRunning.load() || m_isPaused.load() || m_cameraSuspended.load()) return;
    m_externalDetectorUsed.store(true);

    std::lock_guard<std::mutex> lock(m_inferenceQueueMutex);
    while (m_inferenceQueue.size() >= MAX_INFERENCE_QUEUE) {
        m_inferenceQueue.pop();
    }
    m_inferenceQueue.push(InferencePackage{std::move(frameInfo), std::move(detection), std::move(diagnostic)});
    m_inferenceQueueCv.notify_one();
}

bool AsyncPipelineEngine::triggerManualDataSync(NetworkSyncWorker::SyncCallback callback) {
    m_database.flush();
    return m_syncWorker.triggerDailySync(
        m_studyTracker.getSubjectUuid(),
        m_studyTracker.getCurrentDay(),
        std::move(callback),
        StudyWorkflowTracker::statusLabel(m_studyTracker.getStatus()),
        m_studyTracker.getUnlockToken()
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
        },
        StudyWorkflowTracker::statusLabel(m_studyTracker.getStatus()),
        m_studyTracker.getUnlockToken());
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

        const int64_t nowMs = steadyNowMs();
        if (m_lastFaceSeenMs == 0) m_lastFaceSeenMs = nowMs;
        if (!isCalibrating() && nowMs - m_lastFaceSeenMs > kNoFaceSlowdownAfterMs &&
            nowMs - m_lastDetectMs < kNoFaceDetectIntervalMs) {
            continue;
        }
        m_lastDetectMs = nowMs;

        // 臉部定位與眼部開合推論
        LandmarkDetectionResult detection = m_landmarker->detect(frame);
        if (detection.hasFace) m_lastFaceSeenMs = nowMs;

        // 模型偵測器 (ONNX) 沒有內建遺失緩衝：臉部短暫遺失未滿 1 秒的幀不送出，滿 1 秒才判定離座
        // (啟發式偵測器自行處理，Android MediaPipe 在 JNI 處理)
        if (detection.landmarksMeasured && !m_modelPresenceFilter.shouldSubmit(detection.hasFace, steadyNowMs())) {
            continue;
        }

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
    const float openRange = std::max(0.05f, m_rollingBaseline.openEar() - RollingOpenBaseline::kClosedEarFloor);
    ComplexityMetrics metrics = m_mseCalculator.calculateComplexity(denoised, kComplexityToleranceOfRange * openRange);
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
        bool hasPackage = false;
        {
            // 相機因冷卻關閉時沒有影格，每 0.5 秒醒來推進冷卻倒數
            std::unique_lock<std::mutex> lock(m_inferenceQueueMutex);
            m_inferenceQueueCv.wait_for(lock, std::chrono::milliseconds(500), [this]() {
                return !m_inferenceQueue.empty() || !m_isRunning.load();
            });

            if (!m_isRunning.load()) break;

            if (!m_inferenceQueue.empty()) {
                package = std::move(m_inferenceQueue.front());
                m_inferenceQueue.pop();
                hasPackage = true;
            }
        }

        if (m_cameraSuspended.load()) {
            // 關閉相機前已在佇列中的幀不再分析
            if (!hasPackage && !m_isPaused.load()) {
                tickWhileCameraSuspended();
            }
            continue;
        }
        if (!hasPackage) continue;

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

            // 2. 閉眼門檻更新 (未偵測到人臉時不污染基準)
            if (package.detection.landmarksMeasured) {
                // 模型偵測器：PERCLOS 用 P80 (閉合 >= 80%)、眨眼用閉合 >= 50%，以最近 2 分鐘的睜眼 EAR 為準
                if (hasFace) m_rollingBaseline.add(eyeMetrics.earAvg, deltaSec);
                currentThreshold = m_rollingBaseline.perclosThreshold();
                m_lastBlinkThreshold = m_rollingBaseline.blinkThreshold();
                m_extractor.setBlinkThreshold(m_lastBlinkThreshold);
            } else {
                currentThreshold = hasFace
                    ? m_baseline.update(eyeMetrics.earAvg, eyeMetrics.isEyeClosed)
                    : m_baseline.getCurrentThreshold();
                m_lastBlinkThreshold = currentThreshold;
                m_extractor.setBlinkThreshold(-1.0f);
            }
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
            // 開始校準到第一次偵測到臉之間 (使用者還在對準、手機還在判斷畫面方向) 不列入分母
            if (m_isCalibrating && (hasFace || !m_calibrationSamples.empty())) {
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
            deltaSec,
            eyeMetrics.windowBlinkDurationMs,
            eyeMetrics.longClosuresPerMin
        );
        accumulateMinute(true, hasFace, eyeMetrics, complexity.complexityIndex, state, deltaSec,
                         package.detection.landmarksMeasured);
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
                record.blinkRatePerMin = eyeMetrics.blinkRatePerMin;
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

        // 5.2 相機省電：提醒後的冷卻、快篩的非採樣時段關閉相機，需要採樣時再開啟
        if (m_cameraPowerSaving.load() && !state.shouldCameraRun && !isCalibrating()) {
            suspendCamera();
        }

        // 6. 分發遙測至 UI Thread
        publishTelemetry(package, eyeMetrics, complexity, state, currentThreshold, count);
    }
}

void AsyncPipelineEngine::publishTelemetry(InferencePackage& package, const EyeMetrics& eyeMetrics,
                                           const ComplexityMetrics& complexity, const SystemState& state,
                                           float currentThreshold, int64_t count) {
    if (!m_telemetryCallback) return;

    EngineTelemetry telemetry;
    telemetry.latestFrame = std::move(package.frame);
    telemetry.detection = std::move(package.detection);
    telemetry.presenceDiagnostic = std::move(package.presenceDiagnostic);
    telemetry.eyeMetrics = eyeMetrics;
    telemetry.complexityMetrics = complexity;
    telemetry.systemState = state;
    telemetry.currentThreshold = currentThreshold;
    telemetry.blinkThreshold = m_lastBlinkThreshold;
    telemetry.cameraSuspended = m_cameraSuspended.load();
    telemetry.measuredFps = m_measuredFps;
    telemetry.totalFramesProcessed = count;
    telemetry.currentStudyDay = m_studyTracker.getCurrentDay();
    telemetry.studyStatus = m_studyTracker.getStatus();
    telemetry.subjectUuid = m_studyTracker.getSubjectUuid();

    std::string statusLabel;
    if (telemetry.cameraSuspended) {
        statusLabel = "冷卻中已關閉";
    } else {
        statusLabel = m_camera->isUsingSyntheticFallback() ? "模擬測試相機 (Synthetic)" : m_camera->getActiveDriverName();
    }
    std::ostringstream summary;
    summary << "相機狀態: " << statusLabel << " | 實測 " << static_cast<int>(m_measuredFps + 0.5f)
            << " FPS (累計 " << count << " 幀)";
    telemetry.lifecycleSummary = summary.str();

    m_telemetryCallback(telemetry);
}

void AsyncPipelineEngine::setCameraPowerSaving(bool enabled) {
    // 關閉此功能時，若相機正處於關閉狀態，由訊號執行緒在下一次倒數時重新開啟
    m_cameraPowerSaving.store(enabled);
}

bool AsyncPipelineEngine::isCameraSuspended() const {
    return m_cameraSuspended.load();
}

void AsyncPipelineEngine::suspendCamera() {
    if (m_cameraSuspended.exchange(true)) return;
    m_lastSuspendedTick = std::chrono::steady_clock::now();
    if (m_camera) {
        m_camera->stop();
    }
    {
        // 關閉前已送出的影格不再分析
        std::lock_guard<std::mutex> lock(m_frameQueueMutex);
        while (!m_frameQueue.empty()) m_frameQueue.pop();
    }
    std::cout << "[省電] 冷卻或快篩非採樣時段，關閉相機\n";
}

void AsyncPipelineEngine::resumeCamera() {
    if (!m_cameraSuspended.load()) return;
    {
        // 關閉前的眼動歷史已過時 (至少相隔數分鐘)，重新累積窗口
        std::lock_guard<std::mutex> lock(m_analysisMutex);
        m_extractor.reset();
        m_absentSeconds = 0.0f;
        m_complexityTimer = 0.0f;
    }
    m_resetFrameTiming.store(true);
    m_cameraSuspended.store(false);
    if (m_camera && !m_isPaused.load() && !m_externalCameraControl.load()) {
        m_camera->start();
    }
    std::cout << "[省電] 需要採樣，重新開啟相機\n";
}

void AsyncPipelineEngine::accumulateMinute(bool cameraOn, bool hasFace, const EyeMetrics& eye, float ci,
                                           const SystemState& state, float dt, bool measuredDetector) {
    const int64_t nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    const int64_t minuteStart = nowMs - nowMs % 60000;
    if (m_minute.minuteStartMs != 0 && m_minute.minuteStartMs != minuteStart) {
        flushMinute();
    }
    if (m_minute.minuteStartMs == 0) m_minute.minuteStartMs = minuteStart;

    if (cameraOn) m_minute.cameraOnSec += dt;
    if (hasFace) {
        m_minute.presentSec += dt;
        m_minute.ears.push_back(eye.earAvg);
        m_minute.perclos = eye.perclos;
        m_minute.ci = ci;
        m_minute.longClosures = eye.longClosuresPerMin;
        // 眨眼次數與總時長為累計值 (離座時會被重置)：取本分鐘的增量
        const float durTotal = eye.avgBlinkDurationMs * static_cast<float>(eye.blinkCount);
        if (eye.blinkCount >= m_lastBlinkCount) {
            m_minute.blinks += eye.blinkCount - m_lastBlinkCount;
            m_minute.blinkDurMs += std::max(0.0f, durTotal - m_lastBlinkDurTotal);
        } else {
            m_minute.blinks += eye.blinkCount;
            m_minute.blinkDurMs += durTotal;
        }
        m_lastBlinkCount = eye.blinkCount;
        m_lastBlinkDurTotal = durTotal;
        m_minute.detector = measuredDetector ? (m_externalDetectorUsed.load() ? "mediapipe" : m_landmarker->name())
                                             : "heuristic";
    }
    m_minute.score = state.smoothedFatigueScore;
    m_minute.cooldownState = static_cast<int>(state.cooldownState);
}

void AsyncPipelineEngine::flushMinute() {
    if (m_minute.minuteStartMs == 0) return;
    if (m_minute.cameraOnSec > 0.0f || m_minute.presentSec > 0.0f || m_minute.alert != "none" ||
        m_minute.cooldownState != 0) {
        MinuteSummary m;
        m.minuteMs = m_minute.minuteStartMs;
        m.studyDay = m_studyTracker.getCurrentDay();
        m.group = m_studyTracker.getStudyGroup();
        m.alertsEnabled = StudyWorkflowTracker::alertsEnabledFor(m.group, m.studyDay);
        m.presentSec = m_minute.presentSec;
        m.cameraOnSec = m_minute.cameraOnSec;
        if (!m_minute.ears.empty()) {
            std::vector<float>& e = m_minute.ears;
            m.earMean = std::accumulate(e.begin(), e.end(), 0.0f) / static_cast<float>(e.size());
            const size_t k = e.size() / 10;
            std::nth_element(e.begin(), e.begin() + static_cast<std::ptrdiff_t>(k), e.end());
            m.earP10 = e[k];
        }
        m.perclos = m_minute.perclos;
        m.blinkCount = m_minute.blinks;
        m.blinkDurMs = (m_minute.blinks > 0) ? m_minute.blinkDurMs / static_cast<float>(m_minute.blinks) : 0.0f;
        m.ci = m_minute.ci;
        m.longClosures = m_minute.longClosures;
        m.score = m_minute.score;
        m.cooldownState = m_minute.cooldownState;
        m.alert = m_minute.alert;
        m.detector = m_minute.detector;
        m.platform = m_platformName;
        m.appVersion = EFD_VERSION_STRING;
        m_studyLog.appendMinute(m);
    }
    m_minute = MinuteAccumulator{};
}

void AsyncPipelineEngine::recordEyeFatigueResponse(int64_t alertTimeMs, int eyeFatigue, float responseSec) {
    SurveyResponse s;
    s.timeMs = alertTimeMs;
    s.studyDay = m_studyTracker.getCurrentDay();
    s.group = m_studyTracker.getStudyGroup();
    s.alertsEnabled = StudyWorkflowTracker::alertsEnabledFor(s.group, s.studyDay);
    s.eyeFatigue = (eyeFatigue >= 0 && eyeFatigue <= 10) ? eyeFatigue : -1;
    s.skipped = s.eyeFatigue < 0;
    s.responseSec = std::max(0.0f, responseSec);
    m_studyLog.appendSurvey(s);
}

std::string AsyncPipelineEngine::buildStudyLogJson(const std::string& subjectId) const {
    const int64_t nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    return m_studyLog.buildUploadJsonFields(subjectId, nowMs - kStudyLogUploadDays * 24LL * 3600 * 1000);
}

void AsyncPipelineEngine::setExternalCameraControl(bool external) {
    m_externalCameraControl.store(external);
}

void AsyncPipelineEngine::tickWhileCameraSuspended() {
    const auto now = std::chrono::steady_clock::now();
    // 正常間隔約 0.5 秒；較長的間隔是引擎暫停 (鎖屏、休眠)，該段時間已由恢復時的生命週期事件計入
    const float dt = std::clamp(std::chrono::duration<float>(now - m_lastSuspendedTick).count(), 0.0f, 1.0f);
    m_lastSuspendedTick = now;

    SystemState state = m_stateMachine.advanceWithoutObservation(dt);
    accumulateMinute(false, false, EyeMetrics{}, 0.0f, state, dt, false);
    // 使用者重新校準時需要畫面，即使仍在冷卻中也開啟相機
    if (!m_cameraPowerSaving.load() || state.shouldCameraRun || isCalibrating()) {
        resumeCamera();
    }

    m_persistTimer += dt;
    if (m_persistTimer >= 10.0f) {
        m_persistTimer = 0.0f;
        persistStateMachine();
    }

    InferencePackage idle;
    float threshold;
    ComplexityMetrics complexity;
    {
        std::lock_guard<std::mutex> lock(m_analysisMutex);
        threshold = m_baseline.getCurrentThreshold();
        complexity = m_cachedComplexity;
    }
    publishTelemetry(idle, EyeMetrics{}, complexity, state, threshold, m_processedFrameCount.load());
}

} // namespace efd
