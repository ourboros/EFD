#pragma once

#include "efd/types.hpp"
#include "vision/RawFrame.hpp"
#include <string>
#include <vector>
#include <memory>

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
        std::string unconfirmedReason;
    };

    PresenceDiagnostic getLatestPresenceDiagnostic() const;

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
    bool m_isFaceConfirmed = false;
    PresenceDiagnostic m_lastDiag;

    std::vector<Point3D> generateCanonicalFaceMesh(int frameWidth, int frameHeight, float openness, float customCx = 0.0f, float customCy = 0.0f, float customScale = 0.0f) const;
};

} // namespace efd

