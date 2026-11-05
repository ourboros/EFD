#pragma once

#include "efd/types.hpp"
#include "vision/RawFrame.hpp"
#include "vision/CameraService.hpp"
#include "vision/ILandmarkDetector.hpp"
#include "vision/ExternalLandmarks.hpp"
#include "analysis/FeatureExtractor.hpp"
#include "analysis/AdaptiveBaseline.hpp"
#include "analysis/RollingOpenBaseline.hpp"
#include "analysis/GazeTracker.hpp"
#include "analysis/PersonalTrendBaseline.hpp"
#include "analysis/EmdCalculator.hpp"
#include "analysis/MseCalculator.hpp"
#include "state/FatigueStateMachine.hpp"
#include "platform/PlatformLifecycleAdapter.hpp"
#include "storage/DatabaseService.hpp"
#include "storage/StudyDataLog.hpp"
#include "study/StudyWorkflowTracker.hpp"
#include "network/NetworkSyncWorker.hpp"

#include <memory>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <thread>
#include <atomic>
#include <functional>
#include <chrono>

namespace efd {

struct EngineTelemetry {
    RawFrame latestFrame;
    LandmarkDetectionResult detection;
    PresenceDiagnostic presenceDiagnostic;
    EyeMetrics eyeMetrics;
    ComplexityMetrics complexityMetrics;
    SystemState systemState;
    float currentThreshold = 0.21f;
    // 眨眼判斷閾值 (模型偵測器為閉合 50%；啟發式偵測器與 currentThreshold 相同)
    float blinkThreshold = 0.21f;
    // 冷卻期間相機已關閉 (此時沒有影像與偵測結果，只有冷卻倒數)
    bool cameraSuspended = false;
    float measuredFps = 30.0f;
    int64_t totalFramesProcessed = 0;
    std::string lifecycleSummary;
    int currentStudyDay = 1;
    StudyStatus studyStatus = StudyStatus::ActiveMonitoring;
    std::string subjectUuid;
};

class AsyncPipelineEngine {
public:
    using TelemetryCallback = std::function<void(const EngineTelemetry& telemetry)>;
    using AlertCallback = FatigueStateMachine::AlertCallback;

    // 校準有效幀比例門檻：校準期間至少 60% 的幀需偵測到人臉
    static constexpr float kMinCalibrationValidRatio = 0.60f;
    static constexpr size_t kMinCalibrationSamples = 20;
    // 睜眼時的平均 EAR 下限。MediaPipe 實測 (A71)：閉眼 0.03~0.06、低頭看手機 0.14~0.17、正視睜眼 0.21~0.31；
    // 校準期間平均低於此值代表沒有正視鏡頭或量測位置不在眼睛上，存下來的基準會讓閉眼門檻過低
    static constexpr float kMinCalibrationMeanEar = 0.15f;

    explicit AsyncPipelineEngine(PlatformType platform = PlatformType::Windows);
    ~AsyncPipelineEngine();

    // 設定資料目錄 (設定檔、資料庫、同步封裝、冷卻狀態)，須在 start() 之前呼叫
    // 未呼叫時沿用工作目錄 (Windows 桌面版的既有行為)
    void setDataDirectory(const std::string& directory, const std::string& configFileName = "efd_study_config.json");

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
    bool isCameraSleeping() const { return isCameraSuspended(); }

    // 註冊即時遙測回呼 (UI Thread 安全接收)
    void setTelemetryCallback(TelemetryCallback callback);

    // 註冊疲勞警報回呼
    void setAlertCallback(AlertCallback callback);

    // 兩段式校準：開始收集 -> (UI 引導使用者注視數秒) -> 結束並驗證
    // finishCalibration() 回傳 false 代表有效人臉幀不足，基準不會被更新
    void beginCalibration();
    bool finishCalibration();
    bool isCalibrating() const;
    float getLastCalibrationValidRatio() const;
    // 校準進行中的進度：有效人臉幀數與 (第一次偵測到臉之後的) 總幀數，供 UI 決定是否延長校準
    void getCalibrationProgress(size_t& validSamples, size_t& totalFrames) const;

    // 是否已有保存的個人化校準 (可直接進入監控畫面)
    bool hasSavedCalibration() const;

    // 提示靈敏度 (0 低 / 1 標準 / 2 高)：立即套用到狀態機並保存到設定檔
    void setSensitivity(int level);
    int getSensitivity() const;

    // 設定模擬眼睛開合狀態 (供測試與驗證)
    void setSimulatedEyeOpenness(float openness);

    // 更換臉部特徵點偵測器 (預設為啟發式 FaceLandmarker)，須在 start() 之前呼叫
    // 傳入的偵測器須已 initialize() 成功；傳入 nullptr 或未就緒時保留原偵測器並回傳 false
    bool setLandmarkDetector(std::unique_ptr<ILandmarkDetector> detector);

