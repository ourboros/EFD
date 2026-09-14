#include "vision/CameraService.hpp"
#include <chrono>
#include <vector>
#include <cmath>

namespace efd {

CameraService::CameraService(int targetWidth, int targetHeight, float targetFps)
    : m_width(targetWidth), m_height(targetHeight), m_fps(targetFps) {
}

CameraService::~CameraService() {
    stop();
}

bool CameraService::start(int deviceIndex) {
    (void)deviceIndex;
    if (m_isRunning.load()) {
        return true;
    }

    m_isRunning.store(true);
    m_workerThread = std::thread(&CameraService::captureLoop, this);
    return true;
}

void CameraService::stop() {
    if (m_isRunning.load()) {
        m_isRunning.store(false);
        if (m_workerThread.joinable()) {
            m_workerThread.join();
        }
    }
}

bool CameraService::isRunning() const {
    return m_isRunning.load();
}

void CameraService::setFrameCallback(FrameCallback callback) {
    m_frameCallback = std::move(callback);
}

void CameraService::setSimulatedEyeState(float openness) {
    m_simulatedOpenness.store(openness);
}

void CameraService::captureLoop() {
    const auto frameInterval = std::chrono::microseconds(static_cast<int64_t>(1000000.0f / m_fps));
    std::vector<uint8_t> frameBuffer(m_width * m_height * 3, 0);
    int frameCount = 0;

    while (m_isRunning.load()) {
        auto frameStart = std::chrono::steady_clock::now();

        frameCount++;
        uint8_t tint = static_cast<uint8_t>((frameCount * 2) % 255);
        for (int y = 0; y < m_height; ++y) {
            for (int x = 0; x < m_width; ++x) {
                size_t idx = (y * m_width + x) * 3;
                frameBuffer[idx]     = static_cast<uint8_t>((x * 255) / m_width);
                frameBuffer[idx + 1] = static_cast<uint8_t>((y * 255) / m_height);
                frameBuffer[idx + 2] = tint;
            }
        }

        RawFrame frame;
        frame.width = m_width;
        frame.height = m_height;
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

