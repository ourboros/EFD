#include "vision/OnnxFaceLandmarker.hpp"
#include "vision/ExternalLandmarks.hpp"

#include <onnxruntime_cxx_api.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <vector>

namespace efd {

namespace {

constexpr int kDetectorSize = 128;
constexpr int kLandmarkSize = 256;
constexpr int kAnchorCount = 896;
constexpr int kRegressorStride = 16;      // 每個 anchor：框 (cx, cy, w, h) + 6 個關鍵點 (x, y)
constexpr float kMinDetectionScore = 0.5f;
constexpr float kMinPresenceScore = 0.5f;
constexpr float kRoiScale = 1.5f;         // MediaPipe face_landmarker：裁切邊長 = 臉框長邊 x 1.5
constexpr int kRightEyeOuter = 33;        // 追蹤時以 33 -> 263 眼角連線決定旋轉角
constexpr int kLeftEyeOuter = 263;

// 旋轉裁切範圍 (原圖像素座標)
struct Roi {
    float cx = 0.0f;
    float cy = 0.0f;
    float size = 0.0f;
    float angle = 0.0f;   // 弧度，把臉轉正所需的旋轉
};

float sigmoid(float x) {
    return 1.0f / (1.0f + std::exp(-std::clamp(x, -80.0f, 80.0f)));
}

// 依像素格式讀取 (x, y) 的 RGB；畫面外為黑色 (與 MediaPipe 補邊一致)
inline void readPixel(const RawFrame& f, int x, int y, float rgb[3]) {
    if (x < 0 || y < 0 || x >= f.width || y >= f.height) {
        rgb[0] = rgb[1] = rgb[2] = 0.0f;
        return;
    }
    const uint8_t* p = f.data.data() + (static_cast<size_t>(y) * f.width + x) * f.channels;
    switch (f.format) {
    case PixelFormat::BGR888:
        rgb[0] = p[2]; rgb[1] = p[1]; rgb[2] = p[0];
        break;
    case PixelFormat::Grayscale:
        rgb[0] = rgb[1] = rgb[2] = p[0];
        break;
    default:   // RGB888 / RGBA8888
        rgb[0] = p[0]; rgb[1] = p[1]; rgb[2] = p[2];
        break;
    }
}

// 雙線性取樣 (x, y 為連續座標，像素中心在 +0.5)
inline void sampleBilinear(const RawFrame& f, float x, float y, float rgb[3]) {
    const float fx = x - 0.5f;
    const float fy = y - 0.5f;
    const int x0 = static_cast<int>(std::floor(fx));
    const int y0 = static_cast<int>(std::floor(fy));
    const float ax = fx - static_cast<float>(x0);
    const float ay = fy - static_cast<float>(y0);
    float p00[3], p10[3], p01[3], p11[3];
    readPixel(f, x0, y0, p00);
    readPixel(f, x0 + 1, y0, p10);
    readPixel(f, x0, y0 + 1, p01);
    readPixel(f, x0 + 1, y0 + 1, p11);
    for (int c = 0; c < 3; ++c) {
        rgb[c] = p00[c] * (1 - ax) * (1 - ay) + p10[c] * ax * (1 - ay)
               + p01[c] * (1 - ax) * ay + p11[c] * ax * ay;
    }
}

// BlazeFace short range 的 SSD anchor：stride 8 每格 2 個、stride 16 每格 6 個，共 896 個
std::vector<std::array<float, 2>> buildAnchors() {
    std::vector<std::array<float, 2>> anchors;
    anchors.reserve(kAnchorCount);
    const int strides[2] = {8, 16};
    const int perCell[2] = {2, 6};
    for (int layer = 0; layer < 2; ++layer) {
        const int grid = kDetectorSize / strides[layer];
        for (int y = 0; y < grid; ++y) {
            for (int x = 0; x < grid; ++x) {
                for (int k = 0; k < perCell[layer]; ++k) {
                    anchors.push_back({(x + 0.5f) / grid, (y + 0.5f) / grid});
                }
            }
        }
    }
    return anchors;
}

} // namespace

struct OnnxFaceLandmarker::Impl {
    Ort::Env env{ORT_LOGGING_LEVEL_WARNING, "efd"};
    std::unique_ptr<Ort::Session> detector;
    std::unique_ptr<Ort::Session> landmarker;
    std::string detInput, detRegressors, detScores;
    std::string lmInput, lmPoints, lmPresence;
    std::vector<std::array<float, 2>> anchors = buildAnchors();
    std::vector<float> detTensor = std::vector<float>(kDetectorSize * kDetectorSize * 3);
    std::vector<float> lmTensor = std::vector<float>(kLandmarkSize * kLandmarkSize * 3);
    Ort::MemoryInfo memory = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);

    bool tracking = false;
    std::vector<Point3D> lastLandmarks;

    std::unique_ptr<Ort::Session> openSession(const std::filesystem::path& path) {
        Ort::SessionOptions options;
        // 背景長時間執行：2 條執行緒即足夠即時，避免佔滿 CPU
        options.SetIntraOpNumThreads(2);
        options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
        return std::make_unique<Ort::Session>(env, path.c_str(), options);
    }

