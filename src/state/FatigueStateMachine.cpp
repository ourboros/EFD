#include "FatigueStateMachine.hpp"
#include <algorithm>
#include <cmath>

namespace efd {

FatigueStateMachine::FatigueStateMachine(int cooldownSeconds, int screeningInterval, int awayResetSeconds)
    : m_cooldownDuration(cooldownSeconds),
      m_screeningInterval(screeningInterval),
      m_awayResetThreshold(awayResetSeconds) {
}

float FatigueStateMachine::computeFatigueScore(float perclos, float blinkRate, float complexityIndex) const {
    // 融合公式: PERCLOS (55%) + 眨眼率異常權重 (25%) + 非線性複雜度 CI (20%)
    // 1. PERCLOS: 0.0 ~ 0.20 -> 0 ~ 100 分 (採用多段動態靈敏度，擴大動態範圍，解決過去分數卡在 40~48 的問題)
    float perclosScore = 0.0f;
    if (perclos <= 0.04f) {
        // 0% ~ 4%: 正常清醒睜眼區間
        perclosScore = (perclos / 0.04f) * 15.0f; // 0 ~ 15 分
    } else if (perclos <= 0.10f) {
        // 4% ~ 10%: 開始出現眼睛沉重與疲勞
        perclosScore = 15.0f + ((perclos - 0.04f) / 0.06f) * 35.0f; // 15 ~ 50 分
    } else if (perclos <= 0.18f) {
        // 10% ~ 18%: 顯著眼皮下垂、閉眼遲滯
        perclosScore = 50.0f + ((perclos - 0.10f) / 0.08f) * 35.0f; // 50 ~ 85 分
    } else {
        // > 18%: 頻繁閉眼/微睡眠
        perclosScore = std::clamp(85.0f + ((perclos - 0.18f) / 0.10f) * 15.0f, 85.0f, 100.0f); // 85 ~ 100 分
    }

    // 2. 眨眼率: 正常約 10~22 次/分 (得分 0); 過低 (<10, 凝視乾眼) 或過高 (>22, 頻繁眨眼疲勞) 記分
    float blinkScore = 0.0f;
    if (blinkRate > 0.0f) {
        if (blinkRate < 10.0f) {
            blinkScore = std::clamp(((10.0f - blinkRate) / 8.0f) * 85.0f, 0.0f, 100.0f);
        } else if (blinkRate > 22.0f) {
            blinkScore = std::clamp(((blinkRate - 22.0f) / 14.0f) * 85.0f, 0.0f, 100.0f);
        } else {
            blinkScore = 0.0f; // 正常清醒區間不累加疲勞分
        }
    }

    // 3. 複雜度指標 CI: 正常清醒活躍 CI >= 2.5 (得分 0); 疲勞/呆滯時 CI < 2.5
    float complexityScore = 0.0f;
    if (complexityIndex > 0.0f) {
        if (complexityIndex < 2.5f) {
            complexityScore = std::clamp(((2.5f - complexityIndex) / 1.8f) * 80.0f, 0.0f, 100.0f);
        } else {
            complexityScore = 0.0f; // 正常清醒不累加疲勞分
        }
    }

    return std::clamp((0.55f * perclosScore) + (0.25f * blinkScore) + (0.20f * complexityScore), 0.0f, 100.0f);
}

SystemState FatigueStateMachine::update(bool faceDetected, float perclos, float blinkRate, float complexityIndex, float deltaSeconds) {
    m_userPresent = faceDetected;
    if (!faceDetected) {
        m_awayTimer += deltaSeconds;
        if (m_awayTimer >= static_cast<float>(m_awayResetThreshold)) {
            // 離座達到 5 分鐘，自動跳回 20 分鐘冷卻保護（防離座回座時直接彈窗或重複警報）
            m_cooldownState = CooldownState::InCooldown;
            m_currentLevel = FatigueLevel::UserAway;
            m_cooldownTimer = static_cast<float>(m_cooldownDuration); // 20 分鐘 (1200 秒)
            m_screeningTimer = static_cast<float>(m_screeningInterval); // 5 分鐘快篩倒數計時
            m_fatigueTriggered = true;
            m_screeningAlertFired = false;
        }
        return getState();
    }

    // 使用者在座 (人臉偵測成功)
    m_awayTimer = 0.0f;

    m_lastScore = computeFatigueScore(perclos, blinkRate, complexityIndex);

    // 20/5/5 狀態機推進 (嚴重疲勞閾值 65.0 分)
    switch (m_cooldownState) {
    case CooldownState::NormalTracking:
        // 常態追蹤中：若使用者曾觸發提醒，必須先完全恢復到放鬆狀態 (疲勞分數 < 45.0) 才能重置
        if (m_fatigueTriggered) {
            if (m_lastScore < 45.0f) {
                m_fatigueTriggered = false;
                m_currentLevel = FatigueLevel::Relaxed;
            } else {
                m_currentLevel = (m_lastScore >= 65.0f) ? FatigueLevel::SevereWarning : FatigueLevel::Attention;
            }
        } else {
            if (m_lastScore >= 65.0f) {
                // 直接進入紅色危險狀態 (嚴重疲勞警告：門檻 65.0 分)
                // 觸發 Windows 氣泡/Toast 警報，並強制進入 20 分鐘絕對靜默冷卻期
                m_fatigueTriggered = true;
                m_currentLevel = FatigueLevel::SevereWarning;
                m_cooldownState = CooldownState::InCooldown;
                m_cooldownTimer = static_cast<float>(m_cooldownDuration);   // 20 分鐘 (1200 秒)
                m_screeningTimer = static_cast<float>(m_screeningInterval); // 5 分鐘 (300 秒)
                m_screeningAlertFired = false;

                if (m_alertCallback) {
                    m_alertCallback(m_currentLevel, m_lastScore, "你的眼睛處於疲勞狀態，請適當休息");
                }
            } else if (m_lastScore >= 45.0f) {
                // 輕度注意力提醒：更新 UI 狀態，但不消耗 20 分鐘冷卻，亦不彈窗打擾
                m_currentLevel = FatigueLevel::Attention;
            } else {
                m_currentLevel = FatigueLevel::Relaxed;
            }
        }
        break;

    case CooldownState::InCooldown:
        m_cooldownTimer -= deltaSeconds;

        // 20 分鐘冷卻期內真實狀態跟隨即時疲勞分數，以反映真實生理狀況；冷卻機制專注於絕對靜默抑制彈窗
        if (m_lastScore >= 65.0f) {
            m_currentLevel = FatigueLevel::SevereWarning;
        } else if (m_lastScore >= 45.0f) {
            m_currentLevel = FatigueLevel::Attention;
        } else {
            m_currentLevel = FatigueLevel::Relaxed;
            m_fatigueTriggered = false; // 已恢復清醒放鬆
        }

        // 20 分鐘絕對靜默期滿，切換至 5 分鐘總體分數計算與快篩期
        if (m_cooldownTimer <= 0.0f) {
            m_cooldownTimer = 0.0f;
            m_cooldownState = CooldownState::FastScreening;
            m_screeningTimer = static_cast<float>(m_screeningInterval); // 5 分鐘 (300 秒)
            m_screeningAlertFired = false;
        }
        break;

    case CooldownState::FastScreening:
        // 20 分鐘冷卻後，每 5 分鐘進行一次總體分數計算與快篩
        m_screeningTimer -= deltaSeconds;

        if (m_lastScore >= 65.0f) {
            m_currentLevel = FatigueLevel::SevereWarning;
            // 快篩判定仍處於嚴重疲勞，發送升級提醒並立刻重啟新一輪 20 分鐘冷卻保護
            if (!m_screeningAlertFired) {
                m_screeningAlertFired = true;
                if (m_alertCallback) {
                    m_alertCallback(m_currentLevel, m_lastScore, "你的眼睛處於疲勞狀態，請適當休息");
                }
            }
            m_cooldownState = CooldownState::InCooldown;
            m_cooldownTimer = static_cast<float>(m_cooldownDuration);   // 重新鎖定 20 分鐘
            m_screeningTimer = static_cast<float>(m_screeningInterval); // 5 分鐘
            m_screeningAlertFired = false;
        } else if (m_lastScore >= 45.0f) {
            m_currentLevel = FatigueLevel::Attention;
            if (m_screeningTimer <= 0.0f) {
                // 快篩 5 分鐘週期結束，若使用者仍處於輕度疲勞，重置快篩計時器進行下一輪 5 分鐘檢測
                m_screeningTimer = static_cast<float>(m_screeningInterval);
            }
        } else {
            m_currentLevel = FatigueLevel::Relaxed;
            m_fatigueTriggered = false;
            // 使用者已完全清醒放鬆，平穩回歸常態全量追蹤
            m_cooldownState = CooldownState::NormalTracking;
            m_cooldownTimer = 0.0f;
            m_screeningTimer = 0.0f;
            m_screeningAlertFired = false;
        }
        break;

    case CooldownState::AwayPaused:
        // 兼容狀態
        m_cooldownState = CooldownState::InCooldown;
        m_cooldownTimer = static_cast<float>(m_cooldownDuration);
        break;
    }

    return getState();
}

void FatigueStateMachine::setAlertCallback(AlertCallback cb) {
    m_alertCallback = std::move(cb);
}

SystemState FatigueStateMachine::getState() const {
    SystemState state;
    state.fatigueLevel = m_currentLevel;
    state.cooldownState = m_cooldownState;
    state.currentFatigueScore = m_lastScore;
    state.cooldownRemainingSeconds = std::max(0, static_cast<int>(m_cooldownTimer));
    state.awaySeconds = static_cast<int>(m_awayTimer);
    state.studyDay = m_studyDay;
    state.isStudyLocked = m_isStudyLocked;
    state.userPresent = m_userPresent;
    state.timestamp = std::chrono::system_clock::now();
    return state;
}

void FatigueStateMachine::reset() {
    m_cooldownState = CooldownState::NormalTracking;
    m_currentLevel = FatigueLevel::Relaxed;
    m_cooldownTimer = 0.0f;
    m_screeningTimer = 0.0f;
    m_awayTimer = 0.0f;
    m_lastScore = 0.0f;
    m_fatigueTriggered = false;
    m_screeningAlertFired = false;
}

void FatigueStateMachine::setStudyProgress(int currentDay, bool isLocked) {
    m_studyDay = currentDay;
    m_isStudyLocked = isLocked;
}

} // namespace efd

