#include "vision/FaceLandmarker.hpp"
#include <chrono>
#include <cmath>
#include <algorithm>

namespace efd {

FaceLandmarker::FaceLandmarker() = default;
FaceLandmarker::~FaceLandmarker() = default;

bool FaceLandmarker::initialize(const std::string& modelPath) {
    m_modelPath = modelPath;
    m_useSyntheticEngine = false; // 預設使用真實相機影像掃描
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

// 判斷單一色序是否符合膚色模型 (大範圍適應性色度模型，相容多種光線、膚色與距離)
inline bool checkRgbSkin(uint8_t r, uint8_t g, uint8_t b) {
    if (r <= 30 || g <= 18 || b <= 10) return false;
    // 人體膚色在紅光波段強度通常高於綠光與藍光，但放寬以包容冷色溫環境與暗處
    if (r < g - 6 || r < b - 6) return false;

    // YCbCr 經典膚色色度模型 (大範圍容差邊界)
    float y  =  0.299f * r + 0.587f * g + 0.114f * b;
    float cb = -0.1687f * r - 0.3313f * g + 0.500f * b + 128.0f;
    float cr =  0.500f * r - 0.4187f * g - 0.0813f * b + 128.0f;

    return (y >= 20.0f && y <= 250.0f && cb >= 70.0f && cb <= 145.0f && cr >= 120.0f && cr <= 186.0f);
}

// 多色彩空間膚色判斷 (同時校驗 RGB 與 BGR 兩種相機驅動可能輸出的排列順序)
inline bool isSkinPixel(uint8_t c0, uint8_t c1, uint8_t c2) {
    return checkRgbSkin(c0, c1, c2) || checkRgbSkin(c2, c1, c0);
}

} // anonymous namespace

LandmarkDetectionResult FaceLandmarker::detect(const RawFrame& frame) {
    if (!frame.isValid()) {
        return detect(nullptr, 0, 0, PixelFormat::RGB888, true);
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

    FaceLandmarker::PresenceDiagnostic diag;

    // 定義攝影機正中間的監控大範圍方框 (Center ROI Box: 中央 75% 寬度 x 80% 高度)
    int cBoxW = static_cast<int>(width * 0.75f);
    int cBoxH = static_cast<int>(height * 0.80f);
    int cBoxX = (width - cBoxW) / 2;
    int cBoxY = (height - cBoxH) / 2;
    diag.centerBoxX = cBoxX;
    diag.centerBoxY = cBoxY;
    diag.centerBoxW = cBoxW;
    diag.centerBoxH = cBoxH;
    diag.frameWidth = width;
    diag.frameHeight = height;

    bool isRealCamera = (pixelData != nullptr) && (width >= 64) && (height >= 64) && (!isSynthetic);

    if (isRealCamera) {
        // ---------------------------------------------------------------------
        // 階段一：大範圍人體特徵掃描 (Wide-range Feature Scanning)
        // 採樣步長縮小至 2 (<=640) 或 4 (>640)，兼顧效能與靈敏度
        // ---------------------------------------------------------------------
        int step = (width > 640) ? 4 : 2;
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

        // 大範圍動態最小特徵門檻 (全畫面取樣點數的 0.15% 或至少 35 點，易於捕捉不同距離的使用者)
        int totalSamples = (width / step) * (height / step);
        int minSkinRequired = std::max(35, static_cast<int>(totalSamples * 0.0015f));

        diag.skinPixels = skinPixels;
        diag.minSkinRequired = minSkinRequired;
        diag.fillDensity = fillDensity;
        diag.aspectRatio = aspectRatio;
        diag.boxX = minSkinX;
        diag.boxY = minSkinY;
        diag.boxW = boxW;
        diag.boxH = boxH;

        // 1. 特徵充足性檢查 (若找不到相對應特徵，判定無人/離座)
        if (skinPixels < minSkinRequired) {
            diag.unconfirmedReason = "鏡頭前找不到相對應的人體特徵 (特徵點消失 / 使用者離座)";
        } else if (boxW < static_cast<int>(width * 0.05f) || boxH < static_cast<int>(height * 0.05f)) {
            diag.unconfirmedReason = "偵測特徵尺寸過小，疑似非在座人體 (區域: " + std::to_string(boxW) + "x" + std::to_string(boxH) + ")";
        } else if (aspectRatio < 0.35f || aspectRatio > 3.60f) {
            diag.unconfirmedReason = "特徵長寬比不符合人體輪廓 (長寬比: " + std::to_string(aspectRatio) + ")";
        } else {
            detectedFaceCx = static_cast<float>(skinSumX) / skinPixels;
            detectedFaceCy = static_cast<float>(skinSumY) / skinPixels;

            // 2. 邊界超出檢查 (Out-of-Bounds Check: 方框逐漸超出攝影機邊界時自動判定超出畫面)
            bool touchingEdge = (minSkinX <= static_cast<int>(width * 0.02f) ||
                                 maxSkinX >= static_cast<int>(width * 0.98f) ||
                                 minSkinY <= static_cast<int>(height * 0.02f) ||
                                 maxSkinY >= static_cast<int>(height * 0.98f));

            bool centroidOutside = (detectedFaceCx < width * 0.06f || detectedFaceCx > width * 0.94f ||
                                    detectedFaceCy < height * 0.06f || detectedFaceCy > height * 0.94f);

            if (touchingEdge || centroidOutside) {
                diag.isOutOfBounds = true;
                diag.unconfirmedReason = "動態方框逐漸超出攝影機範圍外 (自動判定使用者超出畫面)";
            } else {
                diag.isOutOfBounds = false;

                // 3. 正中間大範圍方框檢查 (使用者必須待在正中間的方框才會觸發提示)
                bool insideCenterBox = (detectedFaceCx >= cBoxX && detectedFaceCx <= (cBoxX + cBoxW) &&
                                        detectedFaceCy >= cBoxY && detectedFaceCy <= (cBoxY + cBoxH));
                diag.isWithinCenterRegion = insideCenterBox;

                if (!insideCenterBox) {
                    diag.unconfirmedReason = "使用者未待在正中間方框內 (請將面部對準中央監控區域)";
                } else {
                    // 特徵充足 + 未超出攝影機畫面 + 位於正中間方框內 -> 通過在場人體驗證
                    personInFrame = true;
                    detectedFaceScale = std::clamp(static_cast<float>(std::max(boxW, boxH)) * 0.85f,
                                                   static_cast<float>(std::min(width, height)) * 0.15f,
                                                   static_cast<float>(std::min(width, height)) * 0.90f);
                }
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
            diag.symmetryRatio = symmetryRatio;

            float midBoxX = static_cast<float>(minSkinX + maxSkinX) * 0.5f;
            bool centerAligned = std::abs(detectedFaceCx - midBoxX) < (static_cast<float>(boxW) * 0.65f);

            // 正對鏡頭判定：大範圍容許側臉角度 (symmetryRatio >= 0.12)
            if (symmetryRatio >= 0.12f && centerAligned) {
                facingCamera = true;
            } else {
                diag.unconfirmedReason = "臉部未正視鏡頭或偏轉角度過大 (對稱度: " + std::to_string(symmetryRatio) + ")";
            }
        }
    } else {
        // Synthetic 模擬測試驅動模式 (專用於無相機單元測試環境)
        personInFrame = (m_simulatedOpenness >= 0.0f);
        facingCamera = personInFrame;
        symmetryRatio = 0.95f;
        diag.frameWidth = (width > 0) ? width : 640;
        diag.frameHeight = (height > 0) ? height : 480;
        diag.boxW = diag.frameWidth / 3;
        diag.boxH = static_cast<int>(diag.frameHeight * 0.48f);
        diag.boxX = (diag.frameWidth - diag.boxW) / 2;
        diag.boxY = (diag.frameHeight - diag.boxH) / 3;
        diag.isWithinCenterRegion = personInFrame;
        diag.isOutOfBounds = false;
        diag.skinPixels = personInFrame ? 350 : 0;
        diag.minSkinRequired = 20;
        diag.fillDensity = personInFrame ? 0.35f : 0.0f;
        diag.aspectRatio = 1.33f;
        diag.symmetryRatio = personInFrame ? 0.95f : 0.0f;
        if (!personInFrame) {
            diag.unconfirmedReason = "Synthetic 模擬輸入為離座 (openness < 0)";
        }
    }

    diag.rawPersonInFrame = personInFrame;
    diag.rawFacingCamera = facingCamera;

    // -------------------------------------------------------------------------
    // 時間平滑遲滯過濾器 (Temporal Hysteresis Filter)
    // 進入在場快速確認 (2 影格)，離座確認 (6 影格 = 0.2 秒無人體即離座清空方框)
    // -------------------------------------------------------------------------
    bool rawDetectionActive = (personInFrame && facingCamera);
    if (rawDetectionActive) {
        m_consecutiveFaceFrames++;
        m_consecutiveMissingFrames = 0;
        m_lastKnownCx = detectedFaceCx;
        m_lastKnownCy = detectedFaceCy;
        m_lastKnownScale = detectedFaceScale;
        if (m_consecutiveFaceFrames >= 2) {
            m_isFaceConfirmed = true;
        }
    } else {
        m_consecutiveMissingFrames++;
        m_consecutiveFaceFrames = 0;
        // 6 影格 (約 0.2 秒) 無訊號即確認離座
        if (m_consecutiveMissingFrames >= 6) {
            m_isFaceConfirmed = false;
            m_lastKnownScale = 0.0f;
        }
    }

    diag.isFaceConfirmed = m_isFaceConfirmed;
    diag.stableCount = m_consecutiveFaceFrames;
    if (m_isFaceConfirmed) {
        diag.unconfirmedReason = "";
        if (m_lastKnownScale > 0.0f && diag.frameWidth > 0 && diag.frameHeight > 0) {
            int fw = static_cast<int>(m_lastKnownScale);
            int fh = static_cast<int>(m_lastKnownScale * 1.25f);
            int fx = static_cast<int>(m_lastKnownCx - fw / 2);
            int fy = static_cast<int>(m_lastKnownCy - fh * 0.55f);
            diag.boxX = std::max(0, fx);
            diag.boxY = std::max(0, fy);
            diag.boxW = std::min(fw, diag.frameWidth - diag.boxX);
            diag.boxH = std::min(fh, diag.frameHeight - diag.boxY);
        }
    } else {
        // 確認離座或超出畫面時，清空動態方框，防止畫面殘留舊座標
        diag.boxX = 0;
        diag.boxY = 0;
        diag.boxW = 0;
        diag.boxH = 0;
    }
    m_lastDiag = diag;

    // -------------------------------------------------------------------------
    // 階段三：確認偵測到正對鏡頭人臉 -> 階段四：確認疲勞狀態
    // -------------------------------------------------------------------------
    if (m_isFaceConfirmed) {
        result.hasFace = true;
        result.faceConfidence = std::clamp(symmetryRatio * 0.85f + 0.15f, 0.75f, 0.98f);

        float effectiveOpenness = 1.0f;
        if (!isRealCamera) {
            // Synthetic 模擬測試驅動自然眨眼週期
            effectiveOpenness = m_simulatedOpenness;
            if (m_simulatedOpenness >= 0.85f) {
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
        } else {
            // 實體相機真實眼睛動態提取 (Real Eye Openness Gradient Extraction)
            float eyeDist = m_lastKnownScale * 0.28f;
            float eyeYOffset = m_lastKnownScale * 0.10f;
            int eyeBoxRadius = std::max(6, static_cast<int>(m_lastKnownScale * 0.08f));

            int eyeCentersX[2] = {
                static_cast<int>(m_lastKnownCx - eyeDist),
                static_cast<int>(m_lastKnownCx + eyeDist)
            };
            int eyeCenterY = static_cast<int>(m_lastKnownCy - eyeYOffset);

            float totalGrad = 0.0f;
            int gradSamples = 0;

            for (int e = 0; e < 2; ++e) {
                int ecx = eyeCentersX[e];
                int ecy = eyeCenterY;

                for (int dy = -eyeBoxRadius; dy <= eyeBoxRadius - 2; dy += 2) {
                    int py = ecy + dy;
                    if (py <= 1 || py >= height - 2) continue;

                    for (int dx = -eyeBoxRadius; dx <= eyeBoxRadius; dx += 2) {
                        int px = ecx + dx;
                        if (px <= 0 || px >= width) continue;

                        int idxTop = ((py - 1) * width + px) * 3;
                        int idxBot = ((py + 1) * width + px) * 3;

                        int lumTop = (pixelData[idxTop] * 299 + pixelData[idxTop + 1] * 587 + pixelData[idxTop + 2] * 114) / 1000;
                        int lumBot = (pixelData[idxBot] * 299 + pixelData[idxBot + 1] * 587 + pixelData[idxBot + 2] * 114) / 1000;

                        totalGrad += std::abs(lumBot - lumTop);
                        gradSamples++;
                    }
                }
            }

            float avgGrad = (gradSamples > 0) ? (totalGrad / gradSamples) : 18.0f;
            effectiveOpenness = std::clamp((avgGrad - 6.0f) / 16.0f, 0.05f, 1.0f);
        }

        result.landmarks = generateCanonicalFaceMesh(width, height, effectiveOpenness, m_lastKnownCx, m_lastKnownCy, m_lastKnownScale);
    } else {
        // 使用者離座或背對鏡頭 -> 清空特徵點，杜絕假疲勞數據
        result.hasFace = false;
        result.faceConfidence = 0.0f;
        result.landmarks.clear();
    }

    auto endTime = std::chrono::steady_clock::now();
    result.inferenceTimeMs = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count();

    return result;
}

FaceLandmarker::PresenceDiagnostic FaceLandmarker::getLatestPresenceDiagnostic() const {
    return m_lastDiag;
}

} // namespace efd

