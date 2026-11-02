#pragma once

#include "efd/types.hpp"
#include "vision/RawFrame.hpp"
#include "vision/ILandmarkDetector.hpp"
#include <string>
#include <vector>
#include <memory>
#include <deque>
#include <cstdint>

namespace efd {

// 啟發式偵測器：膚色網格定位臉部、眼部暗點比例量測開合度，再依開合度產生 468 點網格
// (網格是推算的，不是量測的)。不需模型，作為模型無法載入時的備援
class FaceLandmarker : public ILandmarkDetector {
public:
    // 沿用舊名稱，既有呼叫端 (JNI、測試) 不需修改
    using PresenceDiagnostic = efd::PresenceDiagnostic;

    FaceLandmarker();
    ~FaceLandmarker() override;

    bool initialize(const std::string& modelPath = "") override;
    const char* name() const override { return "heuristic"; }
    LandmarkDetectionResult detect(const RawFrame& frame) override;
    LandmarkDetectionResult detect(const uint8_t* pixelData, int width, int height, PixelFormat format = PixelFormat::RGB888, bool isSynthetic = false);
    bool isReady() const override;
    void setSimulatedEyeOpenness(float openness) override;

    PresenceDiagnostic getLatestPresenceDiagnostic() const override;

    // 初次測試/校準：記錄並鎖定使用者常態偏向位置
    void calibrateUserPositionBias(float cx, float cy) override;
    bool isPositionBiasCalibrated() const override;
    void resetTracking() override;

    // 重置睜眼暗部基準 (重新校準時呼叫)
    void resetEyeBaseline() override;

    // 連續多久未偵測到人臉才判定離開 (以時間計，與幀率無關)
    static constexpr int64_t kFaceLostTimeoutMs = 1000;
    // 睜眼暗部基準取最近 60 秒「確認睜眼」幀暗部比例的中位數 (至少 15 個樣本才更新)
    static constexpr int64_t kEyeBaselineWindowMs = 60000;
    static constexpr size_t kEyeBaselineMinSamples = 15;

private:
    bool m_isInitialized = false;
    bool m_useSyntheticEngine = true;
    float m_simulatedOpenness = 1.0f;
    std::string m_modelPath;

    int m_consecutiveFaceFrames = 0;
    int m_consecutiveMissingFrames = 0;
    float m_lastKnownCx = 0.0f;
    float m_lastKnownCy = 0.0f;
    float m_lastKnownScale = 0.0f;
    float m_trackedCx = 0.0f;
    float m_trackedCy = 0.0f;
    float m_trackedScale = 0.0f;
    float m_userBiasX = 0.0f;
    float m_userBiasY = 0.0f;
    bool m_isPositionBiasCalibrated = false;
    bool m_isFaceConfirmed = false;
    int64_t m_lastFaceSeenMs = 0;
    PresenceDiagnostic m_lastDiag;

    struct DarkRatioSample {
        int64_t timestampMs = 0;
        float ratio = 0.0f;
    };
    std::deque<DarkRatioSample> m_darkRatioHistory;
    float m_openEyeDarkBaseline = 0.18f;
    std::deque<float> m_recentOpenness;   // 最近 3 幀的開合度 (中位數濾波)
    int m_baselineStarvedFrames = 0;      // 睜眼基準樣本未滿的連續幀數

    float updateOpenEyeBaseline(float darkRatio, int64_t nowMs, bool eyesVerifiedOpen);

    std::vector<Point3D> generateCanonicalFaceMesh(int frameWidth, int frameHeight, float openness, float customCx = 0.0f, float customCy = 0.0f, float customScale = 0.0f) const;
};

} // namespace efd

