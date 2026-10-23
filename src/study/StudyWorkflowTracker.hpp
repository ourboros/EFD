#pragma once

#include "efd/types.hpp"
#include <string>
#include <chrono>
#include <mutex>

namespace efd {

enum class StudyStatus : uint8_t {
    ActiveMonitoring = 0,   // Day 1 ~ 13: 正常監控採樣
    LockedForPostTest = 1,  // Day 14: 施測結束，強制鎖定後測門禁 (資產 5.png)
    CompletedUnlocked = 2   // 問卷填寫成功，已取得解鎖 Token (資產 6.png)
};

class StudyWorkflowTracker {
public:
    explicit StudyWorkflowTracker(const std::string& configPath = "efd_study_config.json");
    ~StudyWorkflowTracker() = default;

    void setConfigPath(const std::string& path);
    bool initialize();

    std::string getSubjectUuid() const;
    StudyStatus getStatus() const;
    int getCurrentDay() const;
    float getStudyProgressPercent() const;
    std::string getUnlockToken() const;

    // 推進科研天數 (支援 Time-Warp 加速測試)
    void setTimeWarpDay(int day);
    void advanceDay();

    // 觸發第 14 天施測結束門禁鎖定 (資產 5.png)
    void triggerPostStudyLock();

    // 提交後測問卷並獲取安全解鎖代碼 (資產 6.png)
    std::string submitQuestionnaire(const std::string& questionnairePayload);

    // 重設實驗週期 (供測試使用)
    void resetStudy(const std::string& newUuid = "");

    // 啟動次數與初始設定狀態 (判斷首次開啟或第二次及往後開啟)
    int getLaunchCount() const;
    bool isSecondOrSubsequentLaunch() const;
    bool isInitialSetupCompleted() const;
    void setInitialSetupCompleted(bool completed);

    // 初次校準成功：標記完成並保存個人化基準，下次啟動直接沿用
    void markInitialSetupCompleted(const CalibrationData& calibration);
    CalibrationData getSavedCalibration() const;

    // 雲端同步端點 (支援 Google 試算表 Web App 或科研中心 API)
    std::string getCloudSyncEndpoint() const;
    void setCloudSyncEndpoint(const std::string& endpoint);

    // 取得與更新最後成功同步科研數據的日期 (YYYYMMDD，用於防止重複上傳)
    int getLastSyncedDate() const;
    void setLastSyncedDate(int dateInt);
    int64_t getStudyStartTimestamp() const;

    // 起始日期 (YYYYMMDD) 與即時刷新天數
    int getStartDate() const;
    void setStartDate(int dateInt);
    int refreshStudyProgress();

private:
    std::string m_configPath;
    std::string m_subjectUuid;
    int m_currentDay = 1;
    StudyStatus m_status = StudyStatus::ActiveMonitoring;
    std::string m_unlockToken;
    int64_t m_studyStartTimestamp = 0;
    int m_startDate = 0;
    int m_launchCount = 0;
    bool m_hasCompletedInitialSetup = false;
    float m_savedBaselineEar = 0.0f;
    float m_savedEarStdDev = 0.0f;
    float m_savedThresholdEar = 0.0f;
    std::string m_cloudSyncEndpoint = "https://script.google.com/macros/s/AKfycbzwniwhgb6exjMC67tb_QMNOmp5xDCKKaS7aQs99bXmFv-qFY-U1ZhXcHLUFrJIFvyb5Q/exec";
    int m_lastSyncedDate = 0;
    mutable std::mutex m_mutex;

    int refreshStudyProgressUnlocked();
    bool initializeUnlocked();
    void resetInMemoryUnlocked();
    static int toJdn(int y, int m, int d);
    static int fromJdn(int jdn);
    static int getTodayDateInt();

    void saveConfigUnlocked();
    void loadConfigUnlocked();
    std::string generateRandomUuid();
};

} // namespace efd

