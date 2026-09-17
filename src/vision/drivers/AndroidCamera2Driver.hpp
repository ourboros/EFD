#pragma once

#include "ICameraDriver.hpp"

#ifdef __ANDROID__
#include <camera/NdkCameraManager.h>
#include <camera/NdkCameraDevice.h>
#include <camera/NdkCameraCaptureSession.h>
#include <media/NdkImageReader.h>
#endif

namespace efd {

class AndroidCamera2Driver : public ICameraDriver {
public:
    AndroidCamera2Driver() = default;
    ~AndroidCamera2Driver() override { close(); }

    bool open(const CameraConfig& config) override {
        (void)config;
#ifdef __ANDROID__
        // Android NDK Camera2 前置鏡頭 (ACAMERA_LENS_FACING_FRONT) 初始化
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
#ifdef __ANDROID__
        // 列舉 Android 前鏡頭與後鏡頭
#endif
        return devs;
    }

    std::string getDriverName() const override { return "Android NDK Camera2"; }
};

} // namespace efd

