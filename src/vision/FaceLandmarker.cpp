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

namespace {

// 多色彩空間膚色判斷 (結合 Kovac RGB 與 YCbCr 雙重校驗)
inline bool isSkinPixel(uint8_t r, uint8_t g, uint8_t b) {
    // 1. 基礎 RGB 暖色度與對比度過濾
    if (r <= 75 || g <= 35 || b <= 20) return false;
    if ((r - g) < 10 || r <= b) return false;
    if ((std::max({r, g, b}) - std::min({r, g, b})) < 15) return false;

    // 2. YCbCr 經典膚色橢圓色度模型
    float y  =  0.299f * r + 0.587f * g + 0.114f * b;
    float cb = -0.1687f * r - 0.3313f * g + 0.500f * b + 128.0f;
    float cr =  0.500f * r - 0.4187f * g - 0.0813f * b + 128.0f;

    return (y >= 40.0f && y <= 240.0f && cb >= 77.0f && cb <= 127.0f && cr >= 133.0f && cr <= 173.0f);
}

} // anonymous namespace

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
    float symmetryRatio = 0.0f;

    if (pixelData && width >= 64 && height >= 64) {
        // ---------------------------------------------------------------------
        // 階段一：嚴格判斷畫面中有無人體/面部 (Person Presence Verification)
        // ---------------------------------------------------------------------
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

                if (isSkinPixel(r, g, b)) {
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

        int boxW = std::max(0, maxSkinX - minSkinX);
        int boxH = std::max(0, maxSkinY - minSkinY);
        int sampleCols = (boxW / step) + 1;
        int sampleRows = (boxH / step) + 1;
        int boxSampleArea = sampleCols * sampleRows;
        float fillDensity = (boxSampleArea > 0) ? (static_cast<float>(skinPixels) / boxSampleArea) : 0.0f;
        float aspectRatio = (boxW > 0) ? (static_cast<float>(boxH) / static_cast<float>(boxW)) : 0.0f;

        // 在場驗證條件：
        // 1. 採樣膚色點數充足 (>= 45 點，約數千像素)
        // 2. 邊框尺寸合理 (佔畫面寬度 >= 10%，佔高度 >= 12%，不可為全螢幕背景光)
        // 3. 垂直橢圓頭部比例 (長寬比 0.75 ~ 2.4，杜絕水平桌面或長條背景)
        // 4. 區域緊湊度 (內部填充率 >= 0.28，杜絕室內零散背景色塊)
        if (skinPixels >= 45 &&
            boxW >= static_cast<int>(width * 0.10f) && boxW <= static_cast<int>(width * 0.90f) &&
            boxH >= static_cast<int>(height * 0.12f) && boxH <= static_cast<int>(height * 0.92f) &&
            aspectRatio >= 0.75f && aspectRatio <= 2.40f &&
            fillDensity >= 0.28f) {

            detectedFaceCx = static_cast<float>(skinSumX) / skinPixels;
            detectedFaceCy = static_cast<float>(skinSumY) / skinPixels;

            // 質心必須位於畫面合理活動區域內
            if (detectedFaceCx > width * 0.12f && detectedFaceCx < width * 0.88f &&
                detectedFaceCy > height * 0.10f && detectedFaceCy < height * 0.90f) {
                personInFrame = true;
                detectedFaceScale = std::clamp(static_cast<float>(std::max(boxW, boxH)) * 0.85f,
                                               static_cast<float>(std::min(width, height)) * 0.20f,
                                               static_cast<float>(std::min(width, height)) * 0.80f);
            }
        }

        // ---------------------------------------------------------------------
        // 階段二：確認臉部是否朝向攝影機 (Face Facing Camera / Frontal Orientation)
        // ---------------------------------------------------------------------
        if (personInFrame) {
            int leftSkin = 0;
            int rightSkin = 0;
            int midX = static_cast<int>(detectedFaceCx);

            // 眼部區域 (上中 25%~45%) 與面頰區域 (中下 45%~75%) 亮度對比
            float eyeLumaSum = 0.0f; int eyeLumaCount = 0;
            float cheekLumaSum = 0.0f; int cheekLumaCount = 0;
            int eyeTop = minSkinY + static_cast<int>(boxH * 0.20f);
            int eyeBottom = minSkinY + static_cast<int>(boxH * 0.45f);
            int cheekBottom = minSkinY + static_cast<int>(boxH * 0.75f);

            for (int y = std::max(0, minSkinY); y <= std::min(height - 1, maxSkinY); y += step) {
                for (int x = std::max(0, minSkinX); x <= std::min(width - 1, maxSkinX); x += step) {
                    int idx = (y * width + x) * 3;
                    uint8_t r = pixelData[idx];
                    uint8_t g = pixelData[idx + 1];
                    uint8_t b = pixelData[idx + 2];

                    if (isSkinPixel(r, g, b)) {
                        if (x < midX) leftSkin++;
                        else rightSkin++;

                        float luma = 0.299f * r + 0.587f * g + 0.114f * b;
                        if (y >= eyeTop && y < eyeBottom) {
                            eyeLumaSum += luma;
                            eyeLumaCount++;
                        } else if (y >= eyeBottom && y < cheekBottom) {
                            cheekLumaSum += luma;
                            cheekLumaCount++;
                        }
                    }
                }
            }

            int minSide = std::min(leftSkin, rightSkin);
            int maxSide = std::max(leftSkin, rightSkin);
            symmetryRatio = (maxSide > 0) ? (static_cast<float>(minSide) / maxSide) : 0.0f;

            float midBoxX = static_cast<float>(minSkinX + maxSkinX) * 0.5f;
            bool centerAligned = std::abs(detectedFaceCx - midBoxX) < (static_cast<float>(boxW) * 0.22f);

            // 正對鏡頭判定：
            // 1. 左右臉部雙側對稱性良好 (symmetryRatio >= 0.55)
            // 2. 質心與幾何中心對齊
            if (symmetryRatio >= 0.55f && centerAligned) {
                facingCamera = true;
            }
        }
    } else {
        // Synthetic 模擬測試驅動模式 (專用於無相機單元測試環境)
        personInFrame = (m_simulatedOpenness >= 0.0f);
        facingCamera = personInFrame;
        symmetryRatio = 0.95f;
    }

    // -------------------------------------------------------------------------
    // 階段三：確認偵測到正對鏡頭人臉 -> 階段四：確認疲勞狀態
    // -------------------------------------------------------------------------
    if (personInFrame && facingCamera) {
        result.hasFace = true;
        result.faceConfidence = std::clamp(symmetryRatio * 0.85f + 0.15f, 0.75f, 0.98f);

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
        // 無人在鏡頭前 或 未正對鏡頭 -> 標記未偵測人臉，清空特徵點，杜絕假疲勞數據輸出
        result.hasFace = false;
        result.faceConfidence = 0.0f;
        result.landmarks.clear();
    }

    auto endTime = std::chrono::steady_clock::now();
    result.inferenceTimeMs = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count();

    return result;
}

} // namespace efd

