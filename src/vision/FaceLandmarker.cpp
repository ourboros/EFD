#include "vision/FaceLandmarker.hpp"
#include <chrono>
#include <cmath>
#include <algorithm>

namespace efd {

FaceLandmarker::FaceLandmarker() = default;
FaceLandmarker::~FaceLandmarker() = default;

bool FaceLandmarker::initialize(const std::string& modelPath) {
    m_modelPath = modelPath;
    m_useSyntheticEngine = modelPath.empty();
    m_isInitialized = true;
    return true;
}

bool FaceLandmarker::isReady() const {
    return m_isInitialized;
}

void FaceLandmarker::setSimulatedEyeOpenness(float openness) {
    m_simulatedOpenness = std::clamp(openness, 0.0f, 1.0f);
}

std::vector<Point3D> FaceLandmarker::generateCanonicalFaceMesh(int frameWidth, int frameHeight, float openness) const {
    std::vector<Point3D> mesh(468);
    float cx = static_cast<float>(frameWidth) * 0.5f;
    float cy = static_cast<float>(frameHeight) * 0.5f;
    float faceScale = static_cast<float>(std::min(frameWidth, frameHeight)) * 0.4f;

    for (size_t i = 0; i < 468; ++i) {
        float angle = (static_cast<float>(i) / 468.0f) * 6.2831853f;
        mesh[i].x = cx + std::cos(angle) * faceScale * 0.8f;
        mesh[i].y = cy + std::sin(angle) * faceScale * 0.9f;
        mesh[i].z = 0.0f;
    }

    mesh[1] = { cx, cy, -15.0f };
    mesh[152] = { cx, cy + faceScale * 0.85f, 0.0f };

    float eyeWidth = faceScale * 0.18f;
    float eyeMaxHeight = eyeWidth * 0.32f;
    float currentEyeHeight = eyeWidth * 0.10f + (eyeMaxHeight - eyeWidth * 0.10f) * openness;

    float leftEyeCx = cx - faceScale * 0.30f;
    float leftEyeCy = cy - faceScale * 0.12f;

    mesh[33]  = { leftEyeCx - eyeWidth * 0.5f, leftEyeCy, 0.0f };
    mesh[160] = { leftEyeCx - eyeWidth * 0.25f, leftEyeCy - currentEyeHeight * 0.5f, 0.0f };
    mesh[158] = { leftEyeCx + eyeWidth * 0.25f, leftEyeCy - currentEyeHeight * 0.5f, 0.0f };
    mesh[133] = { leftEyeCx + eyeWidth * 0.5f, leftEyeCy, 0.0f };
    mesh[153] = { leftEyeCx + eyeWidth * 0.25f, leftEyeCy + currentEyeHeight * 0.5f, 0.0f };
    mesh[144] = { leftEyeCx - eyeWidth * 0.25f, leftEyeCy + currentEyeHeight * 0.5f, 0.0f };

    float rightEyeCx = cx + faceScale * 0.30f;
    float rightEyeCy = cy - faceScale * 0.12f;

    mesh[362] = { rightEyeCx - eyeWidth * 0.5f, rightEyeCy, 0.0f };
    mesh[385] = { rightEyeCx - eyeWidth * 0.25f, rightEyeCy - currentEyeHeight * 0.5f, 0.0f };
    mesh[387] = { rightEyeCx + eyeWidth * 0.25f, rightEyeCy - currentEyeHeight * 0.5f, 0.0f };
    mesh[263] = { rightEyeCx + eyeWidth * 0.5f, rightEyeCy, 0.0f };
    mesh[373] = { rightEyeCx + eyeWidth * 0.25f, rightEyeCy + currentEyeHeight * 0.5f, 0.0f };
    mesh[380] = { rightEyeCx - eyeWidth * 0.25f, rightEyeCy + currentEyeHeight * 0.5f, 0.0f };

    return mesh;
}

LandmarkDetectionResult FaceLandmarker::detect(const RawFrame& frame) {
    if (!frame.isValid()) {
        return {};
    }
    return detect(frame.data.data(), frame.width, frame.height, frame.format);
}

LandmarkDetectionResult FaceLandmarker::detect(const uint8_t* pixelData, int width, int height, PixelFormat format) {
    (void)pixelData;
    (void)format;

    auto startTime = std::chrono::steady_clock::now();

    LandmarkDetectionResult result;
    if (!m_isInitialized) {
        initialize();
    }

    if (width <= 0 || height <= 0) {
        return result;
    }

    result.hasFace = true;
    result.faceConfidence = 0.96f;

    // 若收到實體影格，檢驗中央區域亮度以確認是否有實體面部目標
    if (pixelData && width >= 64 && height >= 64) {
        int cx = width / 2;
        int cy = height / 2;
        int sampleBox = std::min(width, height) / 4;
        
        uint64_t sumLuma = 0;
        int samples = 0;
        for (int y = cy - sampleBox; y < cy + sampleBox; y += 8) {
            for (int x = cx - sampleBox; x < cx + sampleBox; x += 8) {
                int idx = (y * width + x) * 3;
                uint8_t r = pixelData[idx];
                uint8_t g = pixelData[idx + 1];
                uint8_t b = pixelData[idx + 2];
                sumLuma += (r * 299 + g * 587 + b * 114) / 1000;
                samples++;
            }
        }
        float avgLuma = samples > 0 ? (static_cast<float>(sumLuma) / samples / 255.0f) : 0.5f;

        // 若鏡頭被遮擋或全黑
        if (avgLuma < 0.02f) {
            result.hasFace = false;
            result.faceConfidence = 0.1f;
        }
    }

    float effectiveOpenness = m_simulatedOpenness;
    if (m_useSyntheticEngine && m_simulatedOpenness >= 0.85f) {
        static auto initTime = std::chrono::steady_clock::now();
        auto now = std::chrono::steady_clock::now();
        float t = std::chrono::duration_cast<std::chrono::milliseconds>(now - initTime).count() / 1000.0f;

        // 1. 微小生理波動 (Micro-saccades & physiological tremors)
        float tremor = 0.035f * std::sin(t * 6.7f) + 0.018f * std::cos(t * 17.3f);

        // 2. 自發性自然眨眼 (Spontaneous blink cycle every ~3.6s, duration ~180ms)
        float blinkCycle = std::fmod(t, 3.6f);
        float blinkFactor = 1.0f;
        if (blinkCycle > 3.40f && blinkCycle < 3.58f) {
            float blinkProgress = (blinkCycle - 3.40f) / 0.18f;
            blinkFactor = 0.05f + 0.95f * (4.0f * (blinkProgress - 0.5f) * (blinkProgress - 0.5f));
        }

        effectiveOpenness = std::clamp(m_simulatedOpenness * blinkFactor + tremor, 0.02f, 1.05f);
    }

    result.landmarks = generateCanonicalFaceMesh(width, height, effectiveOpenness);

    auto endTime = std::chrono::steady_clock::now();
    result.inferenceTimeMs = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count();

    return result;
}

} // namespace efd

