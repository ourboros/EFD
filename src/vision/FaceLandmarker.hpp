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

    std::vector<Point3D> generateCanonicalFaceMesh(int frameWidth, int frameHeight, float openness) const;
};

} // namespace efd

