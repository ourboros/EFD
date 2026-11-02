#pragma once

#include "vision/ILandmarkDetector.hpp"
#include <cstddef>
#include <cstdint>

namespace efd {

// 平台層 (Android MediaPipe Tasks) 偵測結果的轉換
// MediaPipe 輸出 0~1 正規化座標：x 以寬、y 以高為基準，z 與 x 同尺度。
// 必須乘回像素再算 EAR，否則非正方形畫面 (例如 480x640) 的 EAR 會失真
constexpr size_t kMediaPipeLandmarkCount = 478;   // 468 點臉部網格 + 10 點虹膜
constexpr size_t kMinLandmarkCount = 468;          // FeatureExtractor 需要的最少點數

// normalizedXyz 為 pointCount 組 (x, y, z)；nullptr 或點數不足視為沒有臉
// blinkLeft / blinkRight 為 MediaPipe blendshape 分數 (0 睜 ~ 1 閉)，負值代表未提供
LandmarkDetectionResult buildExternalDetection(const float* normalizedXyz, size_t pointCount,
                                               int width, int height,
                                               float blinkLeft = -1.0f, float blinkRight = -1.0f);

// 由偵測結果產生診斷資訊：臉框取特徵點外框；沒有臉時 failStage = "model"
PresenceDiagnostic buildExternalDiagnostic(const LandmarkDetectionResult& detection, int width, int height);

// 臉部短暫遺失 (轉頭、手遮臉) 不立即判定離座：遺失未滿 holdMs 的幀不遞交給引擎，
// 與啟發式偵測器「連續 1 秒未偵測到才判定離座」的規則一致
class ExternalPresenceFilter {
public:
    static constexpr int64_t kDefaultHoldMs = 1000;

    explicit ExternalPresenceFilter(int64_t holdMs = kDefaultHoldMs) : m_holdMs(holdMs) {}

    // 回傳 true 代表這幀應遞交給引擎
    bool shouldSubmit(bool hasFace, int64_t nowMs);
    void reset();

private:
    int64_t m_holdMs;
    int64_t m_lastFaceMs = -1;
};

} // namespace efd
