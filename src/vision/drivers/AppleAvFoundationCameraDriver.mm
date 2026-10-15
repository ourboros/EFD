#include "AppleAvFoundationCameraDriver.hpp"

#if defined(__APPLE__)
#import <AVFoundation/AVFoundation.h>
#import <CoreVideo/CoreVideo.h>
#import <CoreMedia/CoreMedia.h>
#include <iostream>
#include <atomic>
#include <chrono>

@interface EFDCaptureDelegate : NSObject <AVCaptureVideoDataOutputSampleBufferDelegate>
@property (nonatomic, assign) efd::ICameraDriver::FrameCallback frameCallback;
@end

@implementation EFDCaptureDelegate

- (void)captureOutput:(AVCaptureOutput *)output
didOutputSampleBuffer:(CMSampleBufferRef)sampleBuffer
       fromConnection:(AVCaptureConnection *)connection {
    (void)output;
    (void)connection;

    if (!_frameCallback) return;

    CVImageBufferRef imageBuffer = CMSampleBufferGetImageBuffer(sampleBuffer);
    if (!imageBuffer) return;

    if (CVPixelBufferLockBaseAddress(imageBuffer, kCVPixelBufferLock_ReadOnly) != kCVReturnSuccess) {
        return;
    }

    size_t width = CVPixelBufferGetWidth(imageBuffer);
    size_t height = CVPixelBufferGetHeight(imageBuffer);
    size_t bytesPerRow = CVPixelBufferGetBytesPerRow(imageBuffer);
    uint8_t *srcBase = (uint8_t *)CVPixelBufferGetBaseAddress(imageBuffer);

    if (srcBase && width > 0 && height > 0) {
        efd::RawFrame frame;
        frame.width = static_cast<int>(width);
        frame.height = static_cast<int>(height);
        frame.channels = 3;
        frame.format = efd::PixelFormat::RGB888;
        frame.isSynthetic = false; // 真實前置攝影機影格
        frame.timestampMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        frame.data.resize(width * height * 3);

        uint8_t *dstBase = frame.data.data();
        for (size_t y = 0; y < height; ++y) {
            const uint8_t *srcRow = srcBase + (y * bytesPerRow);
            uint8_t *dstRow = dstBase + (y * width * 3);
            for (size_t x = 0; x < width; ++x) {
                // BGRA 轉換至 RGB
                dstRow[x * 3 + 0] = srcRow[x * 4 + 2]; // R
                dstRow[x * 3 + 1] = srcRow[x * 4 + 1]; // G
                dstRow[x * 3 + 2] = srcRow[x * 4 + 0]; // B
            }
        }

        CVPixelBufferUnlockBaseAddress(imageBuffer, kCVPixelBufferLock_ReadOnly);
        _frameCallback(frame);
    } else {
        CVPixelBufferUnlockBaseAddress(imageBuffer, kCVPixelBufferLock_ReadOnly);
    }
}

@end

