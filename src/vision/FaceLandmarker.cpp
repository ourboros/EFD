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

// 膚色與人體特徵光學色彩模型 (嚴格排除紫色椅子、深色布料、木質家具與冷色光源)
inline bool isStrictSkinPixel(uint8_t r, uint8_t g, uint8_t b) {
    // 1. 基本色彩範圍約束
    if (r < 75 || g < 40 || b < 20) return false;

    // 2. 人體血紅素與黑色素光學定律：R 必須大於 G，G 必須明顯大於 B！
    // 紫色或深藍色椅子特徵：B >= G 或 B 與 G 極度接近；真人皮膚必定 G 顯著大於 B (短波藍光被黑色素強烈吸收)
    if (g <= b + 12) return false; // 關鍵特徵：100% 杜絕任何紫色、紫紅色、藍色椅子與布料誤判！
    if (r <= g + 15) return false; // 紅色必須顯著高於綠色 (血液充盈特徵)
    if (r <= b + 30) return false; // 紅色必須遠高於藍色

    // 3. YCbCr 經典膚色色度模型
    float rf = static_cast<float>(r);
    float gf = static_cast<float>(g);
    float bf = static_cast<float>(b);
    float y  =  0.2990f * rf + 0.5870f * gf + 0.1140f * bf;
    float cb = 128.0f - 0.1687f * rf - 0.3313f * gf + 0.5000f * bf;
    float cr = 128.0f + 0.5000f * rf - 0.4187f * gf - 0.0813f * bf;

    // 真人血液血紅素吸收使 Cr (紅色) 明顯高於 Cb (藍色)；木頭/牆面/反光則 Cr 與 Cb 接近
    if (cr <= cb + 16.0f) return false;

    return (y >= 50.0f && y <= 245.0f && cb >= 80.0f && cb <= 126.0f && cr >= 135.0f && cr <= 175.0f);
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
        // 階段一：人體與人臉特徵高精度掃描 (全視野動態覆蓋)
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

        // 合理特徵門檻：以 640x480 為例需至少 250 個採樣點 (代表實際逾千個真人膚色畫素)
        int totalSamples = (width / step) * (height / step);
        int minSkinRequired = std::max(250, static_cast<int>(totalSamples * 0.012f));

        diag.skinPixels = skinPixels;
        diag.minSkinRequired = minSkinRequired;
        diag.fillDensity = fillDensity;
        diag.aspectRatio = aspectRatio;

        // ---------------------------------------------------------------------
        // 順序 1：判斷畫面中是否有人臉頭部實體 (尺寸、輪廓長寬比與實體連續性)
        // ---------------------------------------------------------------------
        if (skinPixels < minSkinRequired) {
            diag.unconfirmedReason = "鏡頭前未偵測到真人臉部 (使用者不在場 / 已離開)";
        } else if (boxW < static_cast<int>(width * 0.08f) || boxH < static_cast<int>(height * 0.10f)) {
            diag.unconfirmedReason = "目標特徵區域過小，非正常在座人體 (區域: " + std::to_string(boxW) + "x" + std::to_string(boxH) + ")";
        } else if (aspectRatio < 0.95f || aspectRatio > 1.85f) {
            diag.unconfirmedReason = "長寬比例非真人臉部頭部輪廓 (比例: " + std::to_string(aspectRatio).substr(0, 4) + ", 判定為背景家具)";
        } else if (fillDensity < 0.14f) {
            diag.unconfirmedReason = "特徵分佈散亂非實體人臉 (填充率: " + std::to_string(fillDensity).substr(0, 4) + ", 判定為雜訊反光)";
        } else {
            // 人體/頭部幾何定位 (避免脖子/胸口將中心下拉)
            detectedFaceCx = (minSkinX + maxSkinX) * 0.5f;
            int faceH = std::min(boxH, static_cast<int>(boxW * 1.35f));
            detectedFaceCy = minSkinY + faceH * 0.50f;
            detectedFaceScale = std::clamp(static_cast<float>(std::max(boxW, faceH)),
                                           static_cast<float>(std::min(width, height)) * 0.15f,
                                           static_cast<float>(std::min(width, height)) * 0.98f);

            // 雙眼真實解剖位置檢驗 (眼睛位於頭頂至下巴的 35%~42% 處)
            int eyeCenterY = static_cast<int>(minSkinY + faceH * 0.38f);
            int eyeLeftX = static_cast<int>(detectedFaceCx - boxW * 0.22f);
            int eyeRightX = static_cast<int>(detectedFaceCx + boxW * 0.22f);
            int eyeBoxRadius = std::clamp(static_cast<int>(boxW * 0.10f), 8, 36);

            int eyeCentersX[2] = { eyeLeftX, eyeRightX };
            float totalGrad = 0.0f;
            int gradSamples = 0;
            float totalContrast = 0.0f;

            for (int e = 0; e < 2; ++e) {
                int ecx = eyeCentersX[e];
                int ecy = eyeCenterY;
                int minLum = 255;
                int maxLum = 0;

                for (int dy = -eyeBoxRadius; dy <= eyeBoxRadius - 2; dy += 2) {
                    int py = ecy + dy;
                    if (py <= 1 || py >= height - 2) continue;
                    for (int dx = -eyeBoxRadius; dx <= eyeBoxRadius; dx += 2) {
                        int px = ecx + dx;
                        if (px <= 0 || px >= width) continue;

                        int idx = (py * width + px) * 3;
                        int lum = (pixelData[idx] * 299 + pixelData[idx + 1] * 587 + pixelData[idx + 2] * 114) / 1000;
                        if (lum < minLum) minLum = lum;
                        if (lum > maxLum) maxLum = lum;

                        int idxTop = ((py - 1) * width + px) * 3;
                        int idxBot = ((py + 1) * width + px) * 3;
                        int lumTop = (pixelData[idxTop] * 299 + pixelData[idxTop + 1] * 587 + pixelData[idxTop + 2] * 114) / 1000;
                        int lumBot = (pixelData[idxBot] * 299 + pixelData[idxBot + 1] * 587 + pixelData[idxBot + 2] * 114) / 1000;
                        totalGrad += std::abs(lumBot - lumTop);
                        gradSamples++;
                    }
                }
                totalContrast += static_cast<float>(std::max(0, maxLum - minLum));
            }

            float avgEyeGrad = (gradSamples > 0) ? (totalGrad / gradSamples) : 0.0f;
            float avgContrast = totalContrast * 0.5f;

            // ---------------------------------------------------------------------
            // 順序 2：判斷使用者人臉是否面對鏡頭 (雙眼垂直梯度反差 + 水平左右對稱度)
            // ---------------------------------------------------------------------
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

            // 雙眼特徵與朝向鏡頭多重驗證
            bool hasBilateralEyes = (avgContrast >= 15.0f && avgEyeGrad >= 2.0f);
            bool isFaceSymmetric = (symmetryRatio >= 0.40f);

            if (!hasBilateralEyes) {
                diag.unconfirmedReason = "特徵區域缺乏人臉雙眼特徵 (未面對鏡頭或非真人臉部)";
            } else if (!isFaceSymmetric) {
                diag.unconfirmedReason = "使用者未正視鏡頭 (側臉、偏頭或轉身, 對稱度: " + std::to_string(symmetryRatio).substr(0, 4) + ")";
            } else {
                personInFrame = true;
                facingCamera = true;
                diag.unconfirmedReason = "使用者面對鏡頭正視中 (雙眼特徵鎖定)";
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
        diag.skinPixels = personInFrame ? 850 : 0;
        diag.minSkinRequired = 250;
        diag.fillDensity = personInFrame ? 0.35f : 0.0f;
        diag.aspectRatio = 1.33f;
        diag.symmetryRatio = personInFrame ? 0.95f : 0.0f;
        if (!personInFrame) {
            diag.unconfirmedReason = "Synthetic 模擬輸入為離座 (openness < 0)";
        }
    }

    diag.rawPersonInFrame = personInFrame;
    diag.isFacingCamera = facingCamera;

    // -------------------------------------------------------------------------
    // 順序 3：使用者人臉確認面對鏡頭 -> 啟動人臉辨識 (動態平滑追蹤)
    // -------------------------------------------------------------------------
    bool rawFacingActive = (personInFrame && facingCamera);
    if (rawFacingActive) {
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

        // 連續 3 影格確認正視鏡頭 -> 正式啟動人臉辨識與特徵點提取！
        if (m_consecutiveFaceFrames >= 3) {
            m_isFaceConfirmed = true;
            diag.isRecognitionActive = true;
        }
    } else {
        m_consecutiveMissingFrames++;
        m_consecutiveFaceFrames = 0;
        // 若未正視鏡頭或離座連續超過 4 影格 (約 0.13 秒)，立即暫停人臉辨識，杜絕殘留假疲勞特徵
        if (m_consecutiveMissingFrames >= 4) {
            m_isFaceConfirmed = false;
            diag.isRecognitionActive = false;
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

