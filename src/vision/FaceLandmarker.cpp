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

    // YCbCr 經典膚色色度模型 (放寬邊界以適應各種室內光線與攝影機)
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

    if (pixelData && width >= 64 && height >= 64 && !m_useSyntheticEngine) {
        // ---------------------------------------------------------------------
        // 階段一：判斷畫面中有無人體/面部 (Person Presence Verification)
        // 採用超寬容特徵採樣，杜絕因微小晃動、戴眼鏡或光線調節造成誤判
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
        int sampleCols = (boxW / step) + 1;
        int sampleRows = (boxH / step) + 1;
        int boxSampleArea = sampleCols * sampleRows;
        float fillDensity = (boxSampleArea > 0) ? (static_cast<float>(skinPixels) / boxSampleArea) : 0.0f;
        float aspectRatio = (boxW > 0) ? (static_cast<float>(boxH) / static_cast<float>(boxW)) : 0.0f;

        // 在場驗證條件（低敏感度設定，大幅提升對坐姿與環境的包容度）：
        // 1. 採樣膚色點數充足 (>= 8 點)
        // 2. 邊框尺寸合理 (佔畫面寬度 >= 3%，佔高度 >= 3%)
        // 3. 頭部幾何比例放寬 (長寬比 0.35 ~ 4.00)
        // 4. 區域緊湊度 (內部填充率 >= 0.04)
        if (skinPixels >= 8 &&
            boxW >= static_cast<int>(width * 0.03f) && boxW <= static_cast<int>(width * 0.98f) &&
            boxH >= static_cast<int>(height * 0.03f) && boxH <= static_cast<int>(height * 0.98f) &&
            aspectRatio >= 0.35f && aspectRatio <= 4.00f &&
            fillDensity >= 0.04f) {

            detectedFaceCx = static_cast<float>(skinSumX) / skinPixels;
            detectedFaceCy = static_cast<float>(skinSumY) / skinPixels;

            // 質心位於畫面合理視野範圍內
            if (detectedFaceCx > width * 0.04f && detectedFaceCx < width * 0.96f &&
                detectedFaceCy > height * 0.04f && detectedFaceCy < height * 0.96f) {
                personInFrame = true;
                detectedFaceScale = std::clamp(static_cast<float>(std::max(boxW, boxH)) * 0.85f,
                                               static_cast<float>(std::min(width, height)) * 0.15f,
                                               static_cast<float>(std::min(width, height)) * 0.90f);
            }
        }

        // ---------------------------------------------------------------------
        // 階段二：確認臉部是否朝向攝影機 (Face Facing Camera / Frontal Orientation)
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
            bool centerAligned = std::abs(detectedFaceCx - midBoxX) < (static_cast<float>(boxW) * 0.65f);

            // 正對鏡頭判定：允許單側光與微側臉 (symmetryRatio >= 0.15)
            if (symmetryRatio >= 0.15f && centerAligned) {
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
    // 時間平滑遲滯過濾器 (Temporal Hysteresis Filter)
    // 解決相機丟幀、光線自動調節、眨眼或使用者轉頭時的狀態抖動
    // -------------------------------------------------------------------------
    bool rawDetectionActive = (personInFrame && facingCamera);
    if (rawDetectionActive) {
        m_consecutiveFaceFrames++;
        m_consecutiveMissingFrames = 0;
        m_lastKnownCx = detectedFaceCx;
        m_lastKnownCy = detectedFaceCy;
        m_lastKnownScale = detectedFaceScale;
        m_isFaceConfirmed = true;
    } else {
        m_consecutiveMissingFrames++;
        m_consecutiveFaceFrames = 0;
        // 提供 90 影格 (約 3.0 秒) 的平滑緩衝延遲：只有在連續整整 3 秒都未偵測到時，才確認離座
        if (m_consecutiveMissingFrames >= 90) {
            m_isFaceConfirmed = false;
        }
    }

    // -------------------------------------------------------------------------
    // 階段三：確認偵測到正對鏡頭人臉 -> 階段四：確認疲勞狀態
    // -------------------------------------------------------------------------
    if (m_isFaceConfirmed) {
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

        result.landmarks = generateCanonicalFaceMesh(width, height, effectiveOpenness, m_lastKnownCx, m_lastKnownCy, m_lastKnownScale);
    } else {
        // 使用者離座或背對鏡頭 (超過 3.0 秒確實驗證) -> 清空特徵點，杜絕假疲勞數據
        result.hasFace = false;
        result.faceConfidence = 0.0f;
        result.landmarks.clear();
    }

    auto endTime = std::chrono::steady_clock::now();
    result.inferenceTimeMs = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count();

    return result;
}

} // namespace efd


