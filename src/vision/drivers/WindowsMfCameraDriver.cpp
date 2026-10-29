#include "WindowsMfCameraDriver.hpp"
#include <iostream>
#include <chrono>
#include <vector>
#include <algorithm>
#include <cstdlib>

#ifdef _WIN32
#pragma comment(lib, "mf.lib")
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "ole32.lib")

namespace efd {

namespace {

pixel::RawFormat rawFormatFromSubtype(const GUID& subtype) {
    if (subtype == MFVideoFormat_NV12)  return pixel::RawFormat::NV12;
    if (subtype == MFVideoFormat_YUY2)  return pixel::RawFormat::YUY2;
    if (subtype == MFVideoFormat_UYVY)  return pixel::RawFormat::UYVY;
    if (subtype == MFVideoFormat_RGB24) return pixel::RawFormat::RGB24;
    if (subtype == MFVideoFormat_RGB32) return pixel::RawFormat::RGB32;
    return pixel::RawFormat::Unknown;
}

const char* rawFormatName(pixel::RawFormat format) {
    switch (format) {
    case pixel::RawFormat::NV12:  return "NV12";
    case pixel::RawFormat::YUY2:  return "YUY2";
    case pixel::RawFormat::UYVY:  return "UYVY";
    case pixel::RawFormat::RGB24: return "RGB24";
    case pixel::RawFormat::RGB32: return "RGB32";
    default:                      return "未支援";
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

    // 3. 建立 SourceReader (僅啟用基本色彩轉換；ADVANCED_VIDEO_PROCESSING 會導致 0x80070057)
    IMFAttributes* pReaderAttributes = nullptr;
    MFCreateAttributes(&pReaderAttributes, 1);
    if (pReaderAttributes) {
        pReaderAttributes->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE);
    }

    hr = MFCreateSourceReaderFromMediaSource(m_pMediaSource, pReaderAttributes, &m_pSourceReader);
    if (pReaderAttributes) pReaderAttributes->Release();

    // 若帶屬性失敗，嘗試以 NULL 屬性（原始模式）建立 SourceReader
    if (FAILED(hr) || !m_pSourceReader) {
        std::cout << "[WindowsMfCameraDriver] SourceReader 帶屬性建立失敗 (HRESULT=0x"
                  << std::hex << hr << std::dec << ")，回退 NULL 屬性模式...\n";
        hr = MFCreateSourceReaderFromMediaSource(m_pMediaSource, nullptr, &m_pSourceReader);
    }

    if (FAILED(hr) || !m_pSourceReader) {
        std::cout << "[WindowsMfCameraDriver] 建立 SourceReader 失敗! HRESULT = 0x"
                  << std::hex << hr << std::dec << "\n";
        if (m_pMediaSource) {
            m_pMediaSource->Release();
            m_pMediaSource = nullptr;
        }
        return false;
    }

    // 4. 設定輸出格式：
    //    (1) 從相機原生格式中挑最接近目標解析度者 (相機不一定支援 640x480，硬要求會失敗)
    //    (2) 請 SourceReader 只做色彩轉換成 RGB32 / RGB24 (同解析度；MJPG 等壓縮格式也會自動解碼)
    //    (3) 轉換失敗時保留原生 NV12 / YUY2 / UYVY 等格式，由本驅動自行解碼
    //    最後一律查詢實際生效的格式、解析度與 stride，不再假設 640x480 RGB
    const int targetW = config.width > 0 ? config.width : 640;
    const int targetH = config.height > 0 ? config.height : 480;
    selectNativeMediaType(targetW, targetH);

    UINT32 nativeW = 0, nativeH = 0;
    {
        IMFMediaType* current = nullptr;
        if (SUCCEEDED(m_pSourceReader->GetCurrentMediaType(
                static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), &current)) && current) {
            MFGetAttributeSize(current, MF_MT_FRAME_SIZE, &nativeW, &nativeH);
            current->Release();
        }
    }
    // 原生解析度大於目標寬度時，請系統依原比例縮小 (例如 1280x720 -> 640x360，不把 16:9 壓成 4:3)；
    // 不支援縮放的電腦則改用原生解析度
    UINT32 scaledW = nativeW, scaledH = nativeH;
    if (nativeW > static_cast<UINT32>(targetW) && nativeH > 0) {
        scaledW = static_cast<UINT32>(targetW);
        scaledH = static_cast<UINT32>((static_cast<unsigned long long>(nativeH) * targetW / nativeW) & ~1ULL);
    }
    const bool converted =
        requestOutputSubtype(MFVideoFormat_RGB32, scaledW, scaledH) ||
        requestOutputSubtype(MFVideoFormat_RGB24, scaledW, scaledH) ||
        requestOutputSubtype(MFVideoFormat_RGB32, nativeW, nativeH) ||
        requestOutputSubtype(MFVideoFormat_RGB24, nativeW, nativeH);
    if (!converted) {
        std::cout << "[WindowsMfCameraDriver] 系統無法轉成 RGB，改由驅動直接解碼原生格式\n";
    }
    if (!refreshCurrentMediaType() || m_actualFormat == pixel::RawFormat::Unknown) {
        std::cout << "[WindowsMfCameraDriver] 相機輸出格式無法解碼，放棄使用此鏡頭\n";
        m_pSourceReader->Release();
        m_pSourceReader = nullptr;
        m_pMediaSource->Shutdown();
        m_pMediaSource->Release();
        m_pMediaSource = nullptr;
        return false;
    }

