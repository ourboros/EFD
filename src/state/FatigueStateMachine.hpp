#pragma once

#include "efd/types.hpp"
#include <functional>

namespace efd {

// 20/5/5 防打擾狀態機 (規格見 README 4.5 節)
// - 疲勞等級與警報皆依 60 秒 EWMA 平滑分數判定 (<45 清醒、>=45 注意、>=65 嚴重)
// - 平滑分數 >= 警報門檻且持續一段時間才提醒 (標準 65 分 / 10 秒，高靈敏度 58 分 / 5 秒)
// - 提醒後 20 分鐘冷卻不再提醒，離座期間照常倒數
// - 冷卻期滿後每 5 分鐘以在座期間的平均分數快篩：>= 門檻再提醒並重新冷卻；< 45 回常態；其餘繼續下一輪
// - 連續離座 5 分鐘視為已休息：清除冷卻並回到常態追蹤
class FatigueStateMachine {
public:
    using AlertCallback = std::function<void(FatigueLevel level, float score, const std::string& message)>;

    static constexpr float kAttentionScore       = 45.0f;
    static constexpr float kSevereScore          = 65.0f;
    static constexpr float kSmoothingTauSeconds  = 60.0f;
    static constexpr float kStandardAlertScore   = 65.0f;
    static constexpr float kStandardSustainSec   = 10.0f;
    static constexpr float kHighSensAlertScore   = 58.0f;
    static constexpr float kHighSensSustainSec   = 5.0f;

    explicit FatigueStateMachine(int cooldownSeconds = 1200,   // 20 分鐘
                                int screeningInterval = 300,  // 5 分鐘
                                int awayResetSeconds = 300);   // 5 分鐘離座清零

    // 每次特徵更新時呼叫 (deltaSeconds 為真實時間間隔)
    SystemState update(bool faceDetected, float perclos, float blinkRate, float complexityIndex, float deltaSeconds = 1.0f);

    // 註冊警報通知回呼函式
    void setAlertCallback(AlertCallback cb);

    // 高靈敏度模式：平滑分數 >= 58 持續 5 秒即提醒 (標準為 >= 65 持續 10 秒)
    void setHighSensitivity(bool enabled);
    bool isHighSensitivity() const;

    // 取得當前系統完整狀態
    SystemState getState() const;

    // 手動重置狀態機
    void reset();

    // 模擬或設定 14 天實驗進度
    void setStudyProgress(int currentDay, bool isLocked);

    static float computeFatigueScore(float perclos, float blinkRate, float complexityIndex);

private:
    float alertScore() const;
    float sustainSeconds() const;
    void fireAlert(float score);
    void enterCooldown();
    void enterScreening();
    void enterNormalTracking();
    FatigueLevel levelForScore(float score) const;

    int m_cooldownDuration;
    int m_screeningInterval;
    int m_awayResetThreshold;

    CooldownState m_cooldownState = CooldownState::NormalTracking;
    FatigueLevel  m_currentLevel = FatigueLevel::Relaxed;

    float m_cooldownTimer = 0.0f;
    float m_awayTimer = 0.0f;
    float m_lastScore = 0.0f;
    float m_smoothedScore = 0.0f;
    float m_aboveAlertSeconds = 0.0f;   // 平滑分數連續高於警報門檻的時間

    // 快篩期：在座期間分數的時間加權累積
    float m_screeningScoreSum = 0.0f;
    float m_screeningSeconds = 0.0f;

    bool  m_highSensitivity = false;
    int   m_studyDay = 1;
    bool  m_isStudyLocked = false;
    bool  m_userPresent = false;

    AlertCallback m_alertCallback;
};

} // namespace efd
