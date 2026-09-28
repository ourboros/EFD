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

// 判斷單一色序是否符合膚色模型
inline bool checkRgbSkin(uint8_t r, uint8_t g, uint8_t b) {
    if (r <= 35 || g <= 20 || b <= 10) return false;
    if (r < g - 5 || r < b - 5) return false;

    // YCbCr 經典膚色色度模型 (放寬邊界以適應各種室內光線與相機)
    float y  =  0.299f * r + 0.587f * g + 0.114f * b;
    float cb = -0.1687f * r - 0.3313f * g + 0.500f * b + 128.0f;
    float cr =  0.500f * r - 0.4187f * g - 0.0813f * b + 128.0f;

    return (y >= 20.0f && y <= 250.0f && cb >= 68.0f && cb <= 145.0f && cr >= 118.0f && cr <= 188.0f);
}

// 多色彩空間膚色判斷 (同時校驗 RGB 與 BGR 兩種相機驅動可能輸出的排列順序)
inline bool isSkinPixel(uint8_t c0, uint8_t c1, uint8_t c2) {
    return checkRgbSkin(c0, c1, c2) || checkRgbSkin(c2, c1, c0);
}

} // anonymous namespace

// -----------------------------------------------------------------------------
// 從眼部感興趣區域 (ROI) 估計眼睛開合程度 (0.0 ~ 1.0)
// 原理：眼睛張開時，瞳孔與虹膜構成中央深色暗區 (深灰/黑)，周圍有眼白；
// 閉眼時眼瞼完全由膚色覆蓋，深色像素顯著消失。
// -----------------------------------------------------------------------------
float FaceLandmarker::estimateEyeOpennessFromROI(const uint8_t* pixelData, int width, int height,
                                                float eyeCx, float eyeCy, float eyeW, float eyeH) const {
    if (!pixelData || width <= 0 || height <= 0 || eyeW <= 2.0f || eyeH <= 2.0f) {
        return 0.85f;
    }

    int x0 = std::clamp(static_cast<int>(eyeCx - eyeW * 0.5f), 0, width - 1);
    int x1 = std::clamp(static_cast<int>(eyeCx + eyeW * 0.5f), 0, width - 1);
    int y0 = std::clamp(static_cast<int>(eyeCy - eyeH * 0.5f), 0, height - 1);
    int y1 = std::clamp(static_cast<int>(eyeCy + eyeH * 0.5f), 0, height - 1);

    if (x1 <= x0 || y1 <= y0) return 0.85f;

    int totalSamples = 0;
    int darkPupilPixels = 0;
    int skinLidPixels = 0;

    for (int y = y0; y <= y1; y += 2) {
        for (int x = x0; x <= x1; x += 2) {
            int idx = (y * width + x) * 3;
            uint8_t r = pixelData[idx];
            uint8_t g = pixelData[idx + 1];
            uint8_t b = pixelData[idx + 2];
            totalSamples++;

            // 亮度估計
            int luma = (static_cast<int>(r) * 299 + static_cast<int>(g) * 587 + static_cast<int>(b) * 114) / 1000;

            // 瞳孔/睫毛深色特徵
            if (luma < 60) {
                darkPupilPixels++;
            } else if (isSkinPixel(r, g, b)) {
                skinLidPixels++;
            }
        }
    }

    if (totalSamples == 0) return 0.85f;

    float pupilRatio = static_cast<float>(darkPupilPixels) / totalSamples;
    float skinRatio = static_cast<float>(skinLidPixels) / totalSamples;

    // 瞳孔可見率較高且眼皮膚色佔比適中時 -> 睜眼 (0.7 ~ 1.0)
    // 幾乎全是眼瞼膚色或暗色消失時 -> 閉眼 (0.05 ~ 0.25)
    float openness = std::clamp((pupilRatio * 7.0f) - (skinRatio * 0.4f) + 0.35f, 0.05f, 1.0f);
    return openness;
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
    float symmetryRatio = 0.0f;
    float computedOpenness = 0.85f;

    if (pixelData && width >= 64 && height >= 64 && !m_useSyntheticEngine) {
        // ---------------------------------------------------------------------
        // 階段一：人體與頭面部幾何特徵掃描 (Human Presence & Face Scanning)
        // 依據人臉膚色聚合區、邊界尺寸與長寬比綜合判定是否有人在畫面中
        // ---------------------------------------------------------------------
        int step = (width > 640) ? 8 : 4;
        int skinPixels = 0;
        uint64_t skinSumX = 0;
        uint64_t skinSumY = 0;
        int minSkinX = width, maxSkinX = 0;
        int minSkinY = height, maxSkinY = 0;
        int totalSampled = 0;

        for (int y = 0; y < height; y += step) {
            for (int x = 0; x < width; x += step) {
                totalSampled++;
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
        int sampleCols = (boxW / step) + 1;
        int sampleRows = (boxH / step) + 1;
        int boxSampleArea = sampleCols * sampleRows;
        float fillDensity = (boxSampleArea > 0) ? (static_cast<float>(skinPixels) / boxSampleArea) : 0.0f;
        float aspectRatio = (boxW > 0) ? (static_cast<float>(boxH) / static_cast<float>(boxW)) : 0.0f;

        // 確實在場條件：
        // 1. 採樣膚色點數充足 (採樣率至少佔畫面 0.8% 以上且 >= 30 個採樣點，排除桌面反射雜訊)
        // 2. 邊框尺寸合理 (佔畫面寬度 8% ~ 95%，高度 8% ~ 95%)
        // 3. 長寬比符合人臉/頭肩 (0.45 ~ 2.60)
        // 4. 區域緊湊度 (內部密度 >= 0.08)
        float minSkinRequired = std::max(30.0f, totalSampled * 0.008f);

        if (skinPixels >= minSkinRequired &&
            boxW >= static_cast<int>(width * 0.08f) && boxW <= static_cast<int>(width * 0.95f) &&
            boxH >= static_cast<int>(height * 0.08f) && boxH <= static_cast<int>(height * 0.95f) &&
            aspectRatio >= 0.45f && aspectRatio <= 2.60f &&
            fillDensity >= 0.08f) {

            detectedFaceCx = static_cast<float>(skinSumX) / skinPixels;
            detectedFaceCy = static_cast<float>(skinSumY) / skinPixels;

            // 質心位於畫面內部合理範圍
            if (detectedFaceCx > width * 0.08f && detectedFaceCx < width * 0.92f &&
                detectedFaceCy > height * 0.08f && detectedFaceCy < height * 0.92f) {
                personInFrame = true;
                detectedFaceScale = std::clamp(static_cast<float>(std::max(boxW, boxH)) * 0.75f,
                                               static_cast<float>(std::min(width, height)) * 0.20f,
                                               static_cast<float>(std::min(width, height)) * 0.85f);
            }
        }

        // ---------------------------------------------------------------------
        // 階段二：確認臉部是否朝向攝影機 (Face Facing Camera / Frontal Symmetry)
        // ---------------------------------------------------------------------
        if (personInFrame) {
            int leftSkin = 0;
            int rightSkin = 0;
            int midX = static_cast<int>(detectedFaceCx);

            for (int y = std::max(0, minSkinY); y <= std::min(height - 1, maxSkinY); y += step) {
                for (int x = std::max(0, minSkinX); x <= std::min(width - 1, maxSkinX); x += step) {
                    int idx = (y * width + x) * 3;
                    uint8_t c0 = pixelData[idx];
                    uint8_t c1 = pixelData[idx + 1];
                    uint8_t c2 = pixelData[idx + 2];

                    if (isSkinPixel(c0, c1, c2)) {
                        if (x < midX) leftSkin++;
                        else rightSkin++;
                    }
                }
            }

            int minSide = std::min(leftSkin, rightSkin);
            int maxSide = std::max(leftSkin, rightSkin);
            symmetryRatio = (maxSide > 0) ? (static_cast<float>(minSide) / maxSide) : 0.0f;

            float midBoxX = static_cast<float>(minSkinX + maxSkinX) * 0.5f;
            bool centerAligned = std::abs(detectedFaceCx - midBoxX) < (static_cast<float>(boxW) * 0.40f);

            // 正視判定：左右對稱度 >= 0.20 且臉心不過度歪斜
            if (symmetryRatio >= 0.20f && centerAligned) {
                facingCamera = true;
            }
        }

        // ---------------------------------------------------------------------
        // 階段三：實體畫面即時雙眼特徵掃描 (Real-time Eye Feature Extraction)
        // ---------------------------------------------------------------------
        if (personInFrame && facingCamera) {
            float eyeW = detectedFaceScale * 0.22f;
            float eyeH = detectedFaceScale * 0.14f;
            float leftEyeX = detectedFaceCx - detectedFaceScale * 0.28f;
            float leftEyeY = detectedFaceCy - detectedFaceScale * 0.12f;
            float rightEyeX = detectedFaceCx + detectedFaceScale * 0.28f;
            float rightEyeY = detectedFaceCy - detectedFaceScale * 0.12f;

            float leftOpen = estimateEyeOpennessFromROI(pixelData, width, height, leftEyeX, leftEyeY, eyeW, eyeH);
            float rightOpen = estimateEyeOpennessFromROI(pixelData, width, height, rightEyeX, rightEyeY, eyeW, eyeH);
            float currentInstantOpenness = (leftOpen + rightOpen) * 0.5f;

            // 平滑濾波更新眼睛開合度
            m_measuredEyeOpenness = (m_measuredEyeOpenness * 0.70f) + (currentInstantOpenness * 0.30f);
            computedOpenness = m_measuredEyeOpenness;
        }
    } else {
        // Synthetic 模擬測試驅動模式 (專用於無相機單元測試環境)
        personInFrame = (m_simulatedOpenness >= 0.0f);
        facingCamera = personInFrame;
        symmetryRatio = 0.95f;
        computedOpenness = m_simulatedOpenness;
    }

    // -------------------------------------------------------------------------
    // 時間平滑遲滯過濾器 (Temporal Hysteresis Filter)
    // 解決相機短暫自動曝光調節、單幀雜訊或眨眼造成的抖動；
    // 入座確認需要連續 3 幀有效偵測；離座確認在連續 30 幀 (約 1 秒) 未偵測到人臉時即時切換。
    // -------------------------------------------------------------------------
    bool rawDetectionActive = (personInFrame && facingCamera);
    if (rawDetectionActive) {
        m_consecutiveFaceFrames++;
        m_consecutiveMissingFrames = 0;
        m_lastKnownCx = detectedFaceCx;
        m_lastKnownCy = detectedFaceCy;
        m_lastKnownScale = detectedFaceScale;

        // 連續 3 影格確認後判定在場正視
        if (m_consecutiveFaceFrames >= 3) {
            m_isFaceConfirmed = true;
        }
    } else {
        m_consecutiveMissingFrames++;
        m_consecutiveFaceFrames = 0;

        // 當離開畫面連續 30 影格 (約 1.0 秒) 未偵測到有效人體，立即判定離座
        if (m_consecutiveMissingFrames >= 30) {
            m_isFaceConfirmed = false;
        }
    }

    // -------------------------------------------------------------------------
    // 階段四：確認偵測到正對鏡頭人臉 -> 提供特徵點；離座則完全清空特徵
    // -------------------------------------------------------------------------
    if (m_isFaceConfirmed) {
        result.hasFace = true;
        result.faceConfidence = std::clamp(symmetryRatio * 0.85f + 0.15f, 0.75f, 0.98f);

        float effectiveOpenness = computedOpenness;
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

        result.landmarks = generateCanonicalFaceMesh(width, height, effectiveOpenness, m_lastKnownCx, m_lastKnownCy, m_lastKnownScale);
    } else {
        // 使用者離座或未正對鏡頭 -> 清空特徵點，杜絕假疲勞數據
        result.hasFace = false;
        result.faceConfidence = 0.0f;
        result.landmarks.clear();
    }

    auto endTime = std::chrono::steady_clock::now();
    result.inferenceTimeMs = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count();

    return result;
}

} // namespace efd