    static std::string inputName(Ort::Session& s, size_t i) {
        Ort::AllocatorWithDefaultOptions alloc;
        return s.GetInputNameAllocated(i, alloc).get();
    }
    static std::string outputName(Ort::Session& s, size_t i) {
        Ort::AllocatorWithDefaultOptions alloc;
        return s.GetOutputNameAllocated(i, alloc).get();
    }

    // 臉部偵測：補邊成正方形 -> 128x128 (每個輸出像素取 2x2 平均，降低縮小時的鋸齒) -> [-1, 1]
    bool detectFace(const RawFrame& f, Roi& roi) {
        const float side = static_cast<float>(std::max(f.width, f.height));
        const float padX = (side - f.width) * 0.5f;
        const float padY = (side - f.height) * 0.5f;
        const float step = side / kDetectorSize;
        float rgb[3];
        for (int oy = 0; oy < kDetectorSize; ++oy) {
            for (int ox = 0; ox < kDetectorSize; ++ox) {
                float sum[3] = {0, 0, 0};
                for (int sy = 0; sy < 2; ++sy) {
                    for (int sx = 0; sx < 2; ++sx) {
                        sampleBilinear(f, (ox + 0.25f + 0.5f * sx) * step - padX,
                                       (oy + 0.25f + 0.5f * sy) * step - padY, rgb);
                        sum[0] += rgb[0]; sum[1] += rgb[1]; sum[2] += rgb[2];
                    }
                }
                float* out = detTensor.data() + (static_cast<size_t>(oy) * kDetectorSize + ox) * 3;
                for (int c = 0; c < 3; ++c) out[c] = sum[c] * 0.25f / 127.5f - 1.0f;
            }
        }

        const int64_t shape[4] = {1, kDetectorSize, kDetectorSize, 3};
        Ort::Value input = Ort::Value::CreateTensor<float>(memory, detTensor.data(), detTensor.size(), shape, 4);
        const char* inNames[] = {detInput.c_str()};
        const char* outNames[] = {detRegressors.c_str(), detScores.c_str()};
        auto outputs = detector->Run(Ort::RunOptions{nullptr}, inNames, &input, 1, outNames, 2);
        const float* reg = outputs[0].GetTensorData<float>();
        const float* cls = outputs[1].GetTensorData<float>();

        int best = 0;
        for (int i = 1; i < kAnchorCount; ++i) {
            if (cls[i] > cls[best]) best = i;
        }
        if (sigmoid(cls[best]) < kMinDetectionScore) return false;

        const float* r = reg + static_cast<size_t>(best) * kRegressorStride;
        const float ax = anchors[best][0];
        const float ay = anchors[best][1];
        auto toPixelX = [&](float v) { return (v / kDetectorSize + ax) * side - padX; };
        auto toPixelY = [&](float v) { return (v / kDetectorSize + ay) * side - padY; };
        const float boxW = r[2] / kDetectorSize * side;
        const float boxH = r[3] / kDetectorSize * side;
        // 關鍵點 0、1 為兩眼 (影像中的右、左)
        const float rx = toPixelX(r[4]), ry = toPixelY(r[5]);
        const float lx = toPixelX(r[6]), ly = toPixelY(r[7]);
        roi.cx = toPixelX(r[0]);
        roi.cy = toPixelY(r[1]);
        roi.size = std::max(boxW, boxH) * kRoiScale;
        roi.angle = -std::atan2(-(ly - ry), lx - rx);
        return roi.size > 1.0f;
    }

    // 旋轉裁切 256x256 -> [0, 1] -> 478 點；回傳臉部存在分數 (0~1)，點座標換算回原圖像素
    float runLandmarks(const RawFrame& f, const Roi& roi, std::vector<Point3D>& points) {
        const float c = std::cos(roi.angle);
        const float s = std::sin(roi.angle);
        float rgb[3];
        for (int v = 0; v < kLandmarkSize; ++v) {
            const float vv = ((v + 0.5f) / kLandmarkSize - 0.5f) * roi.size;
            for (int u = 0; u < kLandmarkSize; ++u) {
                const float uu = ((u + 0.5f) / kLandmarkSize - 0.5f) * roi.size;
                sampleBilinear(f, roi.cx + uu * c - vv * s, roi.cy + uu * s + vv * c, rgb);
                float* out = lmTensor.data() + (static_cast<size_t>(v) * kLandmarkSize + u) * 3;
                out[0] = rgb[0] / 255.0f;
                out[1] = rgb[1] / 255.0f;
                out[2] = rgb[2] / 255.0f;
            }
        }

        const int64_t shape[4] = {1, kLandmarkSize, kLandmarkSize, 3};
        Ort::Value input = Ort::Value::CreateTensor<float>(memory, lmTensor.data(), lmTensor.size(), shape, 4);
        const char* inNames[] = {lmInput.c_str()};
        const char* outNames[] = {lmPoints.c_str(), lmPresence.c_str()};
        auto outputs = landmarker->Run(Ort::RunOptions{nullptr}, inNames, &input, 1, outNames, 2);
        const size_t count = outputs[0].GetTensorTypeAndShapeInfo().GetElementCount() / 3;
        const float* pts = outputs[0].GetTensorData<float>();
        const float presence = sigmoid(outputs[1].GetTensorData<float>()[0]);

        points.resize(count);
        for (size_t i = 0; i < count; ++i) {
            const float pu = pts[i * 3 + 0] / kLandmarkSize - 0.5f;
            const float pv = pts[i * 3 + 1] / kLandmarkSize - 0.5f;
            points[i].x = roi.cx + (pu * c - pv * s) * roi.size;
            points[i].y = roi.cy + (pu * s + pv * c) * roi.size;
            points[i].z = pts[i * 3 + 2] / kLandmarkSize * roi.size;
        }
        return presence;
    }

