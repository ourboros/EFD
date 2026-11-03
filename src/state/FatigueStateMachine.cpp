#include "FatigueStateMachine.hpp"
#include <algorithm>
#include <cmath>
#include <sstream>

namespace efd {

namespace {
const char* kAlertMessage = "你的眼睛處於疲勞狀態，請適當休息";

FatigueLevel levelForScore(float score, float severeThreshold) {
    if (score >= severeThreshold) return FatigueLevel::SevereWarning;
    if (score >= FatigueStateMachine::kAttentionThreshold) return FatigueLevel::Attention;
    return FatigueLevel::Relaxed;
}
} // anonymous namespace

FatigueStateMachine::FatigueStateMachine(int cooldownSeconds, int screeningInterval, int awayResetSeconds)
    : m_cooldownDuration(cooldownSeconds),
      m_screeningInterval(screeningInterval),
      m_awayResetThreshold(awayResetSeconds) {
}

FatigueStateMachine::SensitivityProfile FatigueStateMachine::profileForSensitivity(int level) {
    switch (level) {
    case kSensitivityLow:  return {70.0f, 10.0f};
    case kSensitivityHigh: return {58.0f, 3.0f};
    default:               return {kSevereThreshold, kSevereHoldSeconds};
    }
}

void FatigueStateMachine::setSensitivity(int level) {
    if (level < kSensitivityLow || level > kSensitivityHigh) {
        level = kSensitivityStandard;
    }
    SensitivityProfile profile = profileForSensitivity(level);
    std::lock_guard<std::mutex> lock(m_mutex);
    m_sensitivity = level;
    m_severeThreshold = profile.severeThreshold;
    m_severeHoldSeconds = profile.holdSeconds;
    m_severeHoldTimer = 0.0f;
}

int FatigueStateMachine::getSensitivity() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_sensitivity;
}

float FatigueStateMachine::getSevereThreshold() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_severeThreshold;
}

float FatigueStateMachine::computeFatigueScore(float perclos, float blinkRate, float complexityIndex,
                                               float blinkDurationMs, float longClosuresPerMin) {
    // 融合公式: PERCLOS (55%) + 眨眼異常 (25%) + 非線性複雜度 CI (20%)
    // 1. PERCLOS: 0.0 ~ 0.20 -> 0 ~ 100 分 (多段動態靈敏度)
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
    //    blinkRate == 0 代表窗口尚未累積足夠 (< 30 秒)，不計分
    float blinkScore = 0.0f;
    if (blinkRate > 0.0f) {
        if (blinkRate < 10.0f) {
            blinkScore = std::clamp(((10.0f - blinkRate) / 8.0f) * 85.0f, 0.0f, 100.0f);
        } else if (blinkRate > 22.0f) {
            blinkScore = std::clamp(((blinkRate - 22.0f) / 14.0f) * 85.0f, 0.0f, 100.0f);
        }
    }
    // 眨眼變慢：窗口平均眨眼時長 250 ms 以內不計分，450 ms 約 85 分 (A71 實測清醒約 110~130 ms)
    if (blinkDurationMs > 250.0f) {
        blinkScore = std::max(blinkScore, std::clamp(((blinkDurationMs - 250.0f) / 200.0f) * 85.0f, 0.0f, 100.0f));
    }
    // 長閉眼 (> 800 ms)：每分鐘 3 次約 85 分
    if (longClosuresPerMin > 0.0f) {
        blinkScore = std::max(blinkScore, std::clamp((longClosuresPerMin / 3.0f) * 85.0f, 0.0f, 100.0f));
    }

    // 3. 複雜度指標 CI (MSE 容許誤差固定為個人睜眼 EAR 範圍的 15%)：
    //    A71 實測清醒看螢幕約 1.3~2.2，長時間閉眼降到 0.35~0.65；CI >= 1.0 不計分，0.3 約 80 分
    float complexityScore = 0.0f;
    if (complexityIndex > 0.0f && complexityIndex < 1.0f) {
        complexityScore = std::clamp(((1.0f - complexityIndex) / 0.7f) * 80.0f, 0.0f, 100.0f);
    }

    // PERCLOS 權重 55% 時單靠閉眼最多 55 分，到不了提醒門檻 65 (A71 實測 PERCLOS 69% 只有 62 分)；
    // PERCLOS 是文獻中最可靠的單一指標，因此單項分數乘 0.9 也可觸發提醒 (約 PERCLOS 15% 以上)
    const float fused = (0.55f * perclosScore) + (0.25f * blinkScore) + (0.20f * complexityScore);
    return std::clamp(std::max(fused, kPerclosOnlyWeight * perclosScore), 0.0f, 100.0f);
}

