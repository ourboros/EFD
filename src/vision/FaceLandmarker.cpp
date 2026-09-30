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

// 嚴格膚色色彩模型 (Kovac / Peer Rule + YCbCr 雙重約束)
inline bool isStrictSkinPixel(uint8_t r, uint8_t g, uint8_t b) {
    // 1. 基本色彩範圍與 RGB 比例約束
    if (r <= 65 || g <= 35 || b <= 20) return false;
    if (r <= g || r <= b) return false;
    if (r - g < 12) return false;
    int maxVal = std::max({r, g, b});
    int minVal = std::min({r, g, b});
    if (maxVal - minVal < 14) return false;

    // 2. YCbCr 經典膚色色度模型 (排除木質家具、黃色燈光與反光牆壁)
    float rf = static_cast<float>(r);
    float gf = static_cast<float>(g);
    float bf = static_cast<float>(b);
    float y  =  0.2990f * rf + 0.5870f * gf + 0.1140f * bf;
    float cb = 128.0f - 0.1687f * rf - 0.3313f * gf + 0.5000f * bf;
    float cr = 128.0f + 0.5000f * rf - 0.4187f * gf - 0.0813f * bf;

    return (y >= 40.0f && y <= 245.0f && cb >= 77.0f && cb <= 130.0f && cr >= 132.0f && cr <= 175.0f);
}

inline bool isSkinPixel(uint8_t c0, uint8_t c1, uint8_t c2, PixelFormat format) {
    if (format == PixelFormat::BGR888) {
        return isStrictSkinPixel(c2, c1, c0);
    }
    return isStrictSkinPixel(c0, c1, c2);
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
        // 階段一：人體與人臉特徵高精度掃描 (排除木質家具背景、黃光反射)
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

        // 嚴格特徵門檻 (排除散落家具背景反光，確保真人入鏡)
        int totalSamples = (width / step) * (height / step);
        int minSkinRequired = std::max(220, static_cast<int>(totalSamples * 0.0035f));

        diag.skinPixels = skinPixels;
        diag.minSkinRequired = minSkinRequired;
        diag.fillDensity = fillDensity;
        diag.aspectRatio = aspectRatio;

        // 在場人體特徵幾何檢驗
        if (skinPixels < minSkinRequired) {
            diag.unconfirmedReason = "鏡頭前未偵測到真人臉部膚色特徵 (使用者離座 / 採樣點 " + std::to_string(skinPixels) + " < " + std::to_string(minSkinRequired) + ")";
        } else if (boxW < static_cast<int>(width * 0.08f) || boxH < static_cast<int>(height * 0.10f)) {
            diag.unconfirmedReason = "目標特徵區域過小，非在座人體 (區域: " + std::to_string(boxW) + "x" + std::to_string(boxH) + ")";
        } else if (aspectRatio < 0.55f || aspectRatio > 2.80f) {
            diag.unconfirmedReason = "長寬比例不符合人臉頭部輪廓 (比例: " + std::to_string(aspectRatio) + ")";
        } else if (fillDensity < 0.08f) {
            diag.unconfirmedReason = "特徵分佈散亂，非連續臉部實體 (填充率: " + std::to_string(fillDensity) + ")";
        } else {
            detectedFaceCx = static_cast<float>(skinSumX) / skinPixels;
            detectedFaceCy = static_cast<float>(skinSumY) / skinPixels;
            detectedFaceScale = std::clamp(static_cast<float>(std::max(boxW, boxH)) * 0.88f,
                                           static_cast<float>(std::min(width, height)) * 0.18f,
                                           static_cast<float>(std::min(width, height)) * 0.95f);

            // 雙重確認：檢驗眼部/眉毛區域垂直梯度對比 (杜絕將木質家具或牆面誤認為真人)
            float eyeDist = detectedFaceScale * 0.28f;
            float eyeYOffset = detectedFaceScale * 0.10f;
            int eyeCenterY = static_cast<int>(detectedFaceCy - eyeYOffset);
            int eyeBoxRadius = std::max(6, static_cast<int>(detectedFaceScale * 0.09f));
            int eyeCentersX[2] = {
                static_cast<int>(detectedFaceCx - eyeDist),
                static_cast<int>(detectedFaceCx + eyeDist)
            };

            float totalEyeGrad = 0.0f;
            int eyeGradCount = 0;
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
                        totalEyeGrad += std::abs(lumBot - lumTop);
                        eyeGradCount++;
                    }
                }
            }

            float avgEyeGrad = (eyeGradCount > 0) ? (totalEyeGrad / eyeGradCount) : 0.0f;
            if (avgEyeGrad < 3.2f) {
                diag.unconfirmedReason = "候選區域缺乏人臉眼部對比度 (判定為靜態背景家具非真人)";
            } else {
                personInFrame = true;
            }
        }

        // ---------------------------------------------------------------------
        // 階段二：確認臉部朝向攝影機 (Face Orientation & Symmetry)
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

            float midBoxX = static_cast<float>(minSkinX + maxSkinX) * 0.5f;
            bool centerAligned = std::abs(detectedFaceCx - midBoxX) < (static_cast<float>(boxW) * 0.65f);

            // 正對鏡頭判定：容許微幅轉頭 (symmetryRatio >= 0.12)
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
        diag.skinPixels = personInFrame ? 450 : 0;
        diag.minSkinRequired = 200;
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
    // 方框隨使用者真實位置動態平滑移動，不強制鎖死在正中央！
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
            m_trackedCx = m_trackedCx * 0.65f + detectedFaceCx * 0.35f;
            m_trackedCy = m_trackedCy * 0.65f + detectedFaceCy * 0.35f;
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
        // 敏銳離座判定：連續 3 影格 (約 0.1 秒) 無訊號即刻確認離座，杜絕離座殘留！
        if (m_consecutiveMissingFrames >= 3) {
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
            // 實體相機：依據跟隨追蹤的使用者位置提取雙眼動態梯度
            float eyeDist = m_trackedScale * 0.28f;
            float eyeYOffset = m_trackedScale * 0.10f;
            int eyeBoxRadius = std::max(6, static_cast<int>(m_trackedScale * 0.08f));

            int eyeCentersX[2] = {
                static_cast<int>(m_trackedCx - eyeDist),
                static_cast<int>(m_trackedCx + eyeDist)
            };
            int eyeCenterY = static_cast<int>(m_trackedCy - eyeYOffset);

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

