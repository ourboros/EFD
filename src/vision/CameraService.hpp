#pragma once

#include "vision/RawFrame.hpp"
#include "vision/drivers/ICameraDriver.hpp"
#include <functional>
#include <memory>
#include <vector>
#include <string>
#include <atomic>
#include <thread>
#include <mutex>

namespace efd {

class CameraService {
public:
    using FrameCallback = ICameraDriver::FrameCallback;

    explicit CameraService(int targetWidth = 640, int targetHeight = 480, float targetFps = 30.0f);
    ~CameraService();

    // 啟動攝影機 (自動依據平台尋找前置鏡頭，若無硬體自動回退至 Synthetic 模擬驅動)
    // 啟動攝影機 (自動依據平台尋找前置鏡頭，若無硬體或無串流則自動回退至 Synthetic 模擬驅動)
    bool start(int deviceIndex = 0, CameraFacing targetFacing = CameraFacing::Front);

    // 強制指定使用虛擬測試相機
    bool startSynthetic();

    // 設定是否強制使用虛擬測試模式 (供單元測試與模擬實驗使用)
    void setForceSynthetic(bool force) { m_forceSynthetic = force; }

    // 停止攝影機
    void stop();

    // 查詢是否正在運行
    bool isRunning() const;

    // 查詢當前是否為虛擬模擬回退模式
    bool isUsingSyntheticFallback() const;

    // 取得當前運作的驅動名稱
    std::string getActiveDriverName() const;

    // 列舉系統所有可用攝影機設備
    std::vector<CameraDeviceInfo> enumerateDevices();

    // 設定影格非同步接收回呼
    void setFrameCallback(FrameCallback callback);

    // 設定模擬眼睛開合狀態 (當處於 Synthetic 模式時有效)
    void setSimulatedEyeState(float openness);

    int getWidth() const { return m_config.width; }
    int getHeight() const { return m_config.height; }
    float getFps() const { return m_config.fps; }
    int64_t getTotalFramesDelivered() const { return m_totalFramesDelivered.load(); }

private:
    CameraConfig m_config;
    std::unique_ptr<ICameraDriver> m_driver;
    FrameCallback m_frameCallback;
    std::mutex m_driverMutex;

    bool m_isSyntheticFallback = false;
    bool m_forceSynthetic = false;
    std::atomic<int64_t> m_totalFramesDelivered{0};

    // 看門狗執行緒 (自動監測實體鏡頭取幀狀態)
    std::thread m_watchdogThread;
    std::atomic<bool> m_watchdogRunning{false};

    // 建立平台原生驅動實例
    std::unique_ptr<ICameraDriver> createPlatformDriver();
    void onDriverFrame(const RawFrame& frame);
    void startWatchdog();
    void stopWatchdog();
};

} // namespace efd
