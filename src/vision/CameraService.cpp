#include "CameraService.hpp"
#include "drivers/SyntheticCameraDriver.hpp"

#ifdef _WIN32
#include "drivers/WindowsMfCameraDriver.hpp"
#elif defined(__ANDROID__)
#include "drivers/AndroidCamera2Driver.hpp"
#elif defined(__APPLE__)
#include "drivers/AppleAvFoundationCameraDriver.hpp"
#endif

#include <iostream>
#include <chrono>

namespace efd {

CameraService::CameraService(int targetWidth, int targetHeight, float targetFps) {
    m_config.width = targetWidth;
    m_config.height = targetHeight;
    m_config.fps = targetFps;
    m_config.targetFacing = CameraFacing::Front;
    m_config.deviceIndex = 0;
}

CameraService::~CameraService() {
    stop();
}

std::unique_ptr<ICameraDriver> CameraService::createPlatformDriver() {
#ifdef _WIN32
    return std::make_unique<WindowsMfCameraDriver>();
#elif defined(__ANDROID__)
    return std::make_unique<AndroidCamera2Driver>();
#elif defined(__APPLE__)
    return std::make_unique<AppleAvFoundationCameraDriver>();
#else
    return std::make_unique<SyntheticCameraDriver>();
#endif
}

std::vector<CameraDeviceInfo> CameraService::enumerateDevices() {
    auto driver = createPlatformDriver();
    auto devices = driver->enumerateDevices();
    if (devices.empty()) {
        SyntheticCameraDriver synthetic;
        return synthetic.enumerateDevices();
    }
    return devices;
}

void CameraService::onDriverFrame(const RawFrame& frame) {
    m_totalFramesDelivered++;
    if (m_frameCallback) {
        m_frameCallback(frame);
    }
}

bool CameraService::start(int deviceIndex, CameraFacing targetFacing) {
    stop();

    m_config.deviceIndex = deviceIndex;
    m_config.targetFacing = targetFacing;
    m_totalFramesDelivered.store(0);

    if (m_forceSynthetic) {
        return startSynthetic();
    }

    // 1. 優先嘗試建立並啟動平台原生攝影機 (前置鏡頭優先)
    bool opened = false;
    {
        std::lock_guard<std::mutex> lock(m_driverMutex);
        m_driver = createPlatformDriver();
        m_driver->setFrameCallback([this](const RawFrame& f) {
            this->onDriverFrame(f);
        });

        auto devices = m_driver->enumerateDevices();
        if (!devices.empty()) {
            int chosenIndex = deviceIndex;
            for (const auto& dev : devices) {
                if (dev.facing == targetFacing) {
                    chosenIndex = dev.id;
                    break;
                }
            }
            m_config.deviceIndex = chosenIndex;
            opened = m_driver->open(m_config);
        }
    }

    if (opened) {
        m_isSyntheticFallback = false;
        std::cout << "[CameraService] 成功啟動實體攝影機驅動: " << m_driver->getDriverName() 
                  << " (Index: " << m_config.deviceIndex << ")\n";
        // 啟動看門狗: 若 1.0 秒內實體相機無任何畫面送出，自動切換至模擬驅動
        startWatchdog();
        return true;
    }

    // 2. 若無法開啟實體鏡頭（無硬體或權限受限），無縫回退至 Synthetic 模擬驅動
    std::cout << "[CameraService] 未偵測到可用的實體攝影機或開啟失敗，自動回退至 Synthetic 模擬測試驅動...\n";
    return startSynthetic();
}

bool CameraService::startSynthetic() {
    stop();
    m_forceSynthetic = true;
    stopWatchdog();
    
    std::lock_guard<std::mutex> lock(m_driverMutex);
    m_driver = std::make_unique<SyntheticCameraDriver>();
    m_driver->setFrameCallback([this](const RawFrame& f) {
        this->onDriverFrame(f);
    });
    
    m_isSyntheticFallback = true;
    bool ok = m_driver->open(m_config);
    std::cout << "[CameraService] 啟用 Synthetic 模擬測試攝影機驅動 (30 FPS 影像串流運作中)\n";
    return ok;
}

void CameraService::startWatchdog() {
    stopWatchdog();
    m_watchdogRunning.store(true);
    m_watchdogThread = std::thread([this]() {
        // 等待 1000 毫秒
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        if (!m_watchdogRunning.load()) return;

        // 若 1 秒內未收到任何影格 (實體相機被佔用、無權限或無訊號)
        if (m_totalFramesDelivered.load() == 0 && !m_isSyntheticFallback) {
            std::cout << "[CameraService 看門狗] 偵測到實體攝影機無訊號輸出，自動無縫熱切換至 Synthetic 模擬驅動...\n";
            this->startSynthetic();
        }
    });
}

void CameraService::stopWatchdog() {
    if (m_watchdogRunning.load()) {
        m_watchdogRunning.store(false);
        if (m_watchdogThread.joinable()) {
            m_watchdogThread.join();
        }
    }
}

void CameraService::stop() {
    stopWatchdog();
    std::lock_guard<std::mutex> lock(m_driverMutex);
    if (m_driver) {
        m_driver->close();
        m_driver.reset();
    }
}

bool CameraService::isRunning() const {
    return m_driver && m_driver->isOpened();
}

bool CameraService::isUsingSyntheticFallback() const {
    return m_isSyntheticFallback;
}

std::string CameraService::getActiveDriverName() const {
    return m_driver ? m_driver->getDriverName() : "None";
}

void CameraService::setFrameCallback(FrameCallback callback) {
    m_frameCallback = std::move(callback);
    std::lock_guard<std::mutex> lock(m_driverMutex);
    if (m_driver) {
        m_driver->setFrameCallback([this](const RawFrame& f) {
            this->onDriverFrame(f);
        });
    }
}

void CameraService::setSimulatedEyeState(float openness) {
    std::lock_guard<std::mutex> lock(m_driverMutex);
    if (m_driver) {
        m_driver->setSimulatedEyeState(openness);
    }
}

} // namespace efd
