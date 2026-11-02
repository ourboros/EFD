#pragma once

#include "vision/ILandmarkDetector.hpp"
#include <memory>
#include <string>

namespace efd {

// MediaPipe 臉部模型 (由 face_landmarker.task 轉成 ONNX，見 models/README.md) 以 ONNX Runtime 推論：
// 臉部偵測 (BlazeFace，128x128) -> 依兩眼連線轉正裁切 -> 478 點特徵點 (256x256) -> 換算回原圖像素座標。
// 鎖定臉部後以上一幀特徵點決定裁切範圍，不必每幀重新偵測 (與 Android MediaPipe VIDEO 模式相同)。
// 只在推論執行緒 (Thread 2) 使用。
class OnnxFaceLandmarker : public ILandmarkDetector {
public:
    static constexpr const char* kDetectorModel = "face_detector.onnx";
    static constexpr const char* kLandmarkModel = "face_landmarks.onnx";

    OnnxFaceLandmarker();
    ~OnnxFaceLandmarker() override;

    // modelDir 內需有 face_detector.onnx 與 face_landmarks.onnx；任一載入失敗回傳 false
    bool initialize(const std::string& modelDir = "") override;
    bool isReady() const override;
    const char* name() const override { return "onnx"; }

    LandmarkDetectionResult detect(const RawFrame& frame) override;
    PresenceDiagnostic getLatestPresenceDiagnostic() const override;
    void resetTracking() override;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
    PresenceDiagnostic m_lastDiag;
};

// 建立模型偵測器：未以 ONNX Runtime 建置 (EFD_HAS_ONNX) 或模型載入失敗時回傳 nullptr，呼叫端沿用啟發式偵測
std::unique_ptr<ILandmarkDetector> createModelLandmarkDetector(const std::string& modelDir);

// 執行檔旁的 models 目錄 (建置與打包時把 ONNX 模型複製到這裡)
std::string defaultModelDirectory();

} // namespace efd
