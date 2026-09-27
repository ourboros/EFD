#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <array>
#include <chrono>

namespace efd {

// 3D Point representation for facial landmarks
struct Point3D {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

struct Point2D {
    float x = 0.0f;
    float y = 0.0f;
};

// Canonical MediaPipe 468/478 Face Landmark 6-point indices for Eye Aspect Ratio (EAR)
// 6 points: [p1: outer, p2: top-outer, p3: top-inner, p4: inner, p5: bottom-inner, p6: bottom-outer]
struct EyeLandmarkIndices {
    static constexpr std::array<int, 6> LEFT_EYE  = { 33, 160, 158, 133, 153, 144 };
    static constexpr std::array<int, 6> RIGHT_EYE = { 362, 385, 387, 263, 373, 380 };
};

// 幾何特徵指標 (Geometric Eye Metrics)
struct EyeMetrics {
    float earLeft = 0.0f;
    float earRight = 0.0f;
    float earAvg = 0.0f;
    float perclos = 0.0f;          // Percentage of Eye Closure (0.0 ~ 1.0)
    int   blinkCount = 0;          // Total blinks in current measurement session
    float blinkRatePerMin = 0.0f;  // Estimated blinks per minute
    float avgBlinkDurationMs = 0.0f; // Average blink duration in milliseconds
    bool  isEyeClosed = false;
};

// 訊號複雜度特徵 (Complexity & Nonlinear Dynamics Metrics)
struct ComplexityMetrics {
    std::vector<float> sampleEntropyScales; // Multiscale Entropy for scales tau = 1..N
    float complexityIndex = 0.0f;          // Sum of Multiscale Entropy (CI)
    float emdEnergyRatio = 0.0f;           // High-frequency vs Low-frequency IMF energy ratio
    int   imfCount = 0;                    // Number of extracted Intrinsic Mode Functions
};

// 疲勞分級
enum class FatigueLevel : uint8_t {
    Relaxed = 0,       // 正常 / 清醒
    Attention = 1,     // 輕度疲勞 / 提醒
    SevereWarning = 2, // 重度疲勞 / 警報升級
    UserAway = 3       // 使用者離座
};

// 20/5/5 防打擾狀態機之運行狀態
enum class CooldownState : uint8_t {
    NormalTracking,    // 正常全量追蹤
    InCooldown,        // 20 分鐘冷卻中 (防頻繁警報)
    FastScreening,     // 5 分鐘輕量快篩中 (僅 PERCLOS/眨眼率)
    AwayPaused         // 離座暫停倒數中
};

// 系統綜合狀態快照
struct SystemState {
    FatigueLevel fatigueLevel = FatigueLevel::Relaxed;
    CooldownState cooldownState = CooldownState::NormalTracking;
    float currentFatigueScore = 0.0f; // 0.0 ~ 100.0
    int cooldownRemainingSeconds = 0;
    int awaySeconds = 0;
    int studyDay = 1;                 // 1 ~ 14
    bool isStudyLocked = false;       // 14 天實驗期滿鎖定
    std::chrono::system_clock::time_point timestamp;
};

// 校準資料結構
struct CalibrationData {
    float baselineEar = 0.30f;
    float earStdDev = 0.03f;
    float thresholdEar = 0.21f;
    bool isCalibrated = false;
};

} // namespace efd