void FatigueStateMachine::enterCooldownUnlocked() {
    m_cooldownState = CooldownState::InCooldown;
    m_cooldownTimer = static_cast<float>(m_cooldownDuration);
    m_screeningTimer = 0.0f;
    m_screeningScoreSum = 0.0f;
    m_screeningSampleSeconds = 0.0f;
    m_severeHoldTimer = 0.0f;
}

void FatigueStateMachine::clearCooldownUnlocked() {
    m_cooldownState = CooldownState::NormalTracking;
    m_cooldownTimer = 0.0f;
    m_screeningTimer = 0.0f;
    m_screeningScoreSum = 0.0f;
    m_screeningSampleSeconds = 0.0f;
    m_severeHoldTimer = 0.0f;
}

SystemState FatigueStateMachine::update(bool faceDetected, float perclos, float blinkRate, float complexityIndex, float deltaSeconds,
                                        float blinkDurationMs, float longClosuresPerMin) {
    bool fireAlert = false;
    float alertScore = 0.0f;
    SystemState snapshot;
    AlertCallback callback;

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        const float dt = std::max(0.0f, deltaSeconds);
        m_userPresent = faceDetected;

        if (!faceDetected) {
            m_awayTimer += dt;
            m_severeHoldTimer = 0.0f;
            if (m_awayTimer >= 1.0f) {
                m_currentLevel = FatigueLevel::UserAway;
            }

            // 冷卻是「不再打擾」的真實時間，離座期間照常倒數
            if (m_cooldownState == CooldownState::InCooldown) {
                m_cooldownTimer -= dt;
                if (m_cooldownTimer <= 0.0f) {
                    m_cooldownTimer = 0.0f;
                    m_cooldownState = CooldownState::FastScreening;
                    m_screeningTimer = static_cast<float>(m_screeningInterval);
                    m_screeningScoreSum = 0.0f;
                    m_screeningSampleSeconds = 0.0f;
                }
            }

            // 離座滿 5 分鐘：視為已休息，清除冷卻與累積分數
            if (m_awayTimer >= static_cast<float>(m_awayResetThreshold) && !m_awayResetApplied) {
                clearCooldownUnlocked();
                m_smoothedScore = 0.0f;
                m_lastScore = 0.0f;
                m_awayResetApplied = true;
            }
            snapshot = getStateUnlocked();
        } else {
            m_awayTimer = 0.0f;
            m_awayResetApplied = false;

            m_lastScore = computeFatigueScore(perclos, blinkRate, complexityIndex, blinkDurationMs, longClosuresPerMin);
            // kSmoothingTauSeconds (20 秒) 時間常數 EWMA (與取樣頻率無關)
            float alpha = 1.0f - std::exp(-dt / kSmoothingTauSeconds);
            m_smoothedScore += alpha * (m_lastScore - m_smoothedScore);
            m_currentLevel = levelForScore(m_smoothedScore, m_severeThreshold);

            switch (m_cooldownState) {
            case CooldownState::NormalTracking:
                if (m_smoothedScore >= m_severeThreshold) {
                    m_severeHoldTimer += dt;
                } else {
                    m_severeHoldTimer = 0.0f;
                }
                if (m_severeHoldTimer >= m_severeHoldSeconds) {
                    fireAlert = true;
                    alertScore = m_smoothedScore;
                    enterCooldownUnlocked();
                }
                break;

            case CooldownState::InCooldown:
                m_cooldownTimer -= dt;
                if (m_cooldownTimer <= 0.0f) {
                    m_cooldownTimer = 0.0f;
                    m_cooldownState = CooldownState::FastScreening;
                    m_screeningTimer = static_cast<float>(m_screeningInterval);
                    m_screeningScoreSum = 0.0f;
                    m_screeningSampleSeconds = 0.0f;
                }
                break;

            case CooldownState::FastScreening:
                // 只累計在座時間，確保每輪快篩都有完整 5 分鐘資料
                m_screeningTimer -= dt;
                m_screeningScoreSum += m_lastScore * dt;
                m_screeningSampleSeconds += dt;
                if (m_screeningTimer <= 0.0f) {
                    float avg = (m_screeningSampleSeconds > 0.0f)
                        ? (m_screeningScoreSum / m_screeningSampleSeconds) : 0.0f;
                    if (avg >= m_severeThreshold) {
                        fireAlert = true;
                        alertScore = avg;
                        enterCooldownUnlocked();
                    } else if (avg < kAttentionThreshold) {
                        clearCooldownUnlocked();
                    } else {
                        m_screeningTimer = static_cast<float>(m_screeningInterval);
                        m_screeningScoreSum = 0.0f;
                        m_screeningSampleSeconds = 0.0f;
                    }
                }
                break;
            }
            snapshot = getStateUnlocked();
        }
        if (fireAlert) {
            callback = m_alertCallback;
        }
    }

    if (fireAlert && callback) {
        callback(FatigueLevel::SevereWarning, alertScore, kAlertMessage);
    }
    return snapshot;
}

