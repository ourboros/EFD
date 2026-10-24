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
#include "storage/DatabaseService.hpp"
#include "study/StudyWorkflowTracker.hpp"
#include "network/NetworkSyncWorker.hpp"

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
    FaceLandmarker::PresenceDiagnostic presenceDiagnostic;
    EyeMetrics eyeMetrics;
    ComplexityMetrics complexityMetrics;
    SystemState systemState;
    float currentThreshold = 0.21f;
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

    // 是否已有保存的個人化校準 (可直接進入監控畫面)
    bool hasSavedCalibration() const;

    // 提示靈敏度 (0 低 / 1 標準 / 2 高)：立即套用到狀態機並保存到設定檔
    void setSensitivity(int level);
    int getSensitivity() const;

    // 設定模擬眼睛開合狀態 (供測試與驗證)
    void setSimulatedEyeOpenness(float openness);

    // 執行手動觸發當日眼動數據落盤與雲端同步
    bool triggerManualDataSync();

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
    void* getNativeCameraHandle() const { return m_camera ? m_camera->getNativeCameraHandle() : nullptr; }
    FaceLandmarker& getFaceLandmarker() { return *m_landmarker; }
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
    std::unique_ptr<FaceLandmarker> m_landmarker;
    FeatureExtractor m_extractor;
    AdaptiveBaseline m_baseline;
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
        FaceLandmarker::PresenceDiagnostic presenceDiagnostic;
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
    size_t m_calibrationTotalFrames = 0;
    float m_lastCalibrationValidRatio = 0.0f;
    std::atomic<bool> m_requestEyeBaselineReset{false};

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

    void inferenceWorkerLoop();
    void signalProcessingWorkerLoop();
    void handleLifecycleEvent(const LifecycleEvent& event);
    void restoreFromStorage();
    void persistStateMachine();
    void updateComplexityUnlocked();
};

} // namespace efd
