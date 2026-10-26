#pragma once

#include "efd/types.hpp"
#include "vision/RawFrame.hpp"
#include <string>
#include <vector>
#include <memory>
#include <deque>
#include <cstdint>

namespace efd {

class FaceLandmarker {
public:
    FaceLandmarker();
    ~FaceLandmarker();

    bool initialize(const std::string& modelPath = "");
    LandmarkDetectionResult detect(const RawFrame& frame);
    LandmarkDetectionResult detect(const uint8_t* pixelData, int width, int height, PixelFormat format = PixelFormat::RGB888, bool isSynthetic = false);
    bool isReady() const;
    void setSimulatedEyeOpenness(float openness);

    struct PresenceDiagnostic {
        bool rawPersonInFrame = false;
        bool rawFacingCamera = false;
        bool isFaceConfirmed = false;
        int skinPixels = 0;
        int minSkinRequired = 0;
        float fillDensity = 0.0f;
        float aspectRatio = 0.0f;
        float symmetryRatio = 0.0f;
        int boxX = 0;
        int boxY = 0;
        int boxW = 0;
        int boxH = 0;
        int frameWidth = 640;
        int frameHeight = 480;
        int stableCount = 0;
        float trackedCx = 0.0f;
        float trackedCy = 0.0f;
        float userBiasX = 0.0f;
        float userBiasY = 0.0f;
        bool isPositionBiasCalibrated = false;
        std::string unconfirmedReason;

        // 診斷模式用：本幀卡在哪一關 ("" 代表全部通過；skin / size / aspect / fill / eyes / symmetry)
        std::string failStage;
        // 眼睛或對稱檢查未過，但位置與已鎖定的臉相符而判定在座 (閉眼、低頭、眨眼)
        bool presenceByTracking = false;
        // 膚色連通區域 (未裁切) 與裁切後的臉部方框
        int skinBoxX = 0, skinBoxY = 0, skinBoxW = 0, skinBoxH = 0;
        int faceBoxX = 0, faceBoxY = 0, faceBoxW = 0, faceBoxH = 0;
        int avgSkinLum = 0;
        // 眼部檢查數值 [0] 左 / [1] 右，以及推算的眼睛中心
        float eyeDarkFraction[2] = {0.0f, 0.0f};
        float eyeContrast[2] = {0.0f, 0.0f};
        float eyeGradient[2] = {0.0f, 0.0f};
        int eyeCenterX[2] = {0, 0};
        int eyeCenterY = 0;
    };

    PresenceDiagnostic getLatestPresenceDiagnostic() const;

    // 初次測試/校準：記錄並鎖定使用者常態偏向位置
    void calibrateUserPositionBias(float cx, float cy);
    bool isPositionBiasCalibrated() const;
    void resetTracking();

    // 重置睜眼暗部基準 (重新校準時呼叫)
    void resetEyeBaseline();

    // 連續多久未偵測到人臉才判定離開 (以時間計，與幀率無關)
    static constexpr int64_t kFaceLostTimeoutMs = 1000;
    // 睜眼暗部基準取最近 60 秒暗部比例的第 90 百分位
    static constexpr int64_t kEyeBaselineWindowMs = 60000;

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

    float updateOpenEyeBaseline(float darkRatio, int64_t nowMs);

    std::vector<Point3D> generateCanonicalFaceMesh(int frameWidth, int frameHeight, float openness, float customCx = 0.0f, float customCy = 0.0f, float customScale = 0.0f) const;
};

} // namespace efd