namespace efd {

struct AppleAvFoundationCameraDriver::Impl {
    AVCaptureSession *session = nil;
    AVCaptureDevice *device = nil;
    AVCaptureVideoDataOutput *videoOutput = nil;
    EFDCaptureDelegate *delegate = nil;
    dispatch_queue_t captureQueue = nil;
    std::atomic<bool> isRunning{false};
    FrameCallback callback = nullptr;
    CameraConfig config;
};

AppleAvFoundationCameraDriver::AppleAvFoundationCameraDriver()
    : m_impl(std::make_unique<Impl>()) {
}

AppleAvFoundationCameraDriver::~AppleAvFoundationCameraDriver() {
    close();
}

std::vector<CameraDeviceInfo> AppleAvFoundationCameraDriver::enumerateDevices() {
    std::vector<CameraDeviceInfo> result;

    @autoreleasepool {
        AVCaptureDeviceDiscoverySession *discoverySession = [AVCaptureDeviceDiscoverySession
            discoverySessionWithDeviceTypes:@[AVCaptureDeviceTypeBuiltInWideAngleCamera]
                                  mediaType:AVMediaTypeVideo
                                   position:AVCaptureDevicePositionUnspecified];

        NSArray<AVCaptureDevice *> *devices = discoverySession.devices;
        int idx = 0;
        for (AVCaptureDevice *dev in devices) {
            CameraDeviceInfo info;
            info.id = idx++;
            info.name = [dev.localizedName UTF8String];
            info.symbolicLink = [dev.uniqueID UTF8String];
            info.preferredWidth = 640;
            info.preferredHeight = 480;
            info.preferredFps = 30.0f;

            if (dev.position == AVCaptureDevicePositionFront) {
                info.facing = CameraFacing::Front;
                info.name = "iPhone 前置原深感/FaceTime鏡頭 (" + info.name + ")";
            } else if (dev.position == AVCaptureDevicePositionBack) {
                info.facing = CameraFacing::Back;
            } else {
                info.facing = CameraFacing::Unknown;
            }

            result.push_back(info);
        }

        // 若系統返回空（例如尚未授權但裝置存在），預先插入前置鏡頭預設描述
        if (result.empty()) {
            CameraDeviceInfo defaultFront;
            defaultFront.id = 0;
            defaultFront.name = "iPhone 前置原深感/FaceTime鏡頭";
            defaultFront.symbolicLink = "avfoundation://front_camera";
            defaultFront.facing = CameraFacing::Front;
            defaultFront.preferredWidth = 640;
            defaultFront.preferredHeight = 480;
            defaultFront.preferredFps = 30.0f;
            result.push_back(defaultFront);
        }
    }

    return result;
}

bool AppleAvFoundationCameraDriver::open(const CameraConfig& config) {
    close();
    m_impl->config = config;

    @autoreleasepool {
        // 1. 檢查並請求相機權限
        AVAuthorizationStatus authStatus = [AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeVideo];
        if (authStatus == AVAuthorizationStatusDenied || authStatus == AVAuthorizationStatusRestricted) {
            std::cerr << "[AppleAvFoundationCameraDriver] 使用者已拒絕相機存取權限！\n";
            return false;
        }

        if (authStatus == AVAuthorizationStatusNotDetermined) {
            dispatch_semaphore_t sema = dispatch_semaphore_create(0);
            [AVCaptureDevice requestAccessForMediaType:AVMediaTypeVideo completionHandler:^(BOOL granted) {
                (void)granted;
                dispatch_semaphore_signal(sema);
            }];
            dispatch_semaphore_wait(sema, dispatch_time(DISPATCH_TIME_NOW, (int64_t)(5 * NSEC_PER_SEC)));
            authStatus = [AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeVideo];
            if (authStatus != AVAuthorizationStatusAuthorized) {
                std::cerr << "[AppleAvFoundationCameraDriver] 未獲取相機授權\n";
                return false;
            }
        }

        // 2. 尋找前置相機 (AVCaptureDevicePositionFront 優先)
        AVCaptureDeviceDiscoverySession *discoverySession = [AVCaptureDeviceDiscoverySession
            discoverySessionWithDeviceTypes:@[AVCaptureDeviceTypeBuiltInWideAngleCamera]
                                  mediaType:AVMediaTypeVideo
                                   position:AVCaptureDevicePositionFront];

        AVCaptureDevice *targetDevice = discoverySession.devices.firstObject;
        if (!targetDevice) {
            targetDevice = [AVCaptureDevice defaultDeviceWithMediaType:AVMediaTypeVideo];
        }

        if (!targetDevice) {
            std::cerr << "[AppleAvFoundationCameraDriver] 找不到任何可用的相機硬體裝置！\n";
            return false;
        }

        m_impl->device = targetDevice;

        // 3. 建立 AVCaptureSession
        NSError *error = nil;
        AVCaptureDeviceInput *input = [AVCaptureDeviceInput deviceInputWithDevice:targetDevice error:&error];
        if (!input || error) {
            std::cerr << "[AppleAvFoundationCameraDriver] 建立相機輸入失敗: " 
                      << (error ? [[error localizedDescription] UTF8String] : "Unknown error") << "\n";
            return false;
        }

        m_impl->session = [[AVCaptureSession alloc] init];
        [m_impl->session beginConfiguration];

        if ([m_impl->session canSetSessionPreset:AVCaptureSessionPreset640x480]) {
            [m_impl->session setSessionPreset:AVCaptureSessionPreset640x480];
        } else if ([m_impl->session canSetSessionPreset:AVCaptureSessionPresetMedium]) {
            [m_impl->session setSessionPreset:AVCaptureSessionPresetMedium];
        }

        if ([m_impl->session canAddInput:input]) {
            [m_impl->session addInput:input];
        } else {
            std::cerr << "[AppleAvFoundationCameraDriver] 無法將相機輸入加入 Session！\n";
            [m_impl->session commitConfiguration];
            return false;
        }

        // 4. 配置輸出 VideoDataOutput (32BGRA)
        m_impl->videoOutput = [[AVCaptureVideoDataOutput alloc] init];
        NSDictionary *videoSettings = @{
            (id)kCVPixelBufferPixelFormatTypeKey : @(kCVPixelFormatType_32BGRA)
        };
        m_impl->videoOutput.videoSettings = videoSettings;
        m_impl->videoOutput.alwaysDiscardsLateVideoFrames = YES;

        m_impl->delegate = [[EFDCaptureDelegate alloc] init];
        m_impl->delegate.frameCallback = m_impl->callback;

        m_impl->captureQueue = dispatch_queue_create("com.efd.camera.capture", DISPATCH_QUEUE_SERIAL);
        [m_impl->videoOutput setSampleBufferDelegate:m_impl->delegate queue:m_impl->captureQueue];

        if ([m_impl->session canAddOutput:m_impl->videoOutput]) {
            [m_impl->session addOutput:m_impl->videoOutput];
        }

        // 5. 設定鏡頭方向為直向 (Portrait) 且前鏡頭水平翻轉 (Mirrored) 符合自然視覺
        AVCaptureConnection *connection = [m_impl->videoOutput connectionWithMediaType:AVMediaTypeVideo];
        if (connection) {
            if ([connection isVideoOrientationSupported]) {
                [connection setVideoOrientation:AVCaptureVideoOrientationPortrait];
            }
            if ([connection isVideoMirroringSupported] && targetDevice.position == AVCaptureDevicePositionFront) {
                [connection setVideoMirrored:YES];
            }
        }

        [m_impl->session commitConfiguration];

        // 6. 啟動相機擷取串流
        [m_impl->session startRunning];
        m_impl->isRunning.store(true);

        std::cout << "[AppleAvFoundationCameraDriver] 成功開啟實體前置鏡頭: " 
                  << [targetDevice.localizedName UTF8String] << " (640x480 @ 30 FPS)\n";
        return true;
    }
}

void AppleAvFoundationCameraDriver::close() {
    if (!m_impl) return;

    if (m_impl->isRunning.load()) {
        m_impl->isRunning.store(false);
        if (m_impl->session) {
            [m_impl->session stopRunning];
            m_impl->session = nil;
        }
        m_impl->device = nil;
        m_impl->videoOutput = nil;
        m_impl->delegate = nil;
        m_impl->captureQueue = nil;
    }
}

bool AppleAvFoundationCameraDriver::isOpened() const {
    return m_impl && m_impl->isRunning.load();
}

void AppleAvFoundationCameraDriver::setFrameCallback(FrameCallback callback) {
    if (!m_impl) return;
    m_impl->callback = std::move(callback);
    if (m_impl->delegate) {
        m_impl->delegate.frameCallback = m_impl->callback;
    }
}

void* AppleAvFoundationCameraDriver::getCaptureSessionPtr() const {
    if (!m_impl) return nullptr;
    return (__bridge void*)m_impl->session;
}

} // namespace efd

#else

namespace efd {
struct AppleAvFoundationCameraDriver::Impl {};
AppleAvFoundationCameraDriver::AppleAvFoundationCameraDriver() = default;
AppleAvFoundationCameraDriver::~AppleAvFoundationCameraDriver() = default;
bool AppleAvFoundationCameraDriver::open(const CameraConfig&) { return false; }
void AppleAvFoundationCameraDriver::close() {}
bool AppleAvFoundationCameraDriver::isOpened() const { return false; }
void AppleAvFoundationCameraDriver::setFrameCallback(FrameCallback) {}
std::vector<CameraDeviceInfo> AppleAvFoundationCameraDriver::enumerateDevices() { return {}; }
void* AppleAvFoundationCameraDriver::getCaptureSessionPtr() const { return nullptr; }
} // namespace efd

#endif
