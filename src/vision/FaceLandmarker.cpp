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

// 判斷單一色序是否符合膚色模型 (YCbCr + RGB 寬容色度模型，與 Windows 版演算法完全一致)
inline bool checkRgbSkin(uint8_t r, uint8_t g, uint8_t b) {
    if (r <= 35 || g <= 20 || b <= 10) return false;
    if (r < g - 5 || r < b - 5) return false;

    float y  =  0.299f * r + 0.587f * g + 0.114f * b;
    float cb = -0.1687f * r - 0.3313f * g + 0.500f * b + 128.0f;
    float cr =  0.500f * r - 0.4187f * g - 0.0813f * b + 128.0f;

    return (y >= 20.0f && y <= 250.0f && cb >= 68.0f && cb <= 145.0f && cr >= 118.0f && cr <= 188.0f);
}

// 多色彩空間膚色判斷 (同時校驗 RGB 與 BGR 兩種相機驅動可能輸出的排列順序)
inline bool isSkinPixel(uint8_t c0, uint8_t c1, uint8_t c2, PixelFormat format) {
    if (format == PixelFormat::BGR888) {
        return checkRgbSkin(c2, c1, c0) || checkRgbSkin(c0, c1, c2);
    }
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
    diag.frameWidth = width;
    diag.frameHeight = height;
    diag.userBiasX = m_userBiasX;
    diag.userBiasY = m_userBiasY;
    diag.isPositionBiasCalibrated = m_isPositionBiasCalibrated;

    bool isRealCamera = (pixelData != nullptr) && (width >= 64) && (height >= 64) && (!isSynthetic);

    if (isRealCamera) {
        // ---------------------------------------------------------------------
        // 階段一：人體與人臉特徵高精度掃描 (全視野動態覆蓋，與 Windows 版演算法完全一致)
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

                if (isSkinPixel(c0, c1, c2, format)) {
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

        // 合理特徵門檻：既能過濾微小光影噪訊，又能敏銳捕捉任意坐姿與偏向角的使用者
        int totalSamples = (width / step) * (height / step);
        int minSkinRequired = std::max(60, static_cast<int>(totalSamples * 0.0015f));

        diag.skinPixels = skinPixels;
        diag.minSkinRequired = minSkinRequired;
        diag.fillDensity = fillDensity;
        diag.aspectRatio = aspectRatio;

        // 在場驗證條件（與 Windows 版演算法完全一致）：
        if (skinPixels < minSkinRequired) {
            diag.unconfirmedReason = "鏡頭前未偵測到真人臉部膚色特徵 (使用者離座 / 採樣點 " + std::to_string(skinPixels) + " < " + std::to_string(minSkinRequired) + ")";
        } else if (boxW < static_cast<int>(width * 0.04f) || boxH < static_cast<int>(height * 0.04f)) {
            diag.unconfirmedReason = "目標特徵區域過小，非在座人體 (區域: " + std::to_string(boxW) + "x" + std::to_string(boxH) + ")";
        } else if (aspectRatio < 0.35f || aspectRatio > 3.80f) {
            diag.unconfirmedReason = "長寬比例不符合人臉頭部輪廓 (比例: " + std::to_string(aspectRatio) + ")";
        } else if (fillDensity < 0.035f) {
            diag.unconfirmedReason = "特徵分佈散亂，非連續臉部實體 (填充率: " + std::to_string(fillDensity) + ")";
        } else {
            detectedFaceCx = static_cast<float>(skinSumX) / skinPixels;
            detectedFaceCy = static_cast<float>(skinSumY) / skinPixels;

            // 質心位於畫面合理視野範圍內
            if (detectedFaceCx > width * 0.04f && detectedFaceCx < width * 0.96f &&
                detectedFaceCy > height * 0.04f && detectedFaceCy < height * 0.96f) {
                personInFrame = true;
                int faceH = std::min(boxH, static_cast<int>(boxW * 1.35f));
                detectedFaceScale = std::clamp(static_cast<float>(std::max(boxW, faceH)),
                                               static_cast<float>(std::min(width, height)) * 0.15f,
                                               static_cast<float>(std::min(width, height)) * 0.98f);
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

                    if (isSkinPixel(c0, c1, c2, format)) {
                        if (x < midX) leftSkin++;
                        else rightSkin++;
                    }
                }
            }

            int minSide = std::min(leftSkin, rightSkin);
            int maxSide = std::max(leftSkin, rightSkin);
            symmetryRatio = (maxSide > 0) ? (static_cast<float>(minSide) / maxSide) : 0.0f;
            diag.symmetryRatio = symmetryRatio;

            // 只要偵測到人臉並大致朝向鏡頭 (允許偏轉、偏坐、戴耳機或單側光照)
            if (symmetryRatio >= 0.08f) {
                facingCamera = true;
            } else {
                bool nearBorder = (detectedFaceCx < width * 0.20f || detectedFaceCx > width * 0.80f);
                if (nearBorder && skinPixels >= minSkinRequired) {
                    facingCamera = true;
                } else {
                    diag.unconfirmedReason = "臉部偏轉角度過大 (對稱度: " + std::to_string(symmetryRatio) + ")";
                }
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
        diag.skinPixels = personInFrame ? 450 : 0;
        diag.minSkinRequired = 70;
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
    // 階段三：動態平滑追隨跟蹤 (Smooth Dynamic Follow-Me Tracking)
    // 方框隨使用者真實位置動態平滑移動，支援全畫面任意偏向位置追蹤！
    // -------------------------------------------------------------------------
    bool rawDetectionActive = (personInFrame && facingCamera);
    if (rawDetectionActive) {
        m_consecutiveFaceFrames++;
        m_consecutiveMissingFrames = 0;

        if (m_trackedScale <= 1.0f) {
            m_trackedCx = detectedFaceCx;
            m_trackedCy = detectedFaceCy;
            m_trackedScale = detectedFaceScale;
        } else {
            // 平滑動態跟隨使用者實際頭部位置移動
            m_trackedCx = m_trackedCx * 0.60f + detectedFaceCx * 0.40f;
            m_trackedCy = m_trackedCy * 0.60f + detectedFaceCy * 0.40f;
            m_trackedScale = m_trackedScale * 0.70f + detectedFaceScale * 0.30f;
        }

        m_lastKnownCx = m_trackedCx;
        m_lastKnownCy = m_trackedCy;
        m_lastKnownScale = m_trackedScale;

        if (m_consecutiveFaceFrames >= 2) {
            m_isFaceConfirmed = true;
        }
    } else {
        m_consecutiveMissingFrames++;
        m_consecutiveFaceFrames = 0;
        // 允許 15 影格 (約 0.5 秒) 的短暫晃動或遮擋寬限期，避免一動就立刻判定離座
        if (m_consecutiveMissingFrames >= 15) {
            m_isFaceConfirmed = false;
            m_trackedScale = 0.0f;
        }
    }

    diag.isFaceConfirmed = m_isFaceConfirmed;
    diag.stableCount = m_consecutiveFaceFrames;
    diag.trackedCx = m_trackedCx;
    diag.trackedCy = m_trackedCy;

    if (m_isFaceConfirmed) {
        diag.unconfirmedReason = "";
        int fw = static_cast<int>(m_trackedScale * 0.95f);
        int fh = static_cast<int>(m_trackedScale * 1.25f);
        int fx = static_cast<int>(m_trackedCx - fw * 0.5f);
        int fy = static_cast<int>(m_trackedCy - fh * 0.52f);
        diag.boxX = std::clamp(fx, 0, std::max(0, width - 10));
        diag.boxY = std::clamp(fy, 0, std::max(0, height - 10));
        diag.boxW = std::clamp(fw, 10, std::max(10, width - diag.boxX));
        diag.boxH = std::clamp(fh, 10, std::max(10, height - diag.boxY));
    } else {
        // 確認離座時立即清空方框，杜絕舊座標殘留
        diag.boxX = 0;
        diag.boxY = 0;
        diag.boxW = 0;
        diag.boxH = 0;
    }
    m_lastDiag = diag;

    // -------------------------------------------------------------------------
    // 階段四：依據使用者實際位置追蹤眼睛特徵點與開闔度
    // -------------------------------------------------------------------------
    if (m_isFaceConfirmed) {
        result.hasFace = true;
        result.faceConfidence = std::clamp(symmetryRatio * 0.85f + 0.15f, 0.75f, 0.98f);

        float effectiveOpenness = 1.0f;
        if (!isRealCamera) {
            // Synthetic 模擬測試自然眨眼週期
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
            // 實體相機：依據眼睛興趣區域 (AOI) 二值化後計算黑色像素比例 (Black Pixels Ratio)
            float eyeDist = m_trackedScale * 0.28f;
            float eyeYOffset = m_trackedScale * 0.10f;
            int eyeBoxRadius = std::max(8, static_cast<int>(m_trackedScale * 0.10f));

            int eyeCentersX[2] = {
                static_cast<int>(m_trackedCx - eyeDist),
                static_cast<int>(m_trackedCx + eyeDist)
            };
            int nominalEyeCenterY = static_cast<int>(m_trackedCy - eyeYOffset);
            int searchVerticalRange = std::max(3, static_cast<int>(eyeBoxRadius * 0.50f));
            int bestEyeCenterY = nominalEyeCenterY;
            float maxRowContrast = -1.0f;

            for (int dy = -searchVerticalRange; dy <= searchVerticalRange; dy += 2) {
                int testY = nominalEyeCenterY + dy;
                if (testY <= 1 || testY >= height - 2) continue;

                float rowContrastSum = 0.0f;
                int rowSampleCount = 0;
                for (int e = 0; e < 2; ++e) {
                    int ecx = eyeCentersX[e];
                    for (int dx = -eyeBoxRadius / 2; dx <= eyeBoxRadius / 2; dx += 2) {
                        int px = ecx + dx;
                        if (px <= 0 || px >= width) continue;
                        int idxTop = ((testY - 1) * width + px) * 3;
                        int idxBot = ((testY + 1) * width + px) * 3;
                        int lTop = (pixelData[idxTop] * 299 + pixelData[idxTop + 1] * 587 + pixelData[idxTop + 2] * 114) / 1000;
                        int lBot = (pixelData[idxBot] * 299 + pixelData[idxBot + 1] * 587 + pixelData[idxBot + 2] * 114) / 1000;
                        rowContrastSum += std::abs(lBot - lTop);
                        rowSampleCount++;
                    }
                }
                float avgContrastRow = (rowSampleCount > 0) ? (rowContrastSum / rowSampleCount) : 0.0f;
                if (avgContrastRow > maxRowContrast) {
                    maxRowContrast = avgContrastRow;
                    bestEyeCenterY = testY;
                }
            }

            int eyeCenterY = bestEyeCenterY;

            float totalDarkRatio = 0.0f;

            for (int e = 0; e < 2; ++e) {
                int ecx = eyeCentersX[e];
                int ecy = eyeCenterY;

                long long totalLuma = 0;
                int pixelCount = 0;
                std::vector<int> lumas;
                lumas.reserve((eyeBoxRadius * 2 + 1) * (eyeBoxRadius * 2 + 1));

                for (int dy = -eyeBoxRadius; dy <= eyeBoxRadius; dy++) {
                    int py = ecy + dy;
                    if (py < 0 || py >= height) continue;

                    for (int dx = -eyeBoxRadius; dx <= eyeBoxRadius; dx++) {
                        int px = ecx + dx;
                        if (px < 0 || px >= width) continue;

                        int idx = (py * width + px) * 3;
                        int lum = (pixelData[idx] * 299 + pixelData[idx + 1] * 587 + pixelData[idx + 2] * 114) / 1000;
                        totalLuma += lum;
                        lumas.push_back(lum);
                        pixelCount++;
                    }
                }

                if (pixelCount > 0) {
                    int avgLuma = totalLuma / pixelCount;
                    // 將像素值低於(平均亮度 * 0.75)視為瞳孔/睫毛等黑色像素
                    int darkThreshold = static_cast<int>(avgLuma * 0.78f);
                    int darkPixels = 0;
                    for (int lum : lumas) {
                        if (lum < darkThreshold) {
                            darkPixels++;
                        }
                    }
                    float darkRatio = static_cast<float>(darkPixels) / pixelCount;
                    totalDarkRatio += darkRatio;
                }
            }

            float avgDarkRatio = totalDarkRatio / 2.0f;
            // 睜眼時暗部比例通常較高（例如 0.15~0.30），閉眼時只剩膚色（例如 < 0.05）
            // 將 darkRatio 映射到 0.0 ~ 1.0 的 effectiveOpenness
            // [修正 C] 自適應動態映射：追蹤個人「睜眼基準暗度」解決不同光源差異
            // 睜眼時瞳孔/睫毛形成暗色區域（darkRatio 較高），閉眼時膚色覆蓋（darkRatio 下降）
            static float s_openEyeDarkBaseline = 0.18f; // 初始預設基準（可自動收斂）
            static float s_emaAlpha = 0.015f;           // 緩慢追蹤（約每 67 幀更新一次基準）
            // 只在高暗度幀（推測為睜眼）時更新基準，避免基準被閉眼拉低
            if (avgDarkRatio > s_openEyeDarkBaseline * 0.65f) {
                s_openEyeDarkBaseline = s_openEyeDarkBaseline * (1.0f - s_emaAlpha) + avgDarkRatio * s_emaAlpha;
                s_openEyeDarkBaseline = std::clamp(s_openEyeDarkBaseline, 0.08f, 0.55f);
            }
            // 閉眼閾值 = 基準的 30%（眼瞼覆蓋後暗色像素大幅減少）
            float closedThreshRatio = s_openEyeDarkBaseline * 0.30f;
            float opennessRange = s_openEyeDarkBaseline - closedThreshRatio;
            if (opennessRange < 0.02f) opennessRange = 0.02f;
            effectiveOpenness = std::clamp((avgDarkRatio - closedThreshRatio) / opennessRange, 0.0f, 1.0f);
        }

        // 依據跟隨的位置動態生成臉部特徵網格
        result.landmarks = generateCanonicalFaceMesh(width, height, effectiveOpenness, m_trackedCx, m_trackedCy, m_trackedScale);
    } else {
        // 使用者離座 -> 清空特徵點，杜絕假疲勞數據
        result.hasFace = false;
        result.faceConfidence = 0.0f;
        result.landmarks.clear();
    }

    auto endTime = std::chrono::steady_clock::now();
    result.inferenceTimeMs = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count();

    return result;
}

void FaceLandmarker::calibrateUserPositionBias(float cx, float cy) {
    m_userBiasX = cx;
    m_userBiasY = cy;
    m_isPositionBiasCalibrated = true;
    m_trackedCx = cx;
    m_trackedCy = cy;
}

bool FaceLandmarker::isPositionBiasCalibrated() const {
    return m_isPositionBiasCalibrated;
}

void FaceLandmarker::resetTracking() {
    m_trackedCx = 0.0f;
    m_trackedCy = 0.0f;
    m_trackedScale = 0.0f;
    m_consecutiveFaceFrames = 0;
    m_consecutiveMissingFrames = 100;
    m_isFaceConfirmed = false;
}

FaceLandmarker::PresenceDiagnostic FaceLandmarker::getLatestPresenceDiagnostic() const {
    return m_lastDiag;
}

} // namespace efd

