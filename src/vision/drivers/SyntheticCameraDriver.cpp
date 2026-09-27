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
        
        // 1. 深色背景底色
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                size_t idx = (y * width + x) * 3;
                frameBuffer[idx]     = 25;
                frameBuffer[idx + 1] = 30;
                frameBuffer[idx + 2] = 40;
            }
        }

        // 2. 繪製模擬人臉橢圓 (若模擬睜眼度 >= 0)
        float openVal = m_simulatedOpenness.load();
        if (openVal >= 0.0f) {
            float cx = width * 0.5f;
            float cy = height * 0.5f;
            float rx = width * 0.22f;
            float ry = height * 0.32f;

            for (int y = static_cast<int>(cy - ry); y <= static_cast<int>(cy + ry); ++y) {
                if (y < 0 || y >= height) continue;
                float dy = (y - cy) / ry;
                float maxDx = std::sqrt(std::max(0.0f, 1.0f - dy * dy)) * rx;
                int minX = std::max(0, static_cast<int>(cx - maxDx));
                int maxX = std::min(width - 1, static_cast<int>(cx + maxDx));

                for (int x = minX; x <= maxX; ++x) {
                    size_t idx = (y * width + x) * 3;
                    // 標準膚色 (R=220, G=165, B=130)
                    frameBuffer[idx]     = 220;
                    frameBuffer[idx + 1] = 165;
                    frameBuffer[idx + 2] = 130;
                }
            }

            // 左右眼部暗槽與眼眉特徵 (Eye Troughs)
            int eyeY = static_cast<int>(cy - ry * 0.22f);
            int leftEyeX = static_cast<int>(cx - rx * 0.45f);
            int rightEyeX = static_cast<int>(cx + rx * 0.45f);
            int eyeRadius = std::max(3, static_cast<int>(rx * 0.16f));

            for (int ey = eyeY - eyeRadius; ey <= eyeY + eyeRadius; ++ey) {
                if (ey < 0 || ey >= height) continue;
                for (int ex = leftEyeX - eyeRadius; ex <= leftEyeX + eyeRadius; ++ex) {
                    if (ex < 0 || ex >= width) continue;
                    size_t idx = (ey * width + ex) * 3;
                    frameBuffer[idx] = 40;
                    frameBuffer[idx + 1] = 30;
                    frameBuffer[idx + 2] = 30;
                }
                for (int ex = rightEyeX - eyeRadius; ex <= rightEyeX + eyeRadius; ++ex) {
                    if (ex < 0 || ex >= width) continue;
                    size_t idx = (ey * width + ex) * 3;
                    frameBuffer[idx] = 40;
                    frameBuffer[idx + 1] = 30;
                    frameBuffer[idx + 2] = 30;
                }
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

