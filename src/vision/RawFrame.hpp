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
    bool isSynthetic = false;

    bool isValid() const {
        return width > 0 && height > 0 && !data.empty() && 
               data.size() >= static_cast<size_t>(width * height * channels);
    }
};

struct LandmarkDetectionResult {
    bool hasFace = false;
    float faceConfidence = 0.0f;
    std::vector<Point3D> landmarks; // 468 點 3D 座標 (像素座標，MediaPipe 索引)
    int64_t inferenceTimeMs = 0;
    // true：特徵點由模型量測 (MediaPipe / ONNX)；false：啟發式偵測器依開合度推算的網格
    bool landmarksMeasured = false;
    // MediaPipe blendshape 眨眼分數 (0 睜 ~ 1 閉)；負值代表偵測器未提供
    float eyeBlinkLeft = -1.0f;
    float eyeBlinkRight = -1.0f;
};

} // namespace efd

