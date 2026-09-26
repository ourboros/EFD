#pragma once

#include "efd/types.hpp"
#include <functional>
#include <deque>

namespace efd {

class FatigueStateMachine {
public:
    using AlertCallback = std::function<void(FatigueLevel level, float score, const std::string& message)>;

    explicit FatigueStateMachine(int cooldownSeconds = 1200,   // 20 分鐘
                                int screeningInterval = 300,  // 5 分鐘
                                int awayResetSeconds = 300);   // 5 分鐘離座清零

    // 每秒或每次特徵更新時呼叫 (deltaSeconds 為間隔時間)
    SystemState update(bool faceDetected, float perclos, float blinkRate, float complexityIndex, float deltaSeconds = 1.0f);

    // 更新眨眼持續時間（毫秒，用於加強疲勞判斷）
    void setLastBlinkDurationMs(float durationMs) { m_lastBlinkDurationMs = durationMs; }

    // 註冊警報通知回呼函式
    void setAlertCallback(AlertCallback cb);

    // 取得當前系統完整狀態
    SystemState getState() const;

    // 手動重置狀態機
    void reset();

    // 模擬或設定 14 天實驗進度
    void setStudyProgress(int currentDay, bool isLocked);

private:
    // 即時評分（單幀特徵）
    float computeFatigueScore(float perclos, float blinkRate, float complexityIndex, float blinkDurationMs) const;
    // 1 分鐘趨勢評分（60 秒滑動平均）
    float computeSmoothedScore() const;

    int m_cooldownDuration;
    int m_screeningInterval;
    int m_awayResetThreshold;

    CooldownState m_cooldownState = CooldownState::NormalTracking;
    FatigueLevel  m_currentLevel = FatigueLevel::Relaxed;

    float m_cooldownTimer = 0.0f;
    float m_screeningTimer = 0.0f;
    float m_awayTimer = 0.0f;
    float m_lastScore = 0.0f;
    float m_lastSmoothedScore = 0.0f;
    float m_lastBlinkDurationMs = 150.0f; // 正常眨眼平均 150~200ms

    // 1 分鐘滑動視窗：記錄過去 60 秒（每秒一個採樣點）的即時評分
    static constexpr size_t WINDOW_60S = 60;
    std::deque<float> m_scoreHistory; // 每秒推入一個新分數

    // 人臉偵測狀態
    bool m_userPresent = true;

    int   m_studyDay = 1;
    bool  m_isStudyLocked = false;

    AlertCallback m_alertCallback;
};

} // namespace efd
