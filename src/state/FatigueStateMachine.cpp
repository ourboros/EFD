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
    // 融合公式: PERCLOS (50%) + 眨眼率異常權重 (30%) + 非線性複雜度 CI (20%)
    // 1. PERCLOS: 0.0 ~ 0.15+ (採用高靈敏度多段映射，敏銳捕捉眼瞼下垂與微睡眠)
    float perclosScore = 0.0f;
    if (perclos <= 0.015f) {
        // 0% ~ 1.5%: 正常完全清醒睜眼區間
        perclosScore = (perclos / 0.015f) * 15.0f; // 0 ~ 15 分
    } else if (perclos <= 0.05f) {
        // 1.5% ~ 5%: 開始出現眼皮下垂與疲勞沉重感 (大幅拉高敏銳度)
        perclosScore = 15.0f + ((perclos - 0.015f) / 0.035f) * 35.0f; // 15 ~ 50 分
    } else if (perclos <= 0.10f) {
        // 5% ~ 10%: 明顯眼皮閉合遲緩、用眼過度
        perclosScore = 50.0f + ((perclos - 0.05f) / 0.05f) * 35.0f; // 50 ~ 85 分
    } else {
        // > 10%: 頻繁閉眼 / 微睡眠 / 嚴重過度疲勞
        perclosScore = std::clamp(85.0f + ((perclos - 0.10f) / 0.05f) * 15.0f, 85.0f, 100.0f); // 85 ~ 100 分
    }

    // 2. 眨眼率: 正常約 12~20 次/分 (得分 5); 異常過低 (<10, 凝視乾眼) 或過高 (>20, 乾澀頻繁用力眨眼)
    float blinkScore = 0.0f;
    if (blinkRate > 0.0f) {
        if (blinkRate < 10.0f) {
            blinkScore = std::clamp(((10.0f - blinkRate) / 10.0f) * 75.0f + 15.0f, 0.0f, 100.0f);
        } else if (blinkRate > 20.0f) {
            blinkScore = std::clamp(((blinkRate - 20.0f) / 14.0f) * 75.0f + 15.0f, 0.0f, 100.0f);
        } else {
            blinkScore = 5.0f; // 正常清醒區間給予極低基礎分
        }
    } else {
        blinkScore = 10.0f;
    }

    // 3. 複雜度指標 CI: 正常清醒活躍 CI >= 3.0 (得分 0); 疲勞/呆滯時 CI < 3.0
    float complexityScore = 0.0f;
    if (complexityIndex > 0.0f) {
        if (complexityIndex < 3.0f) {
            complexityScore = std::clamp(((3.0f - complexityIndex) / 2.5f) * 80.0f + 15.0f, 0.0f, 100.0f);
        } else {
            complexityScore = 0.0f; // 正常清醒不累加疲勞分
        }
    }

    return std::clamp((0.50f * perclosScore) + (0.30f * blinkScore) + (0.20f * complexityScore), 0.0f, 100.0f);
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

    // 20/5/5 狀態機推進 (高靈敏度疲勞警報升級邏輯：過度疲勞門檻調整為 55.0 分，初級提醒調整為 35.0 分)
    switch (m_cooldownState) {
    case CooldownState::NormalTracking:
        if (m_lastScore >= 55.0f) {
            // 直接進入紅色危險狀態 (嚴重過度疲勞警告：門檻 55.0 分)
            m_currentLevel = FatigueLevel::SevereWarning;
            m_cooldownState = CooldownState::InCooldown;
            m_cooldownTimer = static_cast<float>(m_cooldownDuration);
            m_screeningTimer = static_cast<float>(m_screeningInterval);

            if (m_alertCallback) {
                m_alertCallback(m_currentLevel, m_lastScore, "你的眼睛處於疲勞狀態，請適當休息");
            }
        } else if (m_lastScore >= 35.0f) {
            // 觸發初級提醒 (黃色注意：35.0 ~ 54.9 分)
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

        if (m_lastScore >= 55.0f) {
            // 疲勞持續加劇，警報升級至紅色危險狀態 (門檻 55.0)
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

