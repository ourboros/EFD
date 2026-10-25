#include "FatigueStateMachine.hpp"
#include <algorithm>
#include <cmath>

namespace efd {

FatigueStateMachine::FatigueStateMachine(int cooldownSeconds, int screeningInterval, int awayResetSeconds)
    : m_cooldownDuration(cooldownSeconds),
      m_screeningInterval(screeningInterval),
      m_awayResetThreshold(awayResetSeconds) {
}

float FatigueStateMachine::computeFatigueScore(float perclos, float blinkRate, float complexityIndex) {
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

namespace {
const char* const kAlertMessage = "你的眼睛處於疲勞狀態，請適當休息";
}

float FatigueStateMachine::alertScore() const {
    return m_highSensitivity ? kHighSensAlertScore : kStandardAlertScore;
}

float FatigueStateMachine::sustainSeconds() const {
    return m_highSensitivity ? kHighSensSustainSec : kStandardSustainSec;
}

FatigueLevel FatigueStateMachine::levelForScore(float score) const {
    if (score >= kSevereScore) return FatigueLevel::SevereWarning;
    if (score >= kAttentionScore) return FatigueLevel::Attention;
    return FatigueLevel::Relaxed;
}

void FatigueStateMachine::fireAlert(float score) {
    if (m_alertCallback) {
        m_alertCallback(FatigueLevel::SevereWarning, score, kAlertMessage);
    }
}

void FatigueStateMachine::enterCooldown() {
    m_cooldownState = CooldownState::InCooldown;
    m_cooldownTimer = static_cast<float>(m_cooldownDuration);
    m_aboveAlertSeconds = 0.0f;
}

void FatigueStateMachine::enterScreening() {
    m_cooldownState = CooldownState::FastScreening;
    m_cooldownTimer = 0.0f;
    m_screeningScoreSum = 0.0f;
    m_screeningSeconds = 0.0f;
}

void FatigueStateMachine::enterNormalTracking() {
    m_cooldownState = CooldownState::NormalTracking;
    m_cooldownTimer = 0.0f;
    m_aboveAlertSeconds = 0.0f;
    m_screeningScoreSum = 0.0f;
    m_screeningSeconds = 0.0f;
}

SystemState FatigueStateMachine::update(bool faceDetected, float perclos, float blinkRate, float complexityIndex, float deltaSeconds) {
    deltaSeconds = std::max(0.0f, deltaSeconds);
    m_userPresent = faceDetected;

    // 冷卻期不論在座或離座都照常倒數
    if (m_cooldownState == CooldownState::InCooldown) {
        m_cooldownTimer -= deltaSeconds;
        if (m_cooldownTimer <= 0.0f) {
            enterScreening();
        }
    }

    if (!faceDetected) {
        m_awayTimer += deltaSeconds;
        m_aboveAlertSeconds = 0.0f;
        if (m_awayTimer >= 1.0f) {
            m_currentLevel = FatigueLevel::UserAway;
        }
        if (m_awayTimer >= static_cast<float>(m_awayResetThreshold)) {
            // 離座滿 5 分鐘視為已休息：清除冷卻與平滑分數，回到常態追蹤
            enterNormalTracking();
            m_smoothedScore = 0.0f;
            m_lastScore = 0.0f;
            m_currentLevel = FatigueLevel::UserAway;
        }
        return getState();
    }

    m_awayTimer = 0.0f;
    m_lastScore = computeFatigueScore(perclos, blinkRate, complexityIndex);

    // 60 秒時間常數 EWMA (依真實時間間隔計算權重，與幀率無關)
    const float alpha = 1.0f - std::exp(-deltaSeconds / kSmoothingTauSeconds);
    m_smoothedScore += alpha * (m_lastScore - m_smoothedScore);
    m_currentLevel = levelForScore(m_smoothedScore);

    switch (m_cooldownState) {
    case CooldownState::NormalTracking:
    case CooldownState::AwayPaused:
        m_cooldownState = CooldownState::NormalTracking;
        if (m_smoothedScore >= alertScore()) {
            m_aboveAlertSeconds += deltaSeconds;
        } else {
            m_aboveAlertSeconds = 0.0f;
        }
        if (m_aboveAlertSeconds >= sustainSeconds()) {
            fireAlert(m_smoothedScore);
            enterCooldown();
        }
        break;

    case CooldownState::InCooldown:
        // 20 分鐘內完全不提醒，僅更新畫面狀態
        break;

    case CooldownState::FastScreening:
        m_screeningScoreSum += m_lastScore * deltaSeconds;
        m_screeningSeconds += deltaSeconds;
        if (m_screeningSeconds >= static_cast<float>(m_screeningInterval)) {
            const float average = m_screeningScoreSum / m_screeningSeconds;
            if (average >= alertScore()) {
                fireAlert(average);
                enterCooldown();
            } else if (average < kAttentionScore) {
                enterNormalTracking();
            } else {
                enterScreening();
            }
        }
        break;
    }

    return getState();
}

void FatigueStateMachine::setHighSensitivity(bool enabled) {
    m_highSensitivity = enabled;
}

bool FatigueStateMachine::isHighSensitivity() const {
    return m_highSensitivity;
}

void FatigueStateMachine::setAlertCallback(AlertCallback cb) {
    m_alertCallback = std::move(cb);
}

SystemState FatigueStateMachine::getState() const {
    SystemState state;
    state.fatigueLevel = m_currentLevel;
    state.cooldownState = m_cooldownState;
    state.currentFatigueScore = m_lastScore;
    state.smoothedFatigueScore = m_smoothedScore;
    state.cooldownRemainingSeconds = std::max(0, static_cast<int>(m_cooldownTimer));
    state.cooldownRemainingSec = state.cooldownRemainingSeconds;
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
    m_awayTimer = 0.0f;
    m_lastScore = 0.0f;
    m_smoothedScore = 0.0f;
    m_aboveAlertSeconds = 0.0f;
    m_screeningScoreSum = 0.0f;
    m_screeningSeconds = 0.0f;
}

void FatigueStateMachine::setStudyProgress(int currentDay, bool isLocked) {
    m_studyDay = currentDay;
    m_isStudyLocked = isLocked;
}

} // namespace efd

