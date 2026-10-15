#pragma once

#include "ICameraDriver.hpp"
#include <memory>

namespace efd {

class AppleAvFoundationCameraDriver : public ICameraDriver {
public:
    AppleAvFoundationCameraDriver();
    ~AppleAvFoundationCameraDriver() override;

    bool open(const CameraConfig& config) override;
    void close() override;
    bool isOpened() const override;
    void setFrameCallback(FrameCallback callback) override;
    std::vector<CameraDeviceInfo> enumerateDevices() override;
    std::string getDriverName() const override { return "Apple AVFoundation (Front Camera)"; }

    void* getCaptureSessionPtr() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace efd