SystemState FatigueStateMachine::advanceWithoutObservation(float deltaSeconds) {
    bool fireAlert = false;
    float alertScore = 0.0f;
    SystemState snapshot;
    AlertCallback callback;

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        const float dt = std::max(0.0f, deltaSeconds);
        m_severeHoldTimer = 0.0f;

        if (m_cooldownState == CooldownState::InCooldown) {
            m_cooldownTimer -= dt;
            if (m_cooldownTimer <= 0.0f) {
                m_cooldownTimer = 0.0f;
                m_cooldownState = CooldownState::FastScreening;
                m_screeningTimer = static_cast<float>(m_screeningInterval);
                m_screeningScoreSum = 0.0f;
                m_screeningSampleSeconds = 0.0f;
            }
        } else if (m_cooldownState == CooldownState::FastScreening) {
            m_screeningTimer -= dt;
            if (m_screeningTimer <= 0.0f) {
                m_screeningTimer = 0.0f;
                float avg = (m_screeningSampleSeconds > 0.0f)
                    ? (m_screeningScoreSum / m_screeningSampleSeconds) : 0.0f;
                if (m_screeningSampleSeconds > 0.0f && avg >= m_severeThreshold) {
                    fireAlert = true;
                    alertScore = avg;
                    enterCooldownUnlocked();
                } else if (m_screeningSampleSeconds > 0.0f && avg < kAttentionThreshold) {
                    clearCooldownUnlocked();
                } else {
                    // 重置進入下一輪 5 分鐘快篩
                    m_screeningTimer = static_cast<float>(m_screeningInterval);
                    m_screeningScoreSum = 0.0f;
                    m_screeningSampleSeconds = 0.0f;
                }
            }
        }
        snapshot = getStateUnlocked();
        if (fireAlert) {
            callback = m_alertCallback;
        }
    }

    if (fireAlert && callback) {
        callback(FatigueLevel::SevereWarning, alertScore, kAlertMessage);
    }
    return snapshot;
}

bool FatigueStateMachine::shouldCameraRun() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return shouldCameraRunUnlocked();
}

bool FatigueStateMachine::shouldCameraRunUnlocked() const {
    if (m_cooldownState == CooldownState::InCooldown) {
        return false;
    }
    if (m_cooldownState == CooldownState::FastScreening) {
        // 在 5 分鐘快篩期中，僅在最後 60 秒 (窗口後端採樣) 需要啟動相機
        const float sampleWindow = std::min(static_cast<float>(m_screeningInterval), kScreeningSampleSeconds);
        return m_screeningTimer <= sampleWindow;
    }
    return true; // NormalTracking 需要相機
}

void FatigueStateMachine::setAlertCallback(AlertCallback cb) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_alertCallback = std::move(cb);
}

SystemState FatigueStateMachine::getState() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return getStateUnlocked();
}

