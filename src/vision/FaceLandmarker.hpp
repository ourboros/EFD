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
    LandmarkDetectionResult detect(const uint8_t* pixelData, int width, int height, PixelFormat format = PixelFormat::RGB888);
    bool isReady() const;
    void setSimulatedEyeOpenness(float openness);

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
    float m_measuredEyeOpenness = 0.85f;

    // 動態背景模型與幀差分析 (用於無人環境與運動人體偵測)
    std::vector<uint8_t> m_bgGray;
    int m_bgWidth = 0;
    int m_bgHeight = 0;
    int m_bgFrameCount = 0;

    std::vector<Point3D> generateCanonicalFaceMesh(int frameWidth, int frameHeight, float openness, float customCx = 0.0f, float customCy = 0.0f, float customScale = 0.0f) const;
    float estimateEyeOpennessFromROI(const uint8_t* pixelData, int width, int height, float eyeCx, float eyeCy, float eyeW, float eyeH) const;
};

} // namespace efd