    m_isRunning.store(true);
    m_workerThread = std::thread(&WindowsMfCameraDriver::captureLoop, this);
    return true;
}

void WindowsMfCameraDriver::selectNativeMediaType(int targetWidth, int targetHeight) {
    const DWORD stream = static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM);
    IMFMediaType* best = nullptr;
    long long bestScore = -1;
    UINT32 bestW = 0, bestH = 0;
    double bestFps = 0.0;
    DWORD nativeCount = 0;

    for (DWORD i = 0; ; ++i) {
        IMFMediaType* type = nullptr;
        if (FAILED(m_pSourceReader->GetNativeMediaType(stream, i, &type)) || !type) break;

        UINT32 w = 0, h = 0, num = 0, den = 0;
        MFGetAttributeSize(type, MF_MT_FRAME_SIZE, &w, &h);
        MFGetAttributeRatio(type, MF_MT_FRAME_RATE, &num, &den);
        const double fps = (den > 0) ? static_cast<double>(num) / den : 30.0;

        // 解析度越接近目標越好；低於 15 fps 的模式不適合眨眼偵測
        long long score = std::llabs(static_cast<long long>(w) - targetWidth) +
                          std::llabs(static_cast<long long>(h) - targetHeight);
        if (fps < 15.0) score += 100000;
        if (w == 0 || h == 0) score += 1000000;

        ++nativeCount;
        if (bestScore < 0 || score < bestScore) {
            if (best) best->Release();
            best = type;
            bestScore = score;
            bestW = w;
            bestH = h;
            bestFps = fps;
        } else {
            type->Release();
        }
    }

    if (best) {
        const HRESULT hr = m_pSourceReader->SetCurrentMediaType(stream, NULL, best);
        std::cout << "[WindowsMfCameraDriver] 相機原生格式 " << nativeCount << " 種，選用 " << bestW << "x" << bestH
                  << " @" << static_cast<int>(bestFps + 0.5) << "fps" << (SUCCEEDED(hr) ? "" : " (設定失敗，沿用預設)") << "\n";
        best->Release();
    }
}

bool WindowsMfCameraDriver::requestOutputSubtype(const GUID& subtype, UINT32 w, UINT32 h) {
    const DWORD stream = static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM);
    IMFMediaType* type = nullptr;
    if (FAILED(MFCreateMediaType(&type))) return false;
    type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    type->SetGUID(MF_MT_SUBTYPE, subtype);
    if (w > 0 && h > 0) {
        MFSetAttributeSize(type, MF_MT_FRAME_SIZE, w, h);
    }
    const bool ok = SUCCEEDED(m_pSourceReader->SetCurrentMediaType(stream, NULL, type));
    type->Release();
    return ok;
}

