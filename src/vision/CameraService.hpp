#pragma once

#include "vision/RawFrame.hpp"
#include <functional>
#include <memory>
#include <thread>
#include <atomic>
#include <string>

namespace efd {

class CameraService {
public:
    using FrameCallback = std::function<void(const RawFrame& frame)>;

    CameraService(int targetWidth = 640, int targetHeight = 480, float targetFps = 30.0f);
    ~CameraService();

    bool start(int deviceIndex = 0);
    void stop();
    bool isRunning() const;
    void setFrameCallback(FrameCallback callback);
    void setSimulatedEyeState(float openness);

    int getWidth() const { return m_width; }
    int getHeight() const { return m_height; }
    float getFps() const { return m_fps; }

private:
    int m_width;
    int m_height;
    float m_fps;

    std::atomic<bool> m_isRunning{false};
    std::thread m_workerThread;
    FrameCallback m_frameCallback;
    std::atomic<float> m_simulatedOpenness{1.0f};

    void captureLoop();
};

} // namespace efd