SystemState FatigueStateMachine::getStateUnlocked() const {
    SystemState state;
    state.fatigueLevel = m_currentLevel;
    state.cooldownState = m_cooldownState;
    state.currentFatigueScore = m_lastScore;
    state.smoothedFatigueScore = m_smoothedScore;
    state.cooldownRemainingSeconds = std::max(0, static_cast<int>(std::ceil(m_cooldownTimer)));
    state.screeningRemainingSeconds = (m_cooldownState == CooldownState::FastScreening)
        ? std::max(0, static_cast<int>(std::ceil(m_screeningTimer))) : 0;
    state.awaySeconds = static_cast<int>(m_awayTimer);
    state.studyDay = m_studyDay;
    state.isStudyLocked = m_isStudyLocked;
    state.userPresent = m_userPresent;
    state.shouldCameraRun = shouldCameraRunUnlocked();
    state.timestamp = std::chrono::system_clock::now();
    return state;
}

void FatigueStateMachine::reset() {
    std::lock_guard<std::mutex> lock(m_mutex);
    clearCooldownUnlocked();
    m_currentLevel = FatigueLevel::Relaxed;
    m_awayTimer = 0.0f;
    m_lastScore = 0.0f;
    m_smoothedScore = 0.0f;
    m_awayResetApplied = false;
}

void FatigueStateMachine::setStudyProgress(int currentDay, bool isLocked) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_studyDay = currentDay;
    m_isStudyLocked = isLocked;
}

std::string FatigueStateMachine::serialize(int64_t nowEpochSec) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::ostringstream oss;
    oss << "savedAt=" << nowEpochSec << "\n"
        << "state=" << static_cast<int>(m_cooldownState) << "\n"
        << "cooldown=" << m_cooldownTimer << "\n"
        << "screening=" << m_screeningTimer << "\n"
        << "screeningSum=" << m_screeningScoreSum << "\n"
        << "screeningSeconds=" << m_screeningSampleSeconds << "\n"
        << "smoothed=" << m_smoothedScore << "\n";
    return oss.str();
}

bool FatigueStateMachine::restore(const std::string& data, int64_t nowEpochSec) {
    int64_t savedAt = -1;
    int state = 0;
    float cooldown = 0.0f, screening = 0.0f, screeningSum = 0.0f, screeningSeconds = 0.0f, smoothed = 0.0f;

    std::istringstream iss(data);
    std::string line;
    try {
        while (std::getline(iss, line)) {
            size_t eq = line.find('=');
            if (eq == std::string::npos) continue;
            std::string key = line.substr(0, eq);
            std::string val = line.substr(eq + 1);
            if (key == "savedAt") savedAt = std::stoll(val);
            else if (key == "state") state = std::stoi(val);
            else if (key == "cooldown") cooldown = std::stof(val);
            else if (key == "screening") screening = std::stof(val);
            else if (key == "screeningSum") screeningSum = std::stof(val);
            else if (key == "screeningSeconds") screeningSeconds = std::stof(val);
            else if (key == "smoothed") smoothed = std::stof(val);
        }
    } catch (...) {
        return false;
    }
    if (savedAt < 0 || state < 0 || state > static_cast<int>(CooldownState::FastScreening)) {
        return false;
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    float elapsed = static_cast<float>(std::max<int64_t>(0, nowEpochSec - savedAt));
    if (elapsed >= static_cast<float>(m_awayResetThreshold)) {
        // 關閉期間已超過離座清零時間，比照離座規則
        clearCooldownUnlocked();
        m_smoothedScore = 0.0f;
        return true;
    }

    m_cooldownState = static_cast<CooldownState>(state);
    m_smoothedScore = std::clamp(smoothed, 0.0f, 100.0f);
    m_cooldownTimer = std::clamp(cooldown, 0.0f, static_cast<float>(m_cooldownDuration));
    m_screeningTimer = std::clamp(screening, 0.0f, static_cast<float>(m_screeningInterval));
    m_screeningScoreSum = std::max(0.0f, screeningSum);
    m_screeningSampleSeconds = std::max(0.0f, screeningSeconds);
    m_severeHoldTimer = 0.0f;

    if (m_cooldownState == CooldownState::InCooldown) {
        m_cooldownTimer -= elapsed;
        if (m_cooldownTimer <= 0.0f) {
            m_cooldownTimer = 0.0f;
            m_cooldownState = CooldownState::FastScreening;
            m_screeningTimer = static_cast<float>(m_screeningInterval);
            m_screeningScoreSum = 0.0f;
            m_screeningSampleSeconds = 0.0f;
        }
    }
    m_currentLevel = levelForScore(m_smoothedScore, m_severeThreshold);
    return true;
}

} // namespace efd
