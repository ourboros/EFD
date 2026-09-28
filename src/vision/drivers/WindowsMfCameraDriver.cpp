#include "WindowsMfCameraDriver.hpp"
#include <iostream>
#include <chrono>
#include <vector>
#include <algorithm>

#ifdef _WIN32
#pragma comment(lib, "mf.lib")
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "ole32.lib")

namespace efd {

namespace {

// 快速 YUY2 轉 RGB888 色彩空間轉換器
inline void yuy2ToRgb888(const uint8_t* yuy2, uint8_t* rgb, int numPixels) {
    for (int i = 0; i < numPixels; i += 2) {
        int y0 = yuy2[0];
        int u  = yuy2[1] - 128;
        int y1 = yuy2[2];
        int v  = yuy2[3] - 128;
        yuy2 += 4;

        int r0 = std::clamp(y0 + ((359 * v) >> 8), 0, 255);
        int g0 = std::clamp(y0 - ((88 * u + 183 * v) >> 8), 0, 255);
        int b0 = std::clamp(y0 + ((454 * u) >> 8), 0, 255);

        int r1 = std::clamp(y1 + ((359 * v) >> 8), 0, 255);
        int g1 = std::clamp(y1 - ((88 * u + 183 * v) >> 8), 0, 255);
        int b1 = std::clamp(y1 + ((454 * u) >> 8), 0, 255);

        rgb[0] = static_cast<uint8_t>(r0);
        rgb[1] = static_cast<uint8_t>(g0);
        rgb[2] = static_cast<uint8_t>(b0);
        rgb[3] = static_cast<uint8_t>(r1);
        rgb[4] = static_cast<uint8_t>(g1);
        rgb[5] = static_cast<uint8_t>(b1);
        rgb += 6;
    }
}

// 快速 BGRA/BGRX 轉 RGB888 色彩空間轉換器
inline void bgraToRgb888(const uint8_t* bgra, uint8_t* rgb, int numPixels) {
    for (int i = 0; i < numPixels; ++i) {
        rgb[0] = bgra[2]; // R
        rgb[1] = bgra[1]; // G
        rgb[2] = bgra[0]; // B
        bgra += 4;
        rgb += 3;
    }
}

} // anonymous namespace

std::string WindowsMfCameraDriver::wcharToString(const wchar_t* wstr) {
    if (!wstr) return "";
    int len = WideCharToMultiByte(CP_UTF8, 0, wstr, -1, NULL, 0, NULL, NULL);
    if (len <= 0) return "";
    std::string str(len - 1, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr, -1, &str[0], len, NULL, NULL);
    return str;
}

WindowsMfCameraDriver::WindowsMfCameraDriver() {
    CoInitializeEx(NULL, COINIT_MULTITHREADED);
    HRESULT hr = MFStartup(MF_VERSION);
    m_isMfInitialized = SUCCEEDED(hr);
}

WindowsMfCameraDriver::~WindowsMfCameraDriver() {
    close();
    if (m_isMfInitialized) {
        MFShutdown();
    }
    CoUninitialize();
}

std::vector<CameraDeviceInfo> WindowsMfCameraDriver::enumerateDevices() {
    std::vector<CameraDeviceInfo> devices;
    if (!m_isMfInitialized) return devices;

    IMFAttributes* pAttributes = nullptr;
    HRESULT hr = MFCreateAttributes(&pAttributes, 1);
    if (FAILED(hr)) return devices;

    hr = pAttributes->SetGUID(
        MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
        MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID
    );

    if (SUCCEEDED(hr)) {
        IMFActivate** ppDevices = nullptr;
        UINT32 count = 0;
        hr = MFEnumDeviceSources(pAttributes, &ppDevices, &count);

        if (SUCCEEDED(hr) && count > 0) {
            for (UINT32 i = 0; i < count; ++i) {
                CameraDeviceInfo dev;
                dev.id = static_cast<int>(i);

                WCHAR* nameBuffer = nullptr;
                UINT32 nameLen = 0;
                if (SUCCEEDED(ppDevices[i]->GetAllocatedString(
                    MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME, &nameBuffer, &nameLen))) {
                    dev.name = wcharToString(nameBuffer);
                    CoTaskMemFree(nameBuffer);
                } else {
                    dev.name = "Camera Device " + std::to_string(i);
                }

                WCHAR* linkBuffer = nullptr;
                UINT32 linkLen = 0;
                if (SUCCEEDED(ppDevices[i]->GetAllocatedString(
                    MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK, &linkBuffer, &linkLen))) {
                    dev.symbolicLink = wcharToString(linkBuffer);
                    CoTaskMemFree(linkBuffer);
                }

                // 鏡頭朝向判斷
                std::string lowerName = dev.name;
                for (char& c : lowerName) c = static_cast<char>(tolower(c));
                if (lowerName.find("front") != std::string::npos ||
                    lowerName.find("integrated") != std::string::npos ||
                    lowerName.find("facetime") != std::string::npos ||
                    lowerName.find("webcam") != std::string::npos ||
                    i == 0) {
                    dev.facing = CameraFacing::Front;
                } else {
                    dev.facing = CameraFacing::Back;
                }

                devices.push_back(dev);
                ppDevices[i]->Release();
            }
            CoTaskMemFree(ppDevices);
        }
    }

    if (pAttributes) pAttributes->Release();
    return devices;
}

bool WindowsMfCameraDriver::open(const CameraConfig& config) {
    if (m_isRunning.load()) {
        close();
    }

    if (!m_isMfInitialized) return false;

    m_config = config;

    // 1. 列舉設備
    IMFAttributes* pAttributes = nullptr;
    HRESULT hr = MFCreateAttributes(&pAttributes, 1);
    if (FAILED(hr)) return false;

    hr = pAttributes->SetGUID(
        MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
        MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID
    );

    if (FAILED(hr)) {
        pAttributes->Release();
        return false;
    }

    IMFActivate** ppDevices = nullptr;
    UINT32 count = 0;
    hr = MFEnumDeviceSources(pAttributes, &ppDevices, &count);
    pAttributes->Release();

    if (FAILED(hr) || count == 0) {
        return false;
    }

    UINT32 targetIndex = static_cast<UINT32>(config.deviceIndex);
    if (targetIndex >= count) targetIndex = 0;

    // 2. 啟動 MediaSource
    hr = ppDevices[targetIndex]->ActivateObject(IID_PPV_ARGS(&m_pMediaSource));
    for (UINT32 i = 0; i < count; ++i) {
        ppDevices[i]->Release();
    }
    CoTaskMemFree(ppDevices);

    if (FAILED(hr) || !m_pMediaSource) {
        return false;
    }

    // 3. 建立 SourceReader (優先啟用標準 Video Processing，若失敗回退至標準模式)
    IMFAttributes* pReaderAttributes = nullptr;
    if (SUCCEEDED(MFCreateAttributes(&pReaderAttributes, 2))) {
        pReaderAttributes->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE);
        pReaderAttributes->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, TRUE);
    }

    hr = MFCreateSourceReaderFromMediaSource(m_pMediaSource, pReaderAttributes, &m_pSourceReader);
    if (pReaderAttributes) {
        pReaderAttributes->Release();
        pReaderAttributes = nullptr;
    }

    // 若帶屬性建立失敗，自動回退至 NULL 屬性建立
    if (FAILED(hr) || !m_pSourceReader) {
        std::cout << "  -> 帶屬性建立 SourceReader (0x" << std::hex << hr << std::dec << ")，自動回退為標準 SourceReader 模式...\n";
        hr = MFCreateSourceReaderFromMediaSource(m_pMediaSource, NULL, &m_pSourceReader);
    }

    if (FAILED(hr) || !m_pSourceReader) {
        std::cerr << "  -> 建立 SourceReader 失敗! HRESULT = 0x" << std::hex << hr << std::dec << "\n";
        if (m_pMediaSource) {
            m_pMediaSource->Release();
            m_pMediaSource = nullptr;
        }
        return false;
    }

    // 4. 枚舉相機支援的原生格式清單
    std::cout << "[WindowsMfCameraDriver] 相機原生支援格式清單 (Native Media Types):\n";
    DWORD typeIndex = 0;
    IMFMediaType* pNativeType = nullptr;
    while (SUCCEEDED(m_pSourceReader->GetNativeMediaType(
        static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), typeIndex, &pNativeType))) {
        UINT32 nw = 0, nh = 0;
        MFGetAttributeSize(pNativeType, MF_MT_FRAME_SIZE, &nw, &nh);
        GUID subtype = { 0 };
        pNativeType->GetGUID(MF_MT_SUBTYPE, &subtype);

        std::string subName = "Other";
        if (subtype == MFVideoFormat_YUY2) subName = "YUY2";
        else if (subtype == MFVideoFormat_NV12) subName = "NV12";
        else if (subtype == MFVideoFormat_MJPG) subName = "MJPG";
        else if (subtype == MFVideoFormat_RGB24) subName = "RGB24";
        else if (subtype == MFVideoFormat_RGB32) subName = "RGB32";

        if (typeIndex < 8) {
            std::cout << "  [" << typeIndex << "] " << nw << "x" << nh << " (" << subName << ")\n";
        }
        pNativeType->Release();
        pNativeType = nullptr;
        typeIndex++;
    }

    // 5. 自動協商輸出格式：優先要求 RGB24，若失敗嘗試 RGB32 / YUY2
    bool formatConfigured = false;
    const GUID targetFormats[] = { MFVideoFormat_RGB24, MFVideoFormat_RGB32, MFVideoFormat_YUY2 };

    for (const auto& fmt : targetFormats) {
        IMFMediaType* pMediaType = nullptr;
        if (SUCCEEDED(MFCreateMediaType(&pMediaType))) {
            pMediaType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
            pMediaType->SetGUID(MF_MT_SUBTYPE, fmt);
            int targetW = (config.width > 0) ? config.width : 640;
            int targetH = (config.height > 0) ? config.height : 480;
            MFSetAttributeSize(pMediaType, MF_MT_FRAME_SIZE, targetW, targetH);

            if (SUCCEEDED(m_pSourceReader->SetCurrentMediaType(
                static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), NULL, pMediaType))) {
                formatConfigured = true;
                pMediaType->Release();
                break;
            }
            pMediaType->Release();
        }
    }

    // 若設定自訂解析度失敗，嘗試僅要求 RGB24 色彩轉換（不強制縮放解析度）
    if (!formatConfigured) {
        IMFMediaType* pMediaType = nullptr;
        if (SUCCEEDED(MFCreateMediaType(&pMediaType))) {
            pMediaType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
            pMediaType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB24);
            if (SUCCEEDED(m_pSourceReader->SetCurrentMediaType(
                static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), NULL, pMediaType))) {
                formatConfigured = true;
            }
            pMediaType->Release();
        }
    }

    // 讀取當前最終生效的輸出格式與解析度
    IMFMediaType* pCurrentType = nullptr;
    if (SUCCEEDED(m_pSourceReader->GetCurrentMediaType(
        static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), &pCurrentType)) && pCurrentType) {
        UINT32 cw = 0, ch = 0;
        MFGetAttributeSize(pCurrentType, MF_MT_FRAME_SIZE, &cw, &ch);
        pCurrentType->GetGUID(MF_MT_SUBTYPE, &m_actualSubtype);
        m_actualWidth = (cw > 0) ? static_cast<int>(cw) : 640;
        m_actualHeight = (ch > 0) ? static_cast<int>(ch) : 480;
        pCurrentType->Release();
    } else {
        m_actualWidth = (config.width > 0) ? config.width : 640;
        m_actualHeight = (config.height > 0) ? config.height : 480;
        m_actualSubtype = MFVideoFormat_RGB24;
    }

    std::cout << "[WindowsMfCameraDriver] 格式協商完成! 生效解析度: " << m_actualWidth << "x" << m_actualHeight << "\n";

    m_isRunning.store(true);
    m_workerThread = std::thread(&WindowsMfCameraDriver::captureLoop, this);
    return true;
}

