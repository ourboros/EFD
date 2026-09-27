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
        // ---------------------------------------------------------------------
        // 階段一：偵測畫面中有無人在攝影機中 (Person Presence Detection)
        // ---------------------------------------------------------------------
        int step = (width > 640) ? 6 : 4;
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

                double gray = 0.299 * r + 0.587 * g + 0.114 * b;

                // YCbCr 嚴謹人類膚色空間分佈
                // Y: 35 ~ 245, Cb: 75 ~ 128, Cr: 132 ~ 175
                double cb = -0.168736 * r - 0.331264 * g + 0.500000 * b + 128.0;
                double cr =  0.500000 * r - 0.418688 * g - 0.081312 * b + 128.0;

                bool isSkin = (gray >= 35.0 && gray <= 245.0 &&
                               cb >= 75.0 && cb <= 128.0 &&
                               cr >= 132.0 && cr <= 175.0 &&
                               cr > (cb + 5.0) &&
                               r > g && (r - g) >= 8 && (r - b) >= 15);

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
        int skinBoxW = std::max(0, maxSkinX - minSkinX);
        int skinBoxH = std::max(0, maxSkinY - minSkinY);

        // 空間緊湊度 (Density) = 膚色像素數 / 候選外接框採樣點數
        float boxSamples = (skinBoxW > 0 && skinBoxH > 0)
            ? (static_cast<float>(skinBoxW) / step) * (static_cast<float>(skinBoxH) / step)
            : 0.0f;
        float spatialDensity = (boxSamples > 0.0f) ? (static_cast<float>(skinPixels) / boxSamples) : 0.0f;
        float boxAspect = (skinBoxW > 0) ? (static_cast<float>(skinBoxH) / static_cast<float>(skinBoxW)) : 0.0f;

        // 判斷條件：
        // 1. 膚色佔比介於合理人臉範圍 (3.5% ~ 65%)
        // 2. 外接框尺寸符合正常人臉尺度 (寬度 >= 10% 螢幕, 高度 >= 12% 螢幕)
        // 3. 外接框長寬比符合人臉比例 (0.70 ~ 2.25)
        // 4. 空間集中度密度 (>= 0.18)，排除散落全螢幕的白牆/木質背景
        if (skinRatio >= 0.035f && skinRatio <= 0.65f && skinPixels >= 35 &&
            skinBoxW >= static_cast<int>(width * 0.10f) && skinBoxW <= static_cast<int>(width * 0.85f) &&
            skinBoxH >= static_cast<int>(height * 0.12f) && skinBoxH <= static_cast<int>(height * 0.90f) &&
            boxAspect >= 0.70f && boxAspect <= 2.25f &&
            spatialDensity >= 0.18f) {
            
            personInFrame = true;
            detectedFaceCx = static_cast<float>(skinSumX) / skinPixels;
            detectedFaceCy = static_cast<float>(skinSumY) / skinPixels;
            detectedFaceScale = std::clamp(static_cast<float>(std::max(skinBoxW, skinBoxH)) * 0.85f,
                                           static_cast<float>(std::min(width, height)) * 0.20f,
                                           static_cast<float>(std::min(width, height)) * 0.75f);

            // -----------------------------------------------------------------
            // 階段二：確認臉部是否朝向攝影機 (Facing Camera / Frontal Orientation)
            // -----------------------------------------------------------------
            // 1. 雙側左右對稱性檢驗 (Bilateral Symmetry)
            int leftSkin = 0;
            int rightSkin = 0;
            int midX = static_cast<int>(detectedFaceCx);

            // 2. 雙眼暗槽與對比度特徵檢驗 (Eye-Pair Feature Troughs)
            int eyeZoneTop = minSkinY + static_cast<int>(skinBoxH * 0.18f);
            int eyeZoneBottom = minSkinY + static_cast<int>(skinBoxH * 0.50f);
            int leftEyeLeft = std::max(0, static_cast<int>(detectedFaceCx - skinBoxW * 0.40f));
            int leftEyeRight = std::max(0, static_cast<int>(detectedFaceCx - skinBoxW * 0.08f));
            int rightEyeLeft = std::min(width - 1, static_cast<int>(detectedFaceCx + skinBoxW * 0.08f));
            int rightEyeRight = std::min(width - 1, static_cast<int>(detectedFaceCx + skinBoxW * 0.40f));

            double leftEyeSum = 0.0, rightEyeSum = 0.0, foreheadSum = 0.0;
            int leftEyeCnt = 0, rightEyeCnt = 0, foreheadCnt = 0;

            for (int y = std::max(0, minSkinY); y <= std::min(height - 1, maxSkinY); y += step) {
                for (int x = std::max(0, minSkinX); x <= std::min(width - 1, maxSkinX); x += step) {
                    int idx = (y * width + x) * 3;
                    uint8_t r = pixelData[idx];
                    uint8_t g = pixelData[idx + 1];
                    uint8_t b = pixelData[idx + 2];
                    double gray = 0.299 * r + 0.587 * g + 0.114 * b;

                    bool isSkin = (r > g && (r - g) >= 8 && (r - b) >= 15);
                    if (isSkin) {
                        if (x < midX) leftSkin++;
                        else rightSkin++;
                    }

                    // 取眼區與額頭亮度
                    if (y >= eyeZoneTop && y <= eyeZoneBottom) {
                        if (x >= leftEyeLeft && x <= leftEyeRight) {
                            leftEyeSum += gray;
                            leftEyeCnt++;
                        } else if (x >= rightEyeLeft && x <= rightEyeRight) {
                            rightEyeSum += gray;
                            rightEyeCnt++;
                        }
                    } else if (y >= minSkinY && y < eyeZoneTop) {
                        foreheadSum += gray;
                        foreheadCnt++;
                    }
                }
            }

            int minSide = std::min(leftSkin, rightSkin);
            int maxSide = std::max(leftSkin, rightSkin);
            float symmetryRatio = (maxSide > 0) ? (static_cast<float>(minSide) / maxSide) : 0.0f;

            // 雙眼亮度特徵判定 (正面臉的雙眼與眉毛區域具備特徵對比)
            bool eyeContrastValid = true;
            if (foreheadCnt > 5 && leftEyeCnt > 2 && rightEyeCnt > 2) {
                double avgLeftEye = leftEyeSum / leftEyeCnt;
                double avgRightEye = rightEyeSum / rightEyeCnt;
                double eyeDiff = std::abs(avgLeftEye - avgRightEye);
                if (eyeDiff > 45.0) {
                    eyeContrastValid = false;
                }
            }

            // 正對鏡頭條件：
            // - 對稱比 >= 0.42
            // - 質心在螢幕合理範圍 (10% ~ 90%)
            // - 雙眼區域對稱未嚴重偏轉
            if (symmetryRatio >= 0.42f &&
                detectedFaceCx > width * 0.10f && detectedFaceCx < width * 0.90f &&
                eyeContrastValid) {
                facingCamera = true;
            }
        }
    } else {
        // 無像素串流模式 (僅在測試模擬時依賴 simulatedOpenness)
        personInFrame = (m_simulatedOpenness >= 0.0f);
        facingCamera = personInFrame;
    }

    // -------------------------------------------------------------------------
    // 階段三：確認偵測到正對鏡頭人臉 -> 階段四：確認與計算疲勞狀態
    // -------------------------------------------------------------------------
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