    // 追蹤用裁切範圍 (MediaPipe face_landmarks_to_roi)：特徵點軸對齊外框 + 眼角連線旋轉角
    static Roi roiFromLandmarks(const std::vector<Point3D>& p) {
        float minX = p[0].x, maxX = p[0].x, minY = p[0].y, maxY = p[0].y;
        for (size_t i = 1; i < kMinLandmarkCount && i < p.size(); ++i) {
            minX = std::min(minX, p[i].x);
            maxX = std::max(maxX, p[i].x);
            minY = std::min(minY, p[i].y);
            maxY = std::max(maxY, p[i].y);
        }
        Roi roi;
        roi.cx = (minX + maxX) * 0.5f;
        roi.cy = (minY + maxY) * 0.5f;
        roi.size = std::max(maxX - minX, maxY - minY) * kRoiScale;
        roi.angle = -std::atan2(-(p[kLeftEyeOuter].y - p[kRightEyeOuter].y), p[kLeftEyeOuter].x - p[kRightEyeOuter].x);
        return roi;
    }
};

OnnxFaceLandmarker::OnnxFaceLandmarker() = default;
OnnxFaceLandmarker::~OnnxFaceLandmarker() = default;

bool OnnxFaceLandmarker::initialize(const std::string& modelDir) {
    namespace fs = std::filesystem;
    try {
        auto impl = std::make_unique<Impl>();
        const fs::path dir(std::u8string(modelDir.begin(), modelDir.end()));
        impl->detector = impl->openSession(dir / kDetectorModel);
        impl->landmarker = impl->openSession(dir / kLandmarkModel);

        // 依轉換後的模型取得名稱：偵測 [regressors, classificators]；特徵點 [478x3 座標, 臉部存在分數, ...]
        impl->detInput = Impl::inputName(*impl->detector, 0);
        impl->detRegressors = Impl::outputName(*impl->detector, 0);
        impl->detScores = Impl::outputName(*impl->detector, 1);
        impl->lmInput = Impl::inputName(*impl->landmarker, 0);
        impl->lmPoints = Impl::outputName(*impl->landmarker, 0);
        impl->lmPresence = Impl::outputName(*impl->landmarker, 1);
        m_impl = std::move(impl);
        return true;
    } catch (const std::exception& e) {
        std::cerr << "[Vision] ONNX 臉部模型載入失敗 (" << modelDir << "): " << e.what() << "\n";
        m_impl.reset();
        return false;
    }
}

bool OnnxFaceLandmarker::isReady() const {
    return m_impl != nullptr;
}

LandmarkDetectionResult OnnxFaceLandmarker::detect(const RawFrame& frame) {
    const auto start = std::chrono::steady_clock::now();
    LandmarkDetectionResult result;
    result.landmarksMeasured = true;
    if (!m_impl || !frame.isValid()) {
        m_lastDiag = buildExternalDiagnostic(result, frame.width, frame.height);
        return result;
    }

    try {
        std::vector<Point3D> points;
        float presence = 0.0f;
        if (m_impl->tracking) {
            presence = m_impl->runLandmarks(frame, Impl::roiFromLandmarks(m_impl->lastLandmarks), points);
            m_impl->tracking = presence >= kMinPresenceScore;
        }
        if (!m_impl->tracking) {
            Roi roi;
            if (m_impl->detectFace(frame, roi)) {
                presence = m_impl->runLandmarks(frame, roi, points);
                m_impl->tracking = presence >= kMinPresenceScore;
            }
        }
        if (m_impl->tracking && points.size() >= kMinLandmarkCount) {
            m_impl->lastLandmarks = points;
            result.landmarks = std::move(points);
            result.hasFace = true;
            result.faceConfidence = presence;
        }
    } catch (const std::exception& e) {
        std::cerr << "[Vision] ONNX 推論失敗: " << e.what() << "\n";
        m_impl->tracking = false;
    }

    result.inferenceTimeMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count();
    m_lastDiag = buildExternalDiagnostic(result, frame.width, frame.height);
    return result;
}

PresenceDiagnostic OnnxFaceLandmarker::getLatestPresenceDiagnostic() const {
    return m_lastDiag;
}

void OnnxFaceLandmarker::resetTracking() {
    if (m_impl) m_impl->tracking = false;
}

} // namespace efd
