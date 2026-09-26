#include "FatigueStateMachine.hpp"
#include <algorithm>
#include <cmath>
#include <numeric>

namespace efd {

FatigueStateMachine::FatigueStateMachine(int cooldownSeconds, int screeningInterval, int awayResetSeconds)
    : m_cooldownDuration(cooldownSeconds),
      m_screeningInterval(screeningInterval),
      m_awayResetThreshold(awayResetSeconds) {
}

// =============================================================================
// 即時疲勞評分 (單幀特徵) — 結合文獻公式：PERCLOS + 眨眼異常 + 眨眼持續時間 + CI
// =============================================================================
float FatigueStateMachine::computeFatigueScore(float perclos, float blinkRate, float complexityIndex, float blinkDurationMs) const {
    // --- 1. PERCLOS 四段自適應曲線 (55% 權重) ---
    // 參考文獻: PERCLOS 在 30 秒視窗內計算，正常清醒 < 4%，疲勞顯著 > 10%
    float perclosScore = 0.0f;
    if (perclos <= 0.04f) {
        perclosScore = (perclos / 0.04f) * 12.0f;  // 0% ~ 4%: 清醒 (0 ~ 12)
    } else if (perclos <= 0.10f) {
        perclosScore = 12.0f + ((perclos - 0.04f) / 0.06f) * 40.0f; // 4% ~ 10%: 疲勞警告 (12 ~ 52)
    } else if (perclos <= 0.20f) {
        perclosScore = 52.0f + ((perclos - 0.10f) / 0.10f) * 33.0f; // 10% ~ 20%: 顯著疲勞 (52 ~ 85)
    } else {
        perclosScore = std::clamp(85.0f + ((perclos - 0.20f) / 0.10f) * 15.0f, 85.0f, 100.0f); // > 20%: 微睡眠
    }

    // --- 2. 眨眼率異常評分 (15% 權重) ---
    // 正常人類眨眼率: 10~22 次/分；凝視乾眼 < 8 次；頻繁疲勞眨眼 > 25 次
    float blinkRateScore = 0.0f;
    if (blinkRate > 0.0f) {
        if (blinkRate < 8.0f) {
            // 凝視/乾眼：眨眼過稀，單調高強度注視疲勞
            blinkRateScore = std::clamp(((8.0f - blinkRate) / 7.0f) * 80.0f, 0.0f, 100.0f);
        } else if (blinkRate < 10.0f) {
            blinkRateScore = std::clamp(((10.0f - blinkRate) / 2.0f) * 25.0f, 0.0f, 100.0f);
        } else if (blinkRate <= 22.0f) {
            blinkRateScore = 0.0f; // 正常清醒區間
        } else if (blinkRate <= 30.0f) {
            // 眨眼過頻：眼睛乾燥或疲勞
            blinkRateScore = std::clamp(((blinkRate - 22.0f) / 8.0f) * 50.0f, 0.0f, 100.0f);
        } else {
            blinkRateScore = std::clamp(((blinkRate - 30.0f) / 10.0f) * 50.0f + 50.0f, 0.0f, 100.0f);
        }
    }

    // --- 3. 眨眼持續時間評分 (15% 權重) ---
    // 文獻: 正常眨眼 100~400ms；疲勞時眨眼時間延長 (> 400ms) 或縮短 (< 80ms 雜訊)
    // BKDUR 增加是疲勞的強力指標（眼瞼肌肉疲勞反應遲鈍）
    float blinkDurScore = 0.0f;
    if (blinkDurationMs > 0.0f) {
        if (blinkDurationMs < 80.0f) {
            blinkDurScore = 0.0f; // 雜訊/反射性眨眼，不計入
        } else if (blinkDurationMs <= 200.0f) {
            blinkDurScore = 0.0f; // 正常清醒範圍
        } else if (blinkDurationMs <= 350.0f) {
            blinkDurScore = ((blinkDurationMs - 200.0f) / 150.0f) * 40.0f; // 200~350ms: 輕度疲勞
        } else if (blinkDurationMs <= 500.0f) {
            blinkDurScore = 40.0f + ((blinkDurationMs - 350.0f) / 150.0f) * 40.0f; // 350~500ms: 顯著疲勞
        } else {
            blinkDurScore = std::clamp(80.0f + ((blinkDurationMs - 500.0f) / 200.0f) * 20.0f, 0.0f, 100.0f);
        }
    }

    // --- 4. 複雜度指標 CI 評分 (15% 權重) ---
    // 文獻: 正常清醒 IMF2_CI >= 2.5；疲勞時高頻複雜度顯著下降
    // 融合公式: FI = w1*PERCLOS + w2*FQlS - w4*IMF2_CI
    float complexityScore = 0.0f;
    if (complexityIndex > 0.0f) {
        if (complexityIndex >= 2.5f) {
            complexityScore = 0.0f; // 清醒活躍，CI 高，不貢獻疲勞分
        } else if (complexityIndex >= 1.5f) {
            complexityScore = ((2.5f - complexityIndex) / 1.0f) * 40.0f; // 1.5~2.5: 逐漸降低
        } else if (complexityIndex >= 0.5f) {
            complexityScore = 40.0f + ((1.5f - complexityIndex) / 1.0f) * 40.0f;
        } else {
            complexityScore = std::clamp(80.0f + (0.5f - complexityIndex) * 40.0f, 0.0f, 100.0f);
        }
    } else {
        // CI = 0 (系統冷啟動 < 30 幀) → 不貢獻分數，以免虛高
        complexityScore = 0.0f;
    }

    // --- 最終加權融合 ---
    float rawScore = (0.55f * perclosScore)
                   + (0.15f * blinkRateScore)
                   + (0.15f * blinkDurScore)
                   + (0.15f * complexityScore);
    return std::clamp(rawScore, 0.0f, 100.0f);
}

// =============================================================================
// 1 分鐘趨勢評分 — 對過去 60 秒的即時評分進行加權平均（近期權重更高）
// =============================================================================
float FatigueStateMachine::computeSmoothedScore() const {
    if (m_scoreHistory.empty()) return m_lastScore;

    // 指數加權移動平均（較新的分數權重較高）
    float weightSum = 0.0f;
    float valueSum = 0.0f;
    float weight = 1.0f;
    const float decay = 1.05f; // 每個舊採樣點衰減 5%

    for (auto it = m_scoreHistory.rbegin(); it != m_scoreHistory.rend(); ++it) {
        valueSum += (*it) * weight;
        weightSum += weight;
        weight /= decay;
    }
    return std::clamp(valueSum / weightSum, 0.0f, 100.0f);
}

// =============================================================================
// 狀態機更新 — 整合 60 秒趨勢評分作為最終決策依據
// =============================================================================
SystemState FatigueStateMachine::update(bool faceDetected, float perclos, float blinkRate, float complexityIndex, float deltaSeconds) {
    m_userPresent = faceDetected;

    if (!faceDetected) {
        m_awayTimer += deltaSeconds;
        if (m_awayTimer >= static_cast<float>(m_awayResetThreshold)) {
            // 離座超過 5 分鐘，重置 20 分鐘冷卻狀態機並清空歷史
            m_cooldownState = CooldownState::AwayPaused;
            m_currentLevel = FatigueLevel::UserAway;
            m_cooldownTimer = 0.0f;
            m_screeningTimer = 0.0f;
            m_scoreHistory.clear(); // 離座清空歷史，避免回座時虛高
        }
        return getState();
    }

    // 使用者回座
    if (m_cooldownState == CooldownState::AwayPaused) {
        m_cooldownState = CooldownState::NormalTracking;
        m_currentLevel = FatigueLevel::Relaxed;
        m_awayTimer = 0.0f;
        m_scoreHistory.clear(); // 重新回座後清空舊歷史
    }
    m_awayTimer = 0.0f;

    // 計算即時分數（使用眨眼持續時間）
    m_lastScore = computeFatigueScore(perclos, blinkRate, complexityIndex, m_lastBlinkDurationMs);

    // 每秒向 1 分鐘滑動視窗推入新分數
    // deltaSeconds 通常為 1/30 秒，累計約每秒推入一次
    static float s_accumSec = 0.0f;
    s_accumSec += deltaSeconds;
    if (s_accumSec >= 1.0f) {
        s_accumSec -= 1.0f;
        m_scoreHistory.push_back(m_lastScore);
        if (m_scoreHistory.size() > WINDOW_60S) {
            m_scoreHistory.pop_front();
        }
    }

    // 計算 60 秒趨勢加權評分
    m_lastSmoothedScore = computeSmoothedScore();

    // ==========================================================================
    // 20/5/5 防打擾狀態機推進
    // 決策依據: 以「趨勢分數」(60 秒 EWMA) 作為主要判斷，避免瞬間雜訊誤報
    //          同時保留即時分數對異常高峰的靈敏反應（即時分數 >= 75 直接升級）
    // ==========================================================================
    switch (m_cooldownState) {
    case CooldownState::NormalTracking:
        if (m_lastSmoothedScore >= 65.0f || m_lastScore >= 75.0f) {
            // 趨勢分數達嚴重疲勞門檻，或瞬間值異常高峰 -> 紅色危險狀態
            m_currentLevel = FatigueLevel::SevereWarning;
            m_cooldownState = CooldownState::InCooldown;
            m_cooldownTimer = static_cast<float>(m_cooldownDuration);
            m_screeningTimer = static_cast<float>(m_screeningInterval);

            if (m_alertCallback) {
                m_alertCallback(m_currentLevel, m_lastSmoothedScore, "你的眼睛處於疲勞狀態，請適當休息");
            }
        } else if (m_lastSmoothedScore >= 45.0f) {
            // 趨勢分數進入注意力提醒區間 -> 黃色提醒
            m_currentLevel = FatigueLevel::Attention;
            m_cooldownState = CooldownState::InCooldown;
            m_cooldownTimer = static_cast<float>(m_cooldownDuration);
            m_screeningTimer = static_cast<float>(m_screeningInterval);

            if (m_alertCallback) {
                m_alertCallback(m_currentLevel, m_lastSmoothedScore, "偵測到用眼疲勞，建議休息或遠眺放鬆。");
            }
        } else {
            m_currentLevel = FatigueLevel::Relaxed;
        }
        break;

    case CooldownState::InCooldown:
        m_cooldownTimer -= deltaSeconds;
        m_screeningTimer -= deltaSeconds;

        if (m_screeningTimer <= 0.0f) {
            m_cooldownState = CooldownState::FastScreening;
            m_screeningTimer = 30.0f; // 30 秒快篩
        }

        if (m_cooldownTimer <= 0.0f) {
            m_cooldownState = CooldownState::NormalTracking;
            m_currentLevel = FatigueLevel::Relaxed;
        }
        break;

    case CooldownState::FastScreening:
        m_cooldownTimer -= deltaSeconds;
        m_screeningTimer -= deltaSeconds;

        // 快篩期間若趨勢分數再度攀升至嚴重疲勞 -> 升級警報
        if (m_lastSmoothedScore >= 65.0f || m_lastScore >= 75.0f) {
            m_currentLevel = FatigueLevel::SevereWarning;
            if (m_alertCallback) {
                m_alertCallback(m_currentLevel, m_lastSmoothedScore, "你的眼睛處於疲勞狀態，請適當休息");
            }
        }

        if (m_screeningTimer <= 0.0f) {
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
    state.smoothedFatigueScore = m_lastSmoothedScore;
    state.userPresent = m_userPresent;
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
    m_lastSmoothedScore = 0.0f;
    m_scoreHistory.clear();
    m_userPresent = true;
}

void FatigueStateMachine::setStudyProgress(int currentDay, bool isLocked) {
    m_studyDay = currentDay;
    m_isStudyLocked = isLocked;
}

} // namespace efd
