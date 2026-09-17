#pragma once

#include "ICameraDriver.hpp"
#include <thread>
#include <atomic>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#endif

namespace efd {

class WindowsMfCameraDriver : public ICameraDriver {
public:
    WindowsMfCameraDriver();
    ~WindowsMfCameraDriver() override;

    bool open(const CameraConfig& config) override;
    void close() override;
    bool isOpened() const override;
    void setFrameCallback(FrameCallback callback) override;
    std::vector<CameraDeviceInfo> enumerateDevices() override;
    std::string getDriverName() const override { return "Windows Media Foundation"; }

private:
    CameraConfig m_config;
    std::atomic<bool> m_isRunning{false};
    std::thread m_workerThread;
    FrameCallback m_frameCallback;

#ifdef _WIN32
    bool m_isMfInitialized = false;
    IMFSourceReader* m_pSourceReader = nullptr;
    IMFMediaSource* m_pMediaSource = nullptr;

    void captureLoop();
    static std::string wcharToString(const wchar_t* wstr);
#endif
};

} // namespace efd

