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

private:
    std::string m_configPath;
    std::string m_subjectUuid;
    int m_currentDay = 1;
    StudyStatus m_status = StudyStatus::ActiveMonitoring;
    std::string m_unlockToken;
    int64_t m_studyStartTimestamp = 0;
    mutable std::mutex m_mutex;

    void saveConfigUnlocked();
    void loadConfigUnlocked();
    std::string generateRandomUuid();
};

} // namespace efd

