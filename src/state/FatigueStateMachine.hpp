#pragma once

#include "efd/types.hpp"
#include <functional>

namespace efd {

class FatigueStateMachine {
public:
    using AlertCallback = std::function<void(FatigueLevel level, float score, const std::string& message)>;

    explicit FatigueStateMachine(int cooldownSeconds = 1200,   // 20 分鐘
                                int screeningInterval = 300,  // 5 分鐘
                                int awayResetSeconds = 300);   // 5 分鐘離座清零

    // 每秒或每次特徵更新時呼叫 (deltaSeconds 為間隔時間)
    SystemState update(bool faceDetected, float perclos, float blinkRate, float complexityIndex, float deltaSeconds = 1.0f);

    // 註冊警報通知回呼函式
    void setAlertCallback(AlertCallback cb);

    // 取得當前系統完整狀態
    SystemState getState() const;

    // 手動重置狀態機
    void reset();

    // 模擬或設定 14 天實驗進度
    void setStudyProgress(int currentDay, bool isLocked);

private:
    float computeFatigueScore(float perclos, float blinkRate, float complexityIndex) const;

    int m_cooldownDuration;
    int m_screeningInterval;
    int m_awayResetThreshold;

    CooldownState m_cooldownState = CooldownState::NormalTracking;
    FatigueLevel  m_currentLevel = FatigueLevel::Relaxed;

    float m_cooldownTimer = 0.0f;
    float m_screeningTimer = 0.0f;
    float m_awayTimer = 0.0f;
    float m_lastScore = 0.0f;

    int   m_studyDay = 1;
    bool  m_isStudyLocked = false;

    AlertCallback m_alertCallback;
};

} // namespace efd

