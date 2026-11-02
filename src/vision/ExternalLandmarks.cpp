#include "vision/ExternalLandmarks.hpp"
#include <algorithm>
#include <cmath>

namespace efd {

LandmarkDetectionResult buildExternalDetection(const float* normalizedXyz, size_t pointCount,
                                               int width, int height,
                                               float blinkLeft, float blinkRight) {
    LandmarkDetectionResult result;
    result.landmarksMeasured = true;
    result.eyeBlinkLeft = blinkLeft;
    result.eyeBlinkRight = blinkRight;
    if (!normalizedXyz || pointCount < kMinLandmarkCount || width <= 0 || height <= 0) {
        return result;
    }

    const float w = static_cast<float>(width);
    const float h = static_cast<float>(height);
    result.landmarks.resize(pointCount);
    for (size_t i = 0; i < pointCount; ++i) {
        const float x = normalizedXyz[i * 3 + 0];
        const float y = normalizedXyz[i * 3 + 1];
        const float z = normalizedXyz[i * 3 + 2];
        if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) {
            result.landmarks.clear();
            return result;
        }
        result.landmarks[i] = Point3D{x * w, y * h, z * w};
    }
    result.hasFace = true;
    result.faceConfidence = 1.0f;
    return result;
}

PresenceDiagnostic buildExternalDiagnostic(const LandmarkDetectionResult& detection, int width, int height) {
    PresenceDiagnostic diag;
    diag.frameWidth = width;
    diag.frameHeight = height;
    diag.rawPersonInFrame = detection.hasFace;
    diag.rawFacingCamera = detection.hasFace;
    diag.isFaceConfirmed = detection.hasFace;
    if (!detection.hasFace || detection.landmarks.empty()) {
        diag.failStage = "model";
        diag.unconfirmedReason = "MediaPipe 未偵測到臉部";
        return diag;
    }

    float minX = detection.landmarks.front().x, maxX = minX;
    float minY = detection.landmarks.front().y, maxY = minY;
    for (const Point3D& p : detection.landmarks) {
        minX = std::min(minX, p.x);
        maxX = std::max(maxX, p.x);
        minY = std::min(minY, p.y);
        maxY = std::max(maxY, p.y);
    }
    diag.faceBoxX = diag.boxX = static_cast<int>(minX);
    diag.faceBoxY = diag.boxY = static_cast<int>(minY);
    diag.faceBoxW = diag.boxW = static_cast<int>(maxX - minX);
    diag.faceBoxH = diag.boxH = static_cast<int>(maxY - minY);
    diag.trackedCx = (minX + maxX) * 0.5f;
    diag.trackedCy = (minY + maxY) * 0.5f;
    diag.aspectRatio = (maxX > minX) ? (maxY - minY) / (maxX - minX) : 0.0f;
    if (detection.eyeBlinkLeft >= 0.0f && detection.eyeBlinkRight >= 0.0f) {
        diag.eyeOpenness = std::clamp(1.0f - (detection.eyeBlinkLeft + detection.eyeBlinkRight) * 0.5f, 0.0f, 1.0f);
    }
    return diag;
}

bool ExternalPresenceFilter::shouldSubmit(bool hasFace, int64_t nowMs) {
    if (hasFace) {
        m_lastFaceMs = nowMs;
        return true;
    }
    // 從未看到臉 (剛啟動) 直接回報沒有臉；看到過則等遺失滿 holdMs 才回報
    return m_lastFaceMs < 0 || nowMs - m_lastFaceMs >= m_holdMs;
}

void ExternalPresenceFilter::reset() {
    m_lastFaceMs = -1;
}

} // namespace efd
