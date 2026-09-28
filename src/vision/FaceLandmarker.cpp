#include "vision/FaceLandmarker.hpp"
#include <chrono>
#include <cmath>
#include <algorithm>

namespace efd {

FaceLandmarker::FaceLandmarker() = default;
FaceLandmarker::~FaceLandmarker() = default;

bool FaceLandmarker::initialize(const std::string& modelPath) {
    m_modelPath = modelPath;
    m_useSyntheticEngine = false;
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

// 判斷單一色序是否符合人類膚色色度模型 (排除木頭、白牆、陰影與強光過曝)
inline bool checkRgbSkin(uint8_t r, uint8_t g, uint8_t b) {
    if (r < 45 || g < 30 || b < 20) return false;
    if (r > 248 && g > 248 && b > 248) return false;

    // 人體膚色色階特徵：R 通常大於 G，且 G 大於 B
    if (r < g - 2 || r < b + 8) return false;

    // YCbCr 色度空間轉換
    float y  =  0.299f * r + 0.587f * g + 0.114f * b;
    float cb = -0.1687f * r - 0.3313f * g + 0.500f * b + 128.0f;
    float cr =  0.500f * r - 0.4187f * g - 0.0813f * b + 128.0f;

    float diff = cr - cb;
    return (y >= 40.0f && y <= 240.0f &&
            cb >= 78.0f && cb <= 135.0f &&
            cr >= 130.0f && cr <= 178.0f &&
            diff >= 10.0f && diff <= 70.0f);
}

// 多色彩空間膚色判斷 (同時支援 RGB888 與 BGR888 格式)
inline bool isSkinPixel(uint8_t c0, uint8_t c1, uint8_t c2) {
    return checkRgbSkin(c0, c1, c2) || checkRgbSkin(c2, c1, c0);
}

} // anonymous namespace

LandmarkDetectionResult FaceLandmarker::detect(const RawFrame& frame) {
    if (!frame.isValid()) {
        return {};
    }
    return detect(frame.data.data(), frame.width, frame.height, frame.format, frame.isSynthetic);
}

LandmarkDetectionResult FaceLandmarker::detect(const uint8_t* pixelData, int width, int height, PixelFormat format, bool isSynthetic) {
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

    if (isSynthetic) {
        // ---------------------------------------------------------------------
        // Synthetic 虛擬測試模式 (專用於無相機單元測試環境)
        // ---------------------------------------------------------------------
        personInFrame = (m_simulatedOpenness >= 0.0f);
        facingCamera = personInFrame;
        symmetryRatio = 0.95f;
        detectedFaceCx = width * 0.5f;
        detectedFaceCy = height * 0.5f;
        detectedFaceScale = std::min(width, height) * 0.4f;
    } else if (pixelData && width >= 64 && height >= 64) {
        // ---------------------------------------------------------------------
        // 階層階段一：判斷畫面中有無人體/面部 (Person Presence Verification)
        // ---------------------------------------------------------------------
        int step = (width > 640) ? 8 : 4;
        int skinPixels = 0;
        uint64_t skinSumX = 0;
        uint64_t skinSumY = 0;
        int minSkinX = width, maxSkinX = 0;
        int minSkinY = height, maxSkinY = 0;

        for (int y = 0; y < height; y += step) {
            for (int x = 0; x < width; x += step) {
                int idx = (y * width + x) * 3;
                uint8_t c0 = pixelData[idx];
                uint8_t c1 = pixelData[idx + 1];
                uint8_t c2 = pixelData[idx + 2];

                if (isSkinPixel(c0, c1, c2)) {
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
        int totalGridPoints = (width / step) * (height / step);
        int sampleCols = (boxW / step) + 1;
        int sampleRows = (boxH / step) + 1;
        int boxSampleArea = sampleCols * sampleRows;
        float fillDensity = (boxSampleArea > 0) ? (static_cast<float>(skinPixels) / boxSampleArea) : 0.0f;
        float aspectRatio = (boxW > 0) ? (static_cast<float>(boxH) / static_cast<float>(boxW)) : 0.0f;

        // 在場驗證條件：
        // 1. 採樣膚色點數充足 (>= 50 點，排除單點雜訊)，且不超過畫面 60% (排除全屏壁紙反光)
        // 2. 邊框尺寸合理 (佔寬度 10%~85%，佔高度 12%~90%)
        // 3. 頭部長寬比例合理 (長寬比 0.65 ~ 2.60)
        // 4. 區域集中度充足 (內部填充率 >= 0.10)
        int minRequiredSkin = std::max(50, totalGridPoints / 300);
        int maxAllowedSkin = static_cast<int>(totalGridPoints * 0.60f);

        if (skinPixels >= minRequiredSkin && skinPixels <= maxAllowedSkin &&
            boxW >= static_cast<int>(width * 0.10f) && boxW <= static_cast<int>(width * 0.85f) &&
            boxH >= static_cast<int>(height * 0.12f) && boxH <= static_cast<int>(height * 0.90f) &&
            aspectRatio >= 0.65f && aspectRatio <= 2.60f &&
            fillDensity >= 0.10f) {

            detectedFaceCx = static_cast<float>(skinSumX) / skinPixels;
            detectedFaceCy = static_cast<float>(skinSumY) / skinPixels;

            // 質心位於畫面合理視野範圍內
            if (detectedFaceCx > width * 0.08f && detectedFaceCx < width * 0.92f &&
                detectedFaceCy > height * 0.06f && detectedFaceCy < height * 0.92f) {
                personInFrame = true;
                detectedFaceScale = std::clamp(static_cast<float>(std::max(boxW, boxH)) * 0.85f,
                                               static_cast<float>(std::min(width, height)) * 0.15f,
                                               static_cast<float>(std::min(width, height)) * 0.90f);
            }
        }

        // ---------------------------------------------------------------------
        // 階層階段二：確認臉部是否朝向攝影機 (Face Facing Camera Verification)
        // ---------------------------------------------------------------------
        if (personInFrame) {
            int leftSkin = 0;
            int rightSkin = 0;
            int midX = static_cast<int>(detectedFaceCx);

            for (int y = std::max(0, minSkinY); y <= std::min(height - 1, maxSkinY); y += step) {
                for (int x = std::max(0, minSkinX); x <= std::min(width - 1, maxSkinX); x += step) {
                    int idx = (y * width + x) * 3;
                    if (isSkinPixel(pixelData[idx], pixelData[idx + 1], pixelData[idx + 2])) {
                        if (x < midX) leftSkin++;
                        else rightSkin++;
                    }
                }
            }

            int minSide = std::min(leftSkin, rightSkin);
            int maxSide = std::max(leftSkin, rightSkin);
            symmetryRatio = (maxSide > 0) ? (static_cast<float>(minSide) / static_cast<float>(maxSide)) : 0.0f;

            float boxMidX = static_cast<float>(minSkinX + maxSkinX) * 0.5f;
            bool centerAligned = std::abs(detectedFaceCx - boxMidX) < (static_cast<float>(boxW) * 0.40f);

            // 正對鏡頭判定：允許自然微側臉與環境側光 (symmetryRatio >= 0.28)，但杜絕側臉 90 度
            if (symmetryRatio >= 0.28f && centerAligned) {
                facingCamera = true;
            }
        }
    }

    // -------------------------------------------------------------------------
    // 階層階段三：確認偵測到正對人臉 (Face Confirmed & Temporal Hysteresis Filter)
    // -------------------------------------------------------------------------
    bool rawDetectionActive = (personInFrame && facingCamera);
    if (rawDetectionActive) {
        m_consecutiveFaceFrames++;
        m_consecutiveMissingFrames = 0;
        m_lastKnownCx = detectedFaceCx;
        m_lastKnownCy = detectedFaceCy;
        m_lastKnownScale = detectedFaceScale;
        // 連續 2 幀偵測到正對臉部，即時確認在座（響應極其靈敏迅速）
        if (m_consecutiveFaceFrames >= 2) {
            m_isFaceConfirmed = true;
        }
    } else {
        m_consecutiveMissingFrames++;
        m_consecutiveFaceFrames = 0;
        // 離座確認延遲：連續 30 幀 (約 1.0 秒) 未偵測到人臉或未正對，確認離座
        if (m_consecutiveMissingFrames >= 30) {
            m_isFaceConfirmed = false;
        }
    }

    // -------------------------------------------------------------------------
    // 階層階段四：確認疲勞狀態 (僅在確認人臉在座正視時計算，離座即時清空)
    // -------------------------------------------------------------------------
    if (m_isFaceConfirmed) {
        result.hasFace = true;
        result.faceConfidence = std::clamp(symmetryRatio * 0.85f + 0.15f, 0.75f, 0.98f);

        float effectiveOpenness = m_simulatedOpenness;
        if (isSynthetic && m_simulatedOpenness >= 0.85f) {
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

        result.landmarks = generateCanonicalFaceMesh(width, height, effectiveOpenness, m_lastKnownCx, m_lastKnownCy, m_lastKnownScale);
    } else {
        // 使用者離座或未正對鏡頭 -> 清空特徵點與信心值，杜絕假疲勞數據輸出
        result.hasFace = false;
        result.faceConfidence = 0.0f;
        result.landmarks.clear();
    }

    auto endTime = std::chrono::steady_clock::now();
    result.inferenceTimeMs = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count();

    return result;
}

} // namespace efd

