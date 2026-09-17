#pragma once

#include "ICameraDriver.hpp"

namespace efd {

class AppleAvFoundationCameraDriver : public ICameraDriver {
public:
    AppleAvFoundationCameraDriver() = default;
    ~AppleAvFoundationCameraDriver() override { close(); }

    bool open(const CameraConfig& config) override {
        (void)config;
#if defined(__APPLE__)
        // iOS / macOS AVFoundation 前置鏡頭 (AVCaptureDevicePositionFront) 初始化
        return true;
#else
        return false;
#endif
    }

    void close() override {}
    bool isOpened() const override { return false; }
    void setFrameCallback(FrameCallback callback) override { (void)callback; }

    std::vector<CameraDeviceInfo> enumerateDevices() override {
        std::vector<CameraDeviceInfo> devs;
#if defined(__APPLE__)
        // 列舉 Apple 設備鏡頭
#endif
        return devs;
    }

    std::string getDriverName() const override { return "Apple AVFoundation"; }
};

} // namespace efd

