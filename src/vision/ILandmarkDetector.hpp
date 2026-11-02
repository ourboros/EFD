#pragma once

#include "efd/types.hpp"
#include "vision/RawFrame.hpp"
#include <string>

namespace efd {

// 在座判定與偵測失敗原因的診斷資訊 (診斷模式、手機遙測 JSON 共用)
// 各偵測器只需填入自己量得到的欄位，其餘保持預設值
struct PresenceDiagnostic {
    bool rawPersonInFrame = false;
    bool rawFacingCamera = false;
    bool isFaceConfirmed = false;
    int skinPixels = 0;
    int minSkinRequired = 0;
    float fillDensity = 0.0f;
    float aspectRatio = 0.0f;
    float symmetryRatio = 0.0f;
    int boxX = 0;
    int boxY = 0;
    int boxW = 0;
    int boxH = 0;
    int frameWidth = 640;
    int frameHeight = 480;
    int stableCount = 0;
    float trackedCx = 0.0f;
    float trackedCy = 0.0f;
    float userBiasX = 0.0f;
    float userBiasY = 0.0f;
    bool isPositionBiasCalibrated = false;
    std::string unconfirmedReason;

    // 診斷模式用：本幀卡在哪一關 ("" 代表全部通過；skin / size / aspect / fill / eyes / symmetry)
    std::string failStage;
    // 眼睛或對稱檢查未過，但位置與已鎖定的臉相符而判定在座 (閉眼、低頭、眨眼)
    bool presenceByTracking = false;
    // 膚色連通區域 (未裁切) 與裁切後的臉部方框
    int skinBoxX = 0, skinBoxY = 0, skinBoxW = 0, skinBoxH = 0;
    int faceBoxX = 0, faceBoxY = 0, faceBoxW = 0, faceBoxH = 0;
    int avgSkinLum = 0;
    // 眼部檢查數值 [0] 左 / [1] 右，以及推算的眼睛中心
    float eyeDarkFraction[2] = {0.0f, 0.0f};
    float eyeContrast[2] = {0.0f, 0.0f};
    float eyeGradient[2] = {0.0f, 0.0f};
    int eyeCenterX[2] = {0, 0};
    int eyeCenterY = 0;
    // 追蹤階段的眼睛開合量測：雙眼暗點比例、睜眼基準 (中位數)、換算後的開合度 (0 閉 ~ 1 開)
    float eyeOpenDarkRatio = 0.0f;
    float eyeOpenBaseline = 0.0f;
    float eyeOpenness = 0.0f;
};

// 臉部特徵點偵測器介面：輸入一幀影像，輸出 468 點座標與在座判定
// 實作：FaceLandmarker (膚色 + 暗點啟發式，無模型時的備援)、之後的 ONNX Runtime 模型版本
class ILandmarkDetector {
public:
    virtual ~ILandmarkDetector() = default;

    // 載入模型 (啟發式偵測器忽略路徑)；回傳 false 代表無法使用，呼叫端應改用備援
    virtual bool initialize(const std::string& modelPath = "") = 0;
    virtual bool isReady() const = 0;
    // 偵測器名稱，寫入日誌與診斷資訊，用來分辨資料由哪一種方法產生
    virtual const char* name() const = 0;

    virtual LandmarkDetectionResult detect(const RawFrame& frame) = 0;
    virtual PresenceDiagnostic getLatestPresenceDiagnostic() const = 0;

    // 以下為選用功能，偵測器不支援時維持空實作
    // Synthetic 模式的模擬眼睛開合 (負值代表模擬離座)
    virtual void setSimulatedEyeOpenness(float /*openness*/) {}
    // 初次校準：記錄使用者常態偏向位置
    virtual void calibrateUserPositionBias(float /*cx*/, float /*cy*/) {}
    virtual bool isPositionBiasCalibrated() const { return false; }
    virtual void resetTracking() {}
    // 重新校準時清除睜眼基準
    virtual void resetEyeBaseline() {}
};

} // namespace efd
