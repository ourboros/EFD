#pragma once

#include "efd/types.hpp"
#include <vector>
#include <cstdint>
#include <chrono>

namespace efd {

enum class PixelFormat {
    RGB888,
    BGR888,
    RGBA8888,
    Grayscale
};

struct RawFrame {
    int width = 0;
    int height = 0;
    int channels = 3;
    PixelFormat format = PixelFormat::RGB888;
    std::vector<uint8_t> data;
    int64_t timestampMs = 0;

    bool isValid() const {
        return width > 0 && height > 0 && !data.empty() && 
               data.size() >= static_cast<size_t>(width * height * channels);
    }
};

struct LandmarkDetectionResult {
    bool hasFace = false;
    float faceConfidence = 0.0f;
    std::vector<Point3D> landmarks; // 468 點 3D 座標
    int64_t inferenceTimeMs = 0;
};

} // namespace efd

