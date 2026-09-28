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
    if (!faceDetected) {
        m_awayTimer += deltaSeconds;
        if (m_awayTimer >= static_cast<float>(m_awayResetThreshold)) {
            // 離座超過 5 分鐘，重置 20 分鐘冷卻狀態機
            m_cooldownState = CooldownState::AwayPaused;
            m_currentLevel = FatigueLevel::UserAway;
            m_cooldownTimer = 0.0f;
            m_screeningTimer = 0.0f;
        }
        return getState();
    }

    // 使用者在座 (人臉偵測成功)
    if (m_cooldownState == CooldownState::AwayPaused) {
        // 使用者回座，重啟正常追蹤
        m_cooldownState = CooldownState::NormalTracking;
        m_currentLevel = FatigueLevel::Relaxed;
        m_awayTimer = 0.0f;
    }
    m_awayTimer = 0.0f;

    m_lastScore = computeFatigueScore(perclos, blinkRate, complexityIndex);

    // 20/5/5 狀態機推進 (使用者要求：放寬嚴重疲勞範圍，降低至 65.0 分)
    switch (m_cooldownState) {
    case CooldownState::NormalTracking:
        if (m_lastScore >= 65.0f) {
            // 直接進入紅色危險狀態 (嚴重疲勞警告：門檻放寬至 65.0 分)
            m_currentLevel = FatigueLevel::SevereWarning;
            m_cooldownState = CooldownState::InCooldown;
            m_cooldownTimer = static_cast<float>(m_cooldownDuration);
            m_screeningTimer = static_cast<float>(m_screeningInterval);

            if (m_alertCallback) {
                m_alertCallback(m_currentLevel, m_lastScore, "你的眼睛處於疲勞狀態，請適當休息");
            }
        } else if (m_lastScore >= 45.0f) {
            // 觸發初級提醒 (黃色注意：45.0 ~ 64.9 分)
            m_currentLevel = FatigueLevel::Attention;
            m_cooldownState = CooldownState::InCooldown;
            m_cooldownTimer = static_cast<float>(m_cooldownDuration);
            m_screeningTimer = static_cast<float>(m_screeningInterval);

            if (m_alertCallback) {
                m_alertCallback(m_currentLevel, m_lastScore, "偵測到用眼疲勞，建議休息或遠眺放鬆。");
            }
        } else {
            m_currentLevel = FatigueLevel::Relaxed;
        }
        break;

    case CooldownState::InCooldown:
        m_cooldownTimer -= deltaSeconds;
        m_screeningTimer -= deltaSeconds;

        if (m_screeningTimer <= 0.0f) {
            // 進入 5 分鐘快篩期
            m_cooldownState = CooldownState::FastScreening;
            m_screeningTimer = 30.0f; // 進行 30 秒快篩
        }

        if (m_cooldownTimer <= 0.0f) {
            // 20 分鐘冷卻期滿，重回正常追蹤
            m_cooldownState = CooldownState::NormalTracking;
            m_currentLevel = FatigueLevel::Relaxed;
        }
        break;

    case CooldownState::FastScreening:
        m_cooldownTimer -= deltaSeconds;
        m_screeningTimer -= deltaSeconds;

        if (m_lastScore >= 65.0f) {
            // 疲勞持續加劇，警報升級至紅色危險狀態 (門檻 65.0)
            m_currentLevel = FatigueLevel::SevereWarning;
            if (m_alertCallback) {
                m_alertCallback(m_currentLevel, m_lastScore, "你的眼睛處於疲勞狀態，請適當休息");
            }
        }

        if (m_screeningTimer <= 0.0f) {
            // 快篩結束，回到冷卻計時
            m_cooldownState = CooldownState::InCooldown;
            m_screeningTimer = static_cast<float>(m_screeningInterval);
        }

        if (m_cooldownTimer <= 0.0f) {
            m_cooldownState = CooldownState::NormalTracking;
            m_currentLevel = FatigueLevel::Relaxed;
        }
        break;

    case CooldownState::AwayPaused:
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
}

void FatigueStateMachine::setStudyProgress(int currentDay, bool isLocked) {
    m_studyDay = currentDay;
    m_isStudyLocked = isLocked;
}

} // namespace efd

