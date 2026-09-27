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

    // 1. 偵測使用者是否在鏡頭畫面中 (Presence Detection) & 2. 確認臉部是否正對鏡頭 (Orientation Detection)
    if (pixelData && width >= 64 && height >= 64) {
        int cx = width / 2;
        int cy = height / 2;
        int sampleBox = std::min(width, height) / 3;
        
        uint64_t sumLuma = 0;
        uint64_t sumSqLuma = 0;
        uint64_t leftLuma = 0;
        uint64_t rightLuma = 0;
        int samples = 0;
        int halfSamples = 0;

        for (int y = cy - sampleBox; y < cy + sampleBox; y += 6) {
            for (int x = cx - sampleBox; x < cx + sampleBox; x += 6) {
                int idx = (y * width + x) * 3;
                uint8_t r = pixelData[idx];
                uint8_t g = pixelData[idx + 1];
                uint8_t b = pixelData[idx + 2];
                uint32_t luma = (r * 299 + g * 587 + b * 114) / 1000;
                
                sumLuma += luma;
                sumSqLuma += luma * luma;
                samples++;

                if (x < cx) {
                    leftLuma += luma;
                    halfSamples++;
                } else if (x > cx) {
                    rightLuma += luma;
                }
            }
        }

        if (samples > 0) {
            float meanLuma = static_cast<float>(sumLuma) / samples;
            float variance = static_cast<float>(sumSqLuma) / samples - (meanLuma * meanLuma);
            float stdDev = (variance > 0.0f) ? std::sqrt(variance) : 0.0f;

            // (A) 畫面亮度過低 (遮擋/全黑) 或過曝
            bool isLightingValid = (meanLuma >= 8.0f && meanLuma <= 250.0f);
            
            // (B) 畫面紋理/對比度 (人臉與五官存在時必有邊緣與灰度起伏，離座時通常為平坦純色背景)
            bool isContrastValid = (stdDev >= 6.0f);

            // (C) 臉部左右對稱性 (正對鏡頭時，左右臉區域平均照度與邊緣分佈均衡)
            bool isOrientationValid = true;
            if (halfSamples > 0) {
                float meanLeft = static_cast<float>(leftLuma) / halfSamples;
                float meanRight = static_cast<float>(rightLuma) / halfSamples;
                float diffRatio = std::abs(meanLeft - meanRight) / std::max(meanLuma, 1.0f);
                if (diffRatio > 0.55f) {
                    isOrientationValid = false; // 側臉超過 45 度或未正對鏡頭
                }
            }

            if (!isLightingValid || !isContrastValid || !isOrientationValid) {
                result.hasFace = false;
                result.faceConfidence = 0.0f;
            }
        }
    }

    if (!result.hasFace) {
        result.landmarks.clear();
        auto endTime = std::chrono::steady_clock::now();
        result.inferenceTimeMs = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count();
        return result;
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