    // 由平台層 (Android MediaPipe) 自行完成偵測時，直接遞交結果給訊號執行緒，略過 Thread 2
    // frameInfo 只需填寫寬高與時間戳 (可不含像素)；暫停中或未啟動時忽略
    void submitExternalDetection(RawFrame frameInfo, LandmarkDetectionResult detection,
                                 PresenceDiagnostic diagnostic = {});

    // 相機省電 (預設關閉，由平台開啟)：依狀態機 shouldCameraRun 開關相機
    // - 提醒後 20 分鐘冷卻：相機關閉
    // - 冷卻後每輪 5 分鐘快篩：前 4 分鐘關閉，最後 1 分鐘開啟採樣
    // 相機關閉時看不到使用者：倒數照常進行，但不算離座 (不能當成已休息而清除冷卻)
    // 引擎擁有相機的平台 (Windows) 由引擎直接開關；Android 由前景服務依 isCameraSuspended() 開關 CameraX
    void setCameraPowerSaving(bool enabled);
    bool isCameraSuspended() const;

    // 離座探測 (預設關閉，需同時開啟相機省電)：相機開著卻連續 60 秒沒偵測到臉時，改為關 15 秒、開 4 秒輪流探測；
    // 關閉時段視為不在座 (照常累計離座時間)，探測到臉即恢復常態。
    // Android 開啟；Windows 不開啟 (視訊鏡頭指示燈會跟著一亮一滅，容易讓受試者誤會)。秒數參數供測試縮短時間
    void setAbsenceProbing(bool enabled, float afterNoFaceSec = 60.0f, float offSec = 15.0f, float onSec = 4.0f);

    // 提醒出現時的眼睛疲勞自評 (0~10；-1 = 略過或逾時)，alertTimeMs 為提醒出現的 epoch 毫秒
    void recordEyeFatigueResponse(int64_t alertTimeMs, int eyeFatigue, float responseSec);

    // 上傳 JSON 的額外欄位：最近 kStudyLogUploadDays 天的每分鐘摘要與提醒自評
    static constexpr int kStudyLogUploadDays = 3;
    std::string buildStudyLogJson(const std::string& subjectId) const;

    // 相機由平台層開關 (Android CameraX)：恢復時引擎不啟動 CameraService
    // (Android 的 CameraService 找不到原生相機會改用模擬相機，模擬畫面會混進快篩採樣)
    void setExternalCameraControl(bool external);

    // 執行手動觸發當日眼動數據落盤與雲端同步 (不標記為每晚同步完成)
    bool triggerManualDataSync(NetworkSyncWorker::SyncCallback callback = nullptr);

    // 檢查並執行夜間 21:00 定時自動同步與錯過日期的補傳
    // 上傳成功後才標記已同步；失敗 10 分鐘後重試。可從任何執行緒呼叫，不會阻塞
    bool checkAndTriggerNightlySync();

    // 由原生層負責同步的平台 (Android) 可關閉 C++ 內建夜間同步
    void setAutoNightlySyncEnabled(bool enabled);

    // 存取各核心模組
    PlatformLifecycleAdapter& getLifecycleAdapter() { return m_lifecycle; }
    FatigueStateMachine& getStateMachine() { return m_stateMachine; }
    AdaptiveBaseline& getAdaptiveBaseline() { return m_baseline; }
    FeatureExtractor& getFeatureExtractor() { return m_extractor; }
    CameraService& getCameraService() { return *m_camera; }
    ILandmarkDetector& getFaceLandmarker() { return *m_landmarker; }
    DatabaseService& getDatabaseService() { return m_database; }
    StudyWorkflowTracker& getStudyTracker() { return m_studyTracker; }
    NetworkSyncWorker& getSyncWorker() { return m_syncWorker; }

private:
    // 同步狀態需在 m_syncWorker 之前宣告：同步回呼在 m_syncWorker 解構 (join) 前仍可能存取
    std::atomic<bool> m_autoNightlySync{true};
    std::atomic<bool> m_syncInFlight{false};
    std::atomic<int64_t> m_nextSyncRetrySteadyMs{0};

    PlatformLifecycleAdapter m_lifecycle;
    DatabaseService m_database;
    StudyWorkflowTracker m_studyTracker;
    NetworkSyncWorker m_syncWorker;

    std::unique_ptr<CameraService>  m_camera;
    std::unique_ptr<ILandmarkDetector> m_landmarker;
    FeatureExtractor m_extractor;
    AdaptiveBaseline m_baseline;               // 啟發式偵測器的閉眼門檻
    RollingOpenBaseline m_rollingBaseline;     // 模型偵測器的閉眼門檻 (PERCLOS P80)
    float m_lastBlinkThreshold = 0.21f;        // 只在訊號執行緒使用
    GazeTracker m_gaze;                        // 眼球移動 (虹膜相對眼角)
    GazeTracker::Metrics m_cachedGaze;         // 每秒更新一次
    // 個人基準 (最近 2 小時在座分鐘的第 75 百分位)：眼皮下垂與眼球移動都和自己平常比
    PersonalTrendBaseline m_openEarTrend;
    PersonalTrendBaseline m_gazeSpeedTrend;
    PersonalTrendBaseline m_gazeShiftTrend;
    float m_trendTimer = 0.0f;
    EmdCalculator    m_emdCalculator;
    MseCalculator    m_mseCalculator;
    FatigueStateMachine m_stateMachine;

