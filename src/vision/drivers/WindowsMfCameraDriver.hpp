#pragma once

#include "ICameraDriver.hpp"
#include "PixelConverters.hpp"
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

    // 相機實際輸出的格式 (由 SourceReader 查詢，不假設 640x480 / RGB)
    int m_actualWidth = 0;
    int m_actualHeight = 0;
    int m_actualStride = 0;   // 每行位元組數；負數代表由下而上
    pixel::RawFormat m_actualFormat = pixel::RawFormat::Unknown;

    void captureLoop();
    void selectNativeMediaType(int targetWidth, int targetHeight);
    bool requestOutputSubtype(const GUID& subtype, UINT32 width, UINT32 height);
    bool refreshCurrentMediaType();
    static std::string wcharToString(const wchar_t* wstr);
#endif
};

} // namespace efd

