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

    // 1. 優先嘗試建立並啟動平台原生攝影機 (前置鏡頭優先，或依使用者指定的索引)
    bool opened = false;
    {
        std::lock_guard<std::mutex> lock(m_driverMutex);
        m_driver = createPlatformDriver();
        m_driver->setFrameCallback([this](const RawFrame& f) {
            this->onDriverFrame(f);
        });

        auto devices = m_driver->enumerateDevices();
        if (!devices.empty()) {
            int chosenIndex = -1;
            // 若使用者指定有效索引，優先採用
            if (deviceIndex >= 0 && deviceIndex < static_cast<int>(devices.size())) {
                chosenIndex = devices[deviceIndex].id;
            } else {
                for (const auto& dev : devices) {
                    if (dev.facing == targetFacing) {
                        chosenIndex = dev.id;
                        break;
                    }
                }
                if (chosenIndex < 0 && !devices.empty()) {
                    chosenIndex = devices[0].id;
                }
            }

            m_config.deviceIndex = chosenIndex;
            opened = m_driver->open(m_config);

            // 若選擇的設備開啟失敗，依序嘗試其餘可用實體相機
            if (!opened) {
                for (const auto& dev : devices) {
                    if (dev.id != chosenIndex) {
                        m_config.deviceIndex = dev.id;
                        if (m_driver->open(m_config)) {
                            opened = true;
                            break;
                        }
                    }
                }
            }
        }
    }

    if (opened) {
        m_isSyntheticFallback = false;
        std::cout << "[CameraService] 成功啟動實體攝影機: " << m_driver->getDriverName() 
                  << " (Device ID: " << m_config.deviceIndex << ")\n";
        return true;
    }

    // 2. 若無實體鏡頭或開啟失敗，自動回退至 Synthetic 模擬測試驅動
    std::cout << "[CameraService] 未偵測到可用的實體攝影機或開啟失敗，自動啟用 Synthetic 模擬測試驅動...\n";
    return startSynthetic();
}

bool CameraService::startSynthetic() {
    stopWatchdog();
    
    std::lock_guard<std::mutex> lock(m_driverMutex);
    if (m_driver) {
        m_driver->close();
        m_driver.reset();
    }
    m_driver = std::make_unique<SyntheticCameraDriver>();
    m_driver->setFrameCallback([this](const RawFrame& f) {
        this->onDriverFrame(f);
    });
    
    m_isSyntheticFallback = true;
    bool ok = m_driver->open(m_config);
    std::cout << "[CameraService] 啟用 Synthetic 模擬測試攝影機 (30 FPS 串流運作中)\n";
    return ok;
}

void CameraService::startWatchdog() {
    // 保留介面供擴展
}

void CameraService::stopWatchdog() {
    // 保留介面供擴展
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
