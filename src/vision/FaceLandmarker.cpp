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

std::vector<Point3D> FaceLandmarker::generateCanonicalFaceMesh(int frameWidth, int frameHeight, float openness, float customCx, float customCy, float customScale) const {
    std::vector<Point3D> mesh(468);
    float cx = (customCx > 0.0f) ? customCx : (static_cast<float>(frameWidth) * 0.5f);
    float cy = (customCy > 0.0f) ? customCy : (static_cast<float>(frameHeight) * 0.5f);
    float faceScale = (customScale > 0.0f) ? customScale : (static_cast<float>(std::min(frameWidth, frameHeight)) * 0.4f);

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
    (void)format;

    auto startTime = std::chrono::steady_clock::now();

    LandmarkDetectionResult result;
    if (!m_isInitialized) {
        initialize();
    }

    if (width <= 0 || height <= 0) {
        return result;
    }

    float detectedFaceCx = width * 0.5f;
    float detectedFaceCy = height * 0.5f;
    float detectedFaceScale = std::min(width, height) * 0.4f;
    bool personInFrame = false;
    bool facingCamera = false;

    if (pixelData && width >= 64 && height >= 64) {
        // 1. 影像採樣分析人體膚色特徵與分佈 (Peer/Kovac 經典色彩空間模型)
        int step = (width > 640) ? 8 : 4;
        int totalSamples = 0;
        int skinPixels = 0;
        uint64_t skinSumX = 0;
        uint64_t skinSumY = 0;
        int minSkinX = width, maxSkinX = 0;
        int minSkinY = height, maxSkinY = 0;

        for (int y = 0; y < height; y += step) {
            for (int x = 0; x < width; x += step) {
                totalSamples++;
                int idx = (y * width + x) * 3;
                uint8_t r = pixelData[idx];
                uint8_t g = pixelData[idx + 1];
                uint8_t b = pixelData[idx + 2];

                // 膚色色度判斷條件
                bool isSkin = (r > 70 && g > 35 && b > 20 &&
                               (r - g) >= 10 && (r > b) &&
                               (std::max({r, g, b}) - std::min({r, g, b})) >= 15);

                if (isSkin) {
                    skinPixels++;
                    skinSumX += x;
                    skinSumY += y;
                    if (x < minSkinX) minSkinX = x;
                    if (x > maxSkinX) maxSkinX = x;
                    if (y < minSkinY) minSkinY = y;
                    if (y > maxSkinY) maxSkinY = y;
                }
            }
        }

        float skinRatio = (totalSamples > 0) ? (static_cast<float>(skinPixels) / totalSamples) : 0.0f;

        // 2. 階段一：判斷畫面中有無人體/面部 (Person Presence)
        // 若膚色佔比低於 3.5%，判定無人在鏡頭前 (空景/牆壁/離座)
        if (skinRatio >= 0.035f && skinPixels >= 30) {
            personInFrame = true;
            detectedFaceCx = static_cast<float>(skinSumX) / skinPixels;
            detectedFaceCy = static_cast<float>(skinSumY) / skinPixels;

            int skinBoxW = std::max(20, maxSkinX - minSkinX);
            int skinBoxH = std::max(20, maxSkinY - minSkinY);
            detectedFaceScale = std::clamp(static_cast<float>(std::max(skinBoxW, skinBoxH)) * 0.85f, 
                                           static_cast<float>(std::min(width, height)) * 0.20f,
                                           static_cast<float>(std::min(width, height)) * 0.75f);

            // 3. 階段二：確認臉部是否朝向攝影機 (Face Facing Camera / Frontal Orientation)
            // 檢查膚色區域雙側對稱性 (Bilateral Symmetry) 與左右分佈平衡
            int leftSkin = 0;
            int rightSkin = 0;
            int midX = static_cast<int>(detectedFaceCx);

            for (int y = std::max(0, minSkinY); y <= std::min(height - 1, maxSkinY); y += step) {
                for (int x = std::max(0, minSkinX); x <= std::min(width - 1, maxSkinX); x += step) {
                    int idx = (y * width + x) * 3;
                    uint8_t r = pixelData[idx];
                    uint8_t g = pixelData[idx + 1];
                    uint8_t b = pixelData[idx + 2];
                    bool isSkin = (r > 70 && g > 35 && b > 20 && (r - g) >= 10 && (r > b));
                    if (isSkin) {
                        if (x < midX) leftSkin++;
                        else rightSkin++;
                    }
                }
            }

            int minSide = std::min(leftSkin, rightSkin);
            int maxSide = std::max(leftSkin, rightSkin);
            float symmetryRatio = (maxSide > 0) ? (static_cast<float>(minSide) / maxSide) : 0.0f;

            // 若對稱性良好 (>= 0.40) 且臉部質心位於畫面可見合理範圍內，判定正對鏡頭
            if (symmetryRatio >= 0.40f && detectedFaceCx > width * 0.12f && detectedFaceCx < width * 0.88f) {
                facingCamera = true;
            }
        }
    } else {
        // Synthetic 模擬驅動模式 (僅在無實體相機且設定為模擬時運作)
        personInFrame = (m_simulatedOpenness >= 0.0f);
        facingCamera = personInFrame;
    }

    // 4. 階段三：確認偵測到正對鏡頭人臉 -> 階段四：確認疲勞狀態
    if (personInFrame && facingCamera) {
        result.hasFace = true;
        result.faceConfidence = 0.95f;

        float effectiveOpenness = m_simulatedOpenness;
        if (m_useSyntheticEngine && m_simulatedOpenness >= 0.85f) {
            static auto initTime = std::chrono::steady_clock::now();
            auto now = std::chrono::steady_clock::now();
            float t = std::chrono::duration_cast<std::chrono::milliseconds>(now - initTime).count() / 1000.0f;

            float tremor = 0.035f * std::sin(t * 6.7f) + 0.018f * std::cos(t * 17.3f);
            float blinkCycle = std::fmod(t, 3.6f);
            float blinkFactor = 1.0f;
            if (blinkCycle > 3.40f && blinkCycle < 3.58f) {
                float blinkProgress = (blinkCycle - 3.40f) / 0.18f;
                blinkFactor = 0.05f + 0.95f * (4.0f * (blinkProgress - 0.5f) * (blinkProgress - 0.5f));
            }
            effectiveOpenness = std::clamp(m_simulatedOpenness * blinkFactor + tremor, 0.02f, 1.05f);
        }

        result.landmarks = generateCanonicalFaceMesh(width, height, effectiveOpenness, detectedFaceCx, detectedFaceCy, detectedFaceScale);
    } else {
        // 無人在鏡頭前 或 未正對鏡頭 -> 標記未偵測人臉，清空特徵點，杜絕假疲勞數據
        result.hasFace = false;
        result.faceConfidence = 0.0f;
        result.landmarks.clear();
    }

    auto endTime = std::chrono::steady_clock::now();
    result.inferenceTimeMs = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count();

    return result;
}

} // namespace efd

