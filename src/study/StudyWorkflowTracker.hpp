#pragma once

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

    // 雲端同步端點 (支援 Google 試算表 Web App 或科研中心 API)
    std::string getCloudSyncEndpoint() const;
    void setCloudSyncEndpoint(const std::string& endpoint);

private:
    std::string m_configPath;
    std::string m_subjectUuid;
    int m_currentDay = 1;
    StudyStatus m_status = StudyStatus::ActiveMonitoring;
    std::string m_unlockToken;
    int64_t m_studyStartTimestamp = 0;
    int m_launchCount = 0;
    bool m_hasCompletedInitialSetup = false;
    std::string m_cloudSyncEndpoint = "https://script.google.com/macros/s/AKfycbyGXGJz2Xnv-PsBnwW0ycxtlKEcJD5q2bB5_gM-s_Xwl-_VupzlqcQy64jAgDL1uByM/exec";
    mutable std::mutex m_mutex;

    void saveConfigUnlocked();
    void loadConfigUnlocked();
    std::string generateRandomUuid();
};

} // namespace efd

