#pragma once

#include "efd/types.hpp"
#include <functional>
#include <mutex>
#include <string>

namespace efd {

// 20/5/5 防打擾狀態機 (規格以 System design.md 第 5.2 節為準)
// - 觸發：60 秒平滑分數 >= 65 且持續 10 秒 -> 發送提醒並進入 20 分鐘冷卻
// - 冷卻：20 分鐘內不再提醒 (離座期間仍持續倒數)
// - 快篩：冷卻期滿後，每 5 分鐘 (在座時間) 以該窗口平均分數判定一次
//         >= 65 再次提醒並重新冷卻；< 45 回到常態追蹤；其餘再快篩 5 分鐘
// - 離座：連續 5 分鐘未偵測到人臉，視為已休息，清除冷卻與平滑分數
// - 靈敏度：只調整提醒門檻與持續秒數 (低 70/15s、標準 65/10s、高 58/5s)，20/5/5 時間規則不變
class FatigueStateMachine {
public:
    using AlertCallback = std::function<void(FatigueLevel level, float score, const std::string& message)>;

    // 標準靈敏度的預設值
    static constexpr float kSevereThreshold = 65.0f;
    static constexpr float kAttentionThreshold = 45.0f;
    static constexpr float kSevereHoldSeconds = 10.0f;
    static constexpr float kSmoothingTauSeconds = 60.0f;

    static constexpr int kSensitivityLow = 0;
    static constexpr int kSensitivityStandard = 1;
    static constexpr int kSensitivityHigh = 2;

    struct SensitivityProfile {
        float severeThreshold;
        float holdSeconds;
    };
    static SensitivityProfile profileForSensitivity(int level);

    explicit FatigueStateMachine(int cooldownSeconds = 1200,   // 20 分鐘
                                int screeningInterval = 300,  // 5 分鐘
                                int awayResetSeconds = 300);   // 5 分鐘離座清零

    // 每次特徵更新時呼叫 (deltaSeconds 為距上次呼叫的真實經過時間)
    SystemState update(bool faceDetected, float perclos, float blinkRate, float complexityIndex, float deltaSeconds = 1.0f);

    // 註冊警報通知回呼函式 (於呼叫 update 的執行緒觸發，且不持有內部鎖)
    void setAlertCallback(AlertCallback cb);

    // 取得當前系統完整狀態
    SystemState getState() const;

    // 手動重置狀態機
    void reset();

    // 提示靈敏度 (0 低 / 1 標準 / 2 高)，超出範圍視為標準
    void setSensitivity(int level);
    int getSensitivity() const;
    float getSevereThreshold() const;

    // 模擬或設定 14 天實驗進度
    void setStudyProgress(int currentDay, bool isLocked);

    // 冷卻狀態持久化：App 重啟後延續冷卻倒數
    // 關閉期間 >= 離座清零秒數時，比照離座規則清零
    std::string serialize(int64_t nowEpochSec) const;
    bool restore(const std::string& data, int64_t nowEpochSec);

    static float computeFatigueScore(float perclos, float blinkRate, float complexityIndex);

private:
    SystemState getStateUnlocked() const;
    void enterCooldownUnlocked();
    void clearCooldownUnlocked();

    int m_cooldownDuration;
    int m_screeningInterval;
    int m_awayResetThreshold;

    int   m_sensitivity = kSensitivityStandard;
    float m_severeThreshold = kSevereThreshold;
    float m_severeHoldSeconds = kSevereHoldSeconds;

    CooldownState m_cooldownState = CooldownState::NormalTracking;
    FatigueLevel  m_currentLevel = FatigueLevel::Relaxed;

    float m_cooldownTimer = 0.0f;
    float m_screeningTimer = 0.0f;
    float m_screeningScoreSum = 0.0f;
    float m_screeningSampleSeconds = 0.0f;
    float m_awayTimer = 0.0f;
    float m_severeHoldTimer = 0.0f;
    float m_lastScore = 0.0f;
    float m_smoothedScore = 0.0f;

    int   m_studyDay = 1;
    bool  m_isStudyLocked = false;
    bool  m_userPresent = false;
    bool  m_awayResetApplied = false;

    AlertCallback m_alertCallback;
    mutable std::mutex m_mutex;
};

} // namespace efd