void WindowsMfCameraDriver::close() {
    if (m_isRunning.load()) {
        m_isRunning.store(false);
        if (m_workerThread.joinable()) {
            m_workerThread.join();
        }
    }

    if (m_pSourceReader) {
        m_pSourceReader->Release();
        m_pSourceReader = nullptr;
    }

    if (m_pMediaSource) {
        m_pMediaSource->Shutdown();
        m_pMediaSource->Release();
        m_pMediaSource = nullptr;
    }
}

bool WindowsMfCameraDriver::isOpened() const {
    return m_isRunning.load();
}

void WindowsMfCameraDriver::setFrameCallback(FrameCallback callback) {
    m_frameCallback = std::move(callback);
}

void WindowsMfCameraDriver::captureLoop() {
    CoInitializeEx(NULL, COINIT_MULTITHREADED);

    float fps = (m_config.fps > 0.0f) ? m_config.fps : 30.0f;
    const auto minFrameInterval = std::chrono::microseconds(static_cast<int64_t>(1000000.0f / (fps * 1.2f)));

    int targetW = m_actualWidth > 0 ? m_actualWidth : 640;
    int targetH = m_actualHeight > 0 ? m_actualHeight : 480;
    int numPixels = targetW * targetH;

    std::vector<uint8_t> rgbBuffer(numPixels * 3, 0);

    while (m_isRunning.load()) {
        auto loopStart = std::chrono::steady_clock::now();

        DWORD streamIndex = 0;
        DWORD flags = 0;
        LONGLONG timestamp = 0;
        IMFSample* pSample = nullptr;

        if (!m_pSourceReader) break;

        HRESULT hr = m_pSourceReader->ReadSample(
            static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM),
            0,
            &streamIndex,
            &flags,
            &timestamp,
            &pSample
        );

        if (SUCCEEDED(hr) && pSample) {
            IMFMediaBuffer* pBuffer = nullptr;
            hr = pSample->ConvertToContiguousBuffer(&pBuffer);

            if (SUCCEEDED(hr) && pBuffer) {
                BYTE* pData = nullptr;
                DWORD currentLength = 0;
                hr = pBuffer->Lock(&pData, NULL, &currentLength);

                if (SUCCEEDED(hr) && pData && currentLength > 0) {
                    RawFrame frame;
                    frame.width = targetW;
                    frame.height = targetH;
                    frame.channels = 3;
                    frame.format = PixelFormat::RGB888;
                    frame.timestampMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::system_clock::now().time_since_epoch()).count();

                    // 依據緩衝區大小自動適配格式
                    if (currentLength == static_cast<DWORD>(numPixels * 3)) {
                        // 標準 RGB24
                        frame.data.assign(pData, pData + currentLength);
                    } else if (currentLength == static_cast<DWORD>(numPixels * 4)) {
                        // BGRA32 轉 RGB888
                        bgraToRgb888(pData, rgbBuffer.data(), numPixels);
                        frame.data = rgbBuffer;
                    } else if (currentLength == static_cast<DWORD>(numPixels * 2)) {
                        // YUY2 轉 RGB888
                        yuy2ToRgb888(pData, rgbBuffer.data(), numPixels);
                        frame.data = rgbBuffer;
                    } else {
                        // 其他尺寸直接複製
                        frame.data.assign(pData, pData + currentLength);
                    }

                    if (m_frameCallback) {
                        m_frameCallback(frame);
                    }

                    pBuffer->Unlock();
                }
                pBuffer->Release();
            }
            pSample->Release();
        } else {
            // 稍作休眠以避免緊密迴圈佔用 CPU
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - loopStart);
        if (elapsed < minFrameInterval) {
            std::this_thread::sleep_for(minFrameInterval - elapsed);
        }
    }

    CoUninitialize();
}

} // namespace efd

#endif