bool WindowsMfCameraDriver::refreshCurrentMediaType() {
    IMFMediaType* current = nullptr;
    if (FAILED(m_pSourceReader->GetCurrentMediaType(
            static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), &current)) || !current) {
        return false;
    }

    UINT32 w = 0, h = 0;
    MFGetAttributeSize(current, MF_MT_FRAME_SIZE, &w, &h);
    GUID subtype = GUID_NULL;
    current->GetGUID(MF_MT_SUBTYPE, &subtype);

    m_actualWidth = static_cast<int>(w);
    m_actualHeight = static_cast<int>(h);
    m_actualFormat = rawFormatFromSubtype(subtype);

    // stride：優先使用 MF_MT_DEFAULT_STRIDE (負數代表由下而上)，否則依格式計算
    UINT32 strideAttr = 0;
    if (SUCCEEDED(current->GetUINT32(MF_MT_DEFAULT_STRIDE, &strideAttr))) {
        m_actualStride = static_cast<int>(static_cast<INT32>(strideAttr));
    } else {
        LONG stride = 0;
        if (SUCCEEDED(MFGetStrideForBitmapInfoHeader(subtype.Data1, w, &stride)) && stride != 0) {
            m_actualStride = static_cast<int>(stride);
        } else {
            m_actualStride = pixel::minimumStride(m_actualFormat, m_actualWidth);
        }
    }
    current->Release();

    std::cout << "[WindowsMfCameraDriver] 相機實際輸出格式: " << rawFormatName(m_actualFormat) << " "
              << m_actualWidth << "x" << m_actualHeight << " stride=" << m_actualStride << "\n";
    return m_actualWidth > 0 && m_actualHeight > 0;
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
    // 關鍵修復：工作執行緒必須初始化 COM 才能在 Media Foundation 中調用 ReadSample
    CoInitializeEx(NULL, COINIT_MULTITHREADED);

    float fps = (m_config.fps > 0.0f) ? m_config.fps : 30.0f;
    const auto minFrameInterval = std::chrono::microseconds(static_cast<int64_t>(1000000.0f / (fps * 1.2f)));

    std::vector<uint8_t> rgbBuffer;
    const int targetWidth = m_config.width > 0 ? m_config.width : 640;

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

        // 相機中途切換格式 (例如插拔、驅動調整) 時重新查詢
        if (SUCCEEDED(hr) && (flags & MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED)) {
            refreshCurrentMediaType();
        }

        if (SUCCEEDED(hr) && pSample) {
            IMFMediaBuffer* pBuffer = nullptr;
            hr = pSample->ConvertToContiguousBuffer(&pBuffer);

            if (SUCCEEDED(hr) && pBuffer) {
                const int w = m_actualWidth;
                const int h = m_actualHeight;
                const pixel::RawFormat format = m_actualFormat;

                // 優先用 IMF2DBuffer 取得實際的行首與 pitch (含行尾補齊、由下而上的負 pitch)
                IMF2DBuffer* p2D = nullptr;
                BYTE* pTopRow = nullptr;
                LONG pitch = 0;
                DWORD bufferLength = 0;
                bool locked2D = false;
                BYTE* pData = nullptr;
                pBuffer->GetCurrentLength(&bufferLength);
                if (SUCCEEDED(pBuffer->QueryInterface(IID_PPV_ARGS(&p2D))) && p2D) {
                    if (SUCCEEDED(p2D->Lock2D(&pTopRow, &pitch))) {
                        locked2D = true;
                    }
                }
                if (!locked2D) {
                    // 一般緩衝區：stride 取自媒體類型；負 stride 時第一行 (畫面最上方) 在緩衝區尾端
                    if (SUCCEEDED(pBuffer->Lock(&pData, NULL, &bufferLength)) && pData) {
                        pitch = (m_actualStride != 0) ? m_actualStride : pixel::minimumStride(format, w);
                        pTopRow = (pitch < 0) ? pData + static_cast<ptrdiff_t>(-pitch) * (h - 1) : pData;
                    }
                }

                const int absPitch = (pitch < 0) ? -pitch : pitch;
                const size_t needed = pixel::requiredBytes(format, absPitch, h);
                const bool sizeOk = pTopRow && w > 0 && h > 0 &&
                                    absPitch >= pixel::minimumStride(format, w) &&
                                    needed > 0 && needed <= bufferLength;

                if (sizeOk) {
                    rgbBuffer.resize(static_cast<size_t>(w) * h * 3);
                    switch (format) {
                    case pixel::RawFormat::NV12:  pixel::nv12ToRgb888(pTopRow, pitch, w, h, rgbBuffer.data()); break;
                    case pixel::RawFormat::YUY2:  pixel::yuy2ToRgb888(pTopRow, pitch, w, h, rgbBuffer.data()); break;
                    case pixel::RawFormat::UYVY:  pixel::uyvyToRgb888(pTopRow, pitch, w, h, rgbBuffer.data()); break;
                    case pixel::RawFormat::RGB24: pixel::bgr24ToRgb888(pTopRow, pitch, w, h, rgbBuffer.data()); break;
                    case pixel::RawFormat::RGB32: pixel::bgraToRgb888(pTopRow, pitch, w, h, rgbBuffer.data()); break;
                    default: break;
                    }

                    RawFrame frame;
                    frame.channels = 3;
                    frame.format = PixelFormat::RGB888;
                    frame.timestampMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::system_clock::now().time_since_epoch()).count();

                    // 720p / 1080p 鏡頭：整數倍縮小到寬約 640 (偵測參數以此尺寸調校，與 iPhone 480x640 相近)
                    const int factor = std::max(1, w / std::max(1, targetWidth));
                    if (factor >= 2) {
                        frame.width = w / factor;
                        frame.height = h / factor;
                        frame.data.resize(static_cast<size_t>(frame.width) * frame.height * 3);
                        pixel::downscaleRgb888(rgbBuffer.data(), w, h, factor, frame.data.data());
                    } else {
                        frame.width = w;
                        frame.height = h;
                        frame.data = rgbBuffer;
                    }

                    if (m_frameCallback) {
                        m_frameCallback(frame);
                    }
                }
                // 緩衝區長度不符合格式時略過該幀 (避免越界讀取或把原始資料當成 RGB)

                if (locked2D) {
                    p2D->Unlock2D();
                } else if (pData) {
                    pBuffer->Unlock();
                }
                if (p2D) p2D->Release();
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
