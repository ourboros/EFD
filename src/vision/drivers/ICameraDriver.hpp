#pragma once

#include "vision/RawFrame.hpp"
#include <string>
#include <vector>
#include <functional>
#include <memory>
#include <cstdint>

namespace efd {

// 攝影機方向 (手機前鏡頭、後鏡頭、外接 USB 鏡頭)
enum class CameraFacing : uint8_t {
    Front,    // 前置鏡頭 (手機前鏡頭、筆電內建鏡頭) - 預設眼動追蹤
    Back,     // 後置鏡頭
    External, // 外接 USB 攝影機
    Unknown   // 未知
};

// 攝影機設備資訊
struct CameraDeviceInfo {
    int id = 0;
    std::string name;
    std::string symbolicLink;
    CameraFacing facing = CameraFacing::Front;
    int preferredWidth = 640;
    int preferredHeight = 480;
    float preferredFps = 30.0f;
};

// 攝影機採集設定
struct CameraConfig {
    int width = 640;
    int height = 480;
    float fps = 30.0f;
    CameraFacing targetFacing = CameraFacing::Front;
    int deviceIndex = 0;
};

// 跨平台相機硬體抽象層介面 (Camera HAL Interface)
class ICameraDriver {
public:
    using FrameCallback = std::function<void(const RawFrame& frame)>;

    virtual ~ICameraDriver() = default;

    // 開啟攝影機
    virtual bool open(const CameraConfig& config) = 0;

    // 關閉攝影機
    virtual void close() = 0;

    // 查詢攝影機是否正在運行
    virtual bool isOpened() const = 0;

    // 註冊影格非同步接收回呼
    virtual void setFrameCallback(FrameCallback callback) = 0;

    // 列舉系統可用攝影機設備
    virtual std::vector<CameraDeviceInfo> enumerateDevices() = 0;

    // 取得當前驅動名稱 (例如 "Windows Media Foundation", "Android Camera2", "Synthetic")
    virtual std::string getDriverName() const = 0;

    // 設定模擬眼睛開合度 (僅適用於 Synthetic 驅動，其餘驅動忽略)
    virtual void setSimulatedEyeState(float openness) { (void)openness; }
};

} // namespace efd

