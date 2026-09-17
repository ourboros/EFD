#pragma once

#include "ICameraDriver.hpp"
#include <thread>
#include <atomic>
#include <vector>

namespace efd {

class SyntheticCameraDriver : public ICameraDriver {
public:
    SyntheticCameraDriver();
    ~SyntheticCameraDriver() override;

    bool open(const CameraConfig& config) override;
    void close() override;
    bool isOpened() const override;
    void setFrameCallback(FrameCallback callback) override;
    std::vector<CameraDeviceInfo> enumerateDevices() override;
    std::string getDriverName() const override { return "Synthetic Test Driver"; }
    void setSimulatedEyeState(float openness) override;

private:
    CameraConfig m_config;
    std::atomic<bool> m_isRunning{false};
    std::thread m_workerThread;
    FrameCallback m_frameCallback;
    std::atomic<float> m_simulatedOpenness{1.0f};

    void captureLoop();
};

} // namespace efd