    std::string m_fsmStatePath = "efd_fsm_state.txt";

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
        PresenceDiagnostic presenceDiagnostic;
    };
    std::queue<InferencePackage> m_inferenceQueue;
    std::mutex m_inferenceQueueMutex;
    std::condition_variable m_inferenceQueueCv;
    static constexpr size_t MAX_INFERENCE_QUEUE = 3;

    // 保護特徵、基準與複雜度 (訊號執行緒與生命週期/校準呼叫端共用)
    std::mutex m_analysisMutex;

    // 校準收集
    mutable std::mutex m_calibrationMutex;
    bool m_isCalibrating = false;
    std::vector<float> m_calibrationSamples;
    size_t m_calibrationTotalFrames = 0;   // 第一次偵測到臉之後才開始累計
    float m_lastCalibrationValidRatio = 0.0f;
    std::atomic<bool> m_requestEyeBaselineReset{false};
    ExternalPresenceFilter m_modelPresenceFilter;   // 只在推論執行緒 (Thread 2) 使用
    int64_t m_lastFaceSeenMs = 0;                   // 只在推論執行緒使用：無臉降頻
    int64_t m_lastDetectMs = 0;

    TelemetryCallback m_telemetryCallback;
    std::atomic<int64_t> m_processedFrameCount{0};
    ComplexityMetrics m_cachedComplexity;
    std::atomic<bool> m_resetFrameTiming{true};
    float m_measuredFps = 30.0f;
    float m_absentSeconds = 0.0f;
    float m_complexityTimer = 0.0f;
    float m_recordTimer = 0.0f;
    float m_persistTimer = 0.0f;
    float m_syncCheckTimer = 0.0f;

    // 相機省電 (只由訊號執行緒切換相機狀態)
    std::atomic<bool> m_cameraPowerSaving{false};
    std::atomic<bool> m_externalCameraControl{false};
    std::atomic<bool> m_externalDetectorUsed{false};   // 曾收到平台層 (Android MediaPipe) 的偵測結果

    // 提醒：狀態機 -> 交叉設計判斷 (本週是否提醒) -> 平台介面
    std::mutex m_alertCallbackMutex;
    AlertCallback m_platformAlertCallback;

    // 每分鐘摘要累加 (只在訊號執行緒使用)
    struct MinuteAccumulator {
        int64_t minuteStartMs = 0;
        float presentSec = 0.0f;
        float cameraOnSec = 0.0f;
        std::vector<float> ears;
        float perclos = 0.0f;
        int blinks = 0;
        float blinkDurMs = 0.0f;
        float ci = 0.0f;
        float longClosures = 0.0f;
        float openEar = 0.0f;
        float gazeSpeed = 0.0f;
        float gazeShifts = 0.0f;
        float score = 0.0f;
        int cooldownState = 0;
        std::string alert = "none";
        std::string detector = "heuristic";
    };
    MinuteAccumulator m_minute;
    int m_lastBlinkCount = 0;
    float m_lastBlinkDurTotal = 0.0f;
    std::string m_platformName = "Windows";
    StudyDataLog m_studyLog;
    std::atomic<bool> m_cameraSuspended{false};
    std::chrono::steady_clock::time_point m_lastSuspendedTick{};
    std::atomic<bool> m_absenceProbing{false};
    bool m_probeOff = false;          // 以下只在訊號執行緒使用：離座探測的關閉時段
    float m_probeOffSec = 0.0f;
    float m_probeAfterNoFaceSec = 60.0f;
    float m_probeOffDurationSec = 15.0f;
    float m_probeOnDurationSec = 4.0f;
    float m_noFaceCameraSec = 0.0f;   // 相機開著、連續沒偵測到臉的秒數

    void inferenceWorkerLoop();
    void signalProcessingWorkerLoop();
    void handleLifecycleEvent(const LifecycleEvent& event);
    void restoreFromStorage();
    void persistStateMachine();
    void updateComplexityUnlocked();
    void suspendCamera(const char* reason);
    void resumeCamera();
    void tickWhileCameraSuspended();
    void accumulateMinute(bool cameraOn, bool hasFace, const EyeMetrics& eye, float ci,
                          const SystemState& state, float dt, bool measuredDetector);
    void flushMinute();
    void publishTelemetry(InferencePackage& package, const EyeMetrics& eyeMetrics, const ComplexityMetrics& complexity,
                          const SystemState& state, float currentThreshold, int64_t count);
};

} // namespace efd
