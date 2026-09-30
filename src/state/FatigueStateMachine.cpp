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
        } else {
            // 短暫離開鏡頭 (< 5 分鐘)：標記離座，但保留冷卻計時器防止誤觸發警報
            m_currentLevel = FatigueLevel::UserAway;
        }
        return getState();
    }

    // 使用者在座 (人臉偵測成功)
    if (m_cooldownState == CooldownState::AwayPaused) {
        // 使用者離座超過 5 分鐘後回座，重啟全新 20 分鐘監控追蹤
        m_cooldownState = CooldownState::NormalTracking;
        m_currentLevel = FatigueLevel::Relaxed;
        m_awayTimer = 0.0f;
    }
    m_awayTimer = 0.0f;

    m_lastScore = computeFatigueScore(perclos, blinkRate, complexityIndex);

    // 20/5/5 狀態機推進 (20分鐘冷卻週期 / 5分鐘快篩間隔 / 5分鐘離座清零)
    switch (m_cooldownState) {
    case CooldownState::NormalTracking:
        if (m_lastScore >= 65.0f) {
            // 首次觸發嚴重疲勞警告：啟動 20 分鐘防打擾冷卻週期與 5 分鐘快篩間隔
            m_currentLevel = FatigueLevel::SevereWarning;
            m_cooldownState = CooldownState::InCooldown;
            m_cooldownTimer = static_cast<float>(m_cooldownDuration);      // 1200 秒 (20 分鐘)
            m_screeningTimer = static_cast<float>(m_screeningInterval);    // 300 秒 (5 分鐘)

            // 僅發送一次警告通知
            if (m_alertCallback) {
                m_alertCallback(m_currentLevel, m_lastScore, "你的眼睛處於疲勞狀態，請適當休息");
            }
        } else if (m_lastScore >= 45.0f) {
            // 觸發初級提醒 (黃色注意：45.0 ~ 64.9 分)
            m_currentLevel = FatigueLevel::Attention;
        } else {
            m_currentLevel = FatigueLevel::Relaxed;
        }
        break;

    case CooldownState::InCooldown:
        m_cooldownTimer -= deltaSeconds;
        m_screeningTimer -= deltaSeconds;

        // 冷卻期中嚴禁重複發送警報通知，保持安靜防打擾
        if (m_screeningTimer <= 0.0f) {
            // 每 5 分鐘快篩間隔到達，進入 5 秒短暫快篩期
            m_cooldownState = CooldownState::FastScreening;
            m_screeningTimer = 5.0f;
        }

        if (m_cooldownTimer <= 0.0f) {
            // 20 分鐘冷卻期滿，重回正常追蹤
            m_cooldownState = CooldownState::NormalTracking;
            m_currentLevel = (m_lastScore >= 45.0f) ? FatigueLevel::Attention : FatigueLevel::Relaxed;
            m_cooldownTimer = 0.0f;
            m_screeningTimer = 0.0f;
        }
        break;

    case CooldownState::FastScreening:
        m_cooldownTimer -= deltaSeconds;
        m_screeningTimer -= deltaSeconds;

        if (m_lastScore >= 65.0f) {
            // 快篩時疲勞仍未緩解：僅發送一次升級提醒，並立即重設 5 分鐘快篩計時回到 InCooldown
            m_currentLevel = FatigueLevel::SevereWarning;
            if (m_alertCallback) {
                m_alertCallback(m_currentLevel, m_lastScore, "疲勞持續未緩解，建議適度休息放鬆");
            }
            m_cooldownState = CooldownState::InCooldown;
            m_screeningTimer = static_cast<float>(m_screeningInterval);
        } else if (m_screeningTimer <= 0.0f) {
            // 快篩結束，疲勞已有所緩解，回到冷卻計時
            m_cooldownState = CooldownState::InCooldown;
            m_screeningTimer = static_cast<float>(m_screeningInterval);
            if (m_lastScore < 45.0f) {
                m_currentLevel = FatigueLevel::Relaxed;
            }
        }

        if (m_cooldownTimer <= 0.0f) {
            m_cooldownState = CooldownState::NormalTracking;
            m_currentLevel = (m_lastScore >= 45.0f) ? FatigueLevel::Attention : FatigueLevel::Relaxed;
            m_cooldownTimer = 0.0f;
            m_screeningTimer = 0.0f;
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

