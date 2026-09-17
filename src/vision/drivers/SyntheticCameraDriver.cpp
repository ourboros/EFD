#include "SyntheticCameraDriver.hpp"
#include <chrono>
#include <cmath>

namespace efd {

SyntheticCameraDriver::SyntheticCameraDriver() = default;

SyntheticCameraDriver::~SyntheticCameraDriver() {
    close();
}

bool SyntheticCameraDriver::open(const CameraConfig& config) {
    if (m_isRunning.load()) {
        close();
    }

    m_config = config;
    m_isRunning.store(true);
    m_workerThread = std::thread(&SyntheticCameraDriver::captureLoop, this);
    return true;
}

void SyntheticCameraDriver::close() {
    if (m_isRunning.load()) {
        m_isRunning.store(false);
        if (m_workerThread.joinable()) {
            m_workerThread.join();
        }
    }
}

bool SyntheticCameraDriver::isOpened() const {
    return m_isRunning.load();
}

void SyntheticCameraDriver::setFrameCallback(FrameCallback callback) {
    m_frameCallback = std::move(callback);
}

void SyntheticCameraDriver::setSimulatedEyeState(float openness) {
    m_simulatedOpenness.store(openness);
}

std::vector<CameraDeviceInfo> SyntheticCameraDriver::enumerateDevices() {
    CameraDeviceInfo virtualCam;
    virtualCam.id = 0;
    virtualCam.name = "Virtual Synthetic Eye Tracking Camera";
    virtualCam.symbolicLink = "virtual://synthetic_cam_0";
    virtualCam.facing = CameraFacing::Front;
    virtualCam.preferredWidth = 640;
    virtualCam.preferredHeight = 480;
    virtualCam.preferredFps = 30.0f;
    return { virtualCam };
}

void SyntheticCameraDriver::captureLoop() {
    float fps = (m_config.fps > 0.0f) ? m_config.fps : 30.0f;
    const auto frameInterval = std::chrono::microseconds(static_cast<int64_t>(1000000.0f / fps));
    int width = (m_config.width > 0) ? m_config.width : 640;
    int height = (m_config.height > 0) ? m_config.height : 480;

    std::vector<uint8_t> frameBuffer(width * height * 3, 0);
    int frameCount = 0;

    while (m_isRunning.load()) {
        auto frameStart = std::chrono::steady_clock::now();

        frameCount++;
        uint8_t tint = static_cast<uint8_t>((frameCount * 3) % 255);
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                size_t idx = (y * width + x) * 3;
                frameBuffer[idx]     = static_cast<uint8_t>((x * 255) / width);
                frameBuffer[idx + 1] = static_cast<uint8_t>((y * 255) / height);
                frameBuffer[idx + 2] = tint;
            }
        }

        RawFrame frame;
        frame.width = width;
        frame.height = height;
        frame.channels = 3;
        frame.format = PixelFormat::RGB888;
        frame.data = frameBuffer;
        frame.timestampMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();

        if (m_frameCallback) {
            m_frameCallback(frame);
        }

        auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - frameStart);
        if (elapsed < frameInterval) {
            std::this_thread::sleep_for(frameInterval - elapsed);
        }
    }
}

} // namespace efd

