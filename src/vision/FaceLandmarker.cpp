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

    bool faceDetected = true;
    float confidence = 0.95f;

    // 實體影像人臉與朝向偵測 (Person Present -> Facing Camera -> Face Detected)
    if (pixelData && width >= 64 && height >= 64) {
        int step = std::max(2, width / 80); // 快速取樣步長
        int skinPixels = 0;
        int totalSampled = 0;
        int minX = width, maxX = 0, minY = height, maxY = 0;
        uint64_t skinSumX = 0, skinSumY = 0;

        // 偵測中央與上半部人臉區域 (X: 15%~85%, Y: 10%~90%)
        int startX = width * 15 / 100;
        int endX = width * 85 / 100;
        int startY = height * 10 / 100;
        int endY = height * 90 / 100;

        for (int y = startY; y < endY; y += step) {
            const uint8_t* row = pixelData + (y * width * 3);
            for (int x = startX; x < endX; x += step) {
                int idx = x * 3;
                uint8_t r = row[idx];
                uint8_t g = row[idx + 1];
                uint8_t b = row[idx + 2];
                totalSampled++;

                // YCbCr 膚色檢測公式
                int yVal = (299 * r + 587 * g + 114 * b) / 1000;
                int cbVal = (-169 * r - 331 * g + 500 * b) / 1000 + 128;
                int crVal = (500 * r - 419 * g - 81 * b) / 1000 + 128;

                // 標準人體膚色色彩學區間：Y > 35, 75 <= Cb <= 130, 130 <= Cr <= 178 且 R > G > B
                bool isSkin = (yVal >= 35 && yVal <= 235) &&
                              (cbVal >= 75 && cbVal <= 130) &&
                              (crVal >= 130 && crVal <= 178) &&
                              (r > g) && (g >= b - 15);

                if (isSkin) {
                    skinPixels++;
                    skinSumX += x;
                    skinSumY += y;
                    minX = std::min(minX, x);
                    maxX = std::max(maxX, x);
                    minY = std::min(minY, y);
                    maxY = std::max(maxY, y);
                }
            }
        }

        float skinRatio = (totalSampled > 0) ? (static_cast<float>(skinPixels) / totalSampled) : 0.0f;

        // 階段 1：判定畫面中是否有活體人臉 (膚色比例需達到至少 3.0% 且像素點足夠)
        if (skinRatio < 0.030f || skinPixels < 25) {
            faceDetected = false;
            confidence = 0.0f;
        } else {
            // 階段 2：確認臉部是否正對攝影機 (朝向判定)
            float centroidX = static_cast<float>(skinSumX) / skinPixels / width;
            float centroidY = static_cast<float>(skinSumY) / skinPixels / height;
            int faceBoxW = maxX - minX;
            int faceBoxH = maxY - minY;
            float boxAspect = (faceBoxH > 0) ? (static_cast<float>(faceBoxW) / faceBoxH) : 0.0f;

            // 條件 A: 重心必須在畫面中段 (X 落在 20%~80%, Y 落在 15%~85%)
            bool isCentered = (centroidX >= 0.20f && centroidX <= 0.80f) &&
                              (centroidY >= 0.15f && centroidY <= 0.85f);

            // 條件 B: 臉部長寬比符合人臉幾何 (寬高比 0.40 ~ 1.70)
            bool isFaceGeometry = (boxAspect >= 0.40f && boxAspect <= 1.70f);

            if (isCentered && isFaceGeometry) {
                faceDetected = true;
                confidence = std::clamp(skinRatio * 8.0f, 0.75f, 0.98f);
            } else {
                // 偏離中心或側臉轉向過大 (未正對鏡頭)
                faceDetected = false;
                confidence = 0.15f;
            }
        }
    }

    result.hasFace = faceDetected;
    result.faceConfidence = confidence;

    // 若未偵測到人臉或未正對鏡頭，清空特徵點並直接返回
    if (!faceDetected) {
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

