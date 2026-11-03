package com.efd.eyefatiguedetection;

import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.ImageFormat;
import android.graphics.PixelFormat;
import android.hardware.camera2.CameraCharacteristics;
import android.hardware.camera2.CaptureRequest;
import android.os.SystemClock;
import android.util.Log;
import android.util.Range;
import android.util.Size;

import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import androidx.annotation.OptIn;
import androidx.camera.camera2.interop.Camera2CameraInfo;
import androidx.camera.camera2.interop.Camera2Interop;
import androidx.camera.camera2.interop.ExperimentalCamera2Interop;
import androidx.camera.core.CameraInfo;
import androidx.camera.core.CameraSelector;
import androidx.camera.core.ImageAnalysis;
import androidx.camera.core.ImageProxy;
import androidx.camera.core.resolutionselector.AspectRatioStrategy;
import androidx.camera.core.resolutionselector.ResolutionSelector;
import androidx.camera.core.resolutionselector.ResolutionStrategy;
import androidx.camera.lifecycle.ProcessCameraProvider;
import androidx.core.content.ContextCompat;
import androidx.lifecycle.LifecycleOwner;

import com.google.common.util.concurrent.ListenableFuture;

import java.lang.ref.WeakReference;
import java.nio.ByteBuffer;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

public class EfdCameraXManager {
    private static final String TAG = "EFD_CameraXManager";

    // 分析頻率上限 10 FPS：每 100 ms 一張仍能抓到一般 100~400 ms 的眨眼。
    // 三星 A71 實測 MediaPipe 每幀約 55 ms (GPU)，15 FPS 時本 App 佔一顆核心約 94%，降頻以控制發熱與耗電
    private static final long MIN_FRAME_INTERVAL_MS = 100;
    // 連續 10 秒沒偵測到臉 (手機平放、拿去看別處) 改為每 500 ms 一張：A71 實測無臉時仍以 10 FPS
    // 跑臉部偵測，佔一顆核心約 98%；臉一出現就恢復 10 FPS (最多延遲 0.5 秒)
    private static final long NO_FACE_SLOWDOWN_AFTER_MS = 10_000;
    private static final long NO_FACE_FRAME_INTERVAL_MS = 500;
    private static final long INFERENCE_LOG_INTERVAL_MS = 30_000;

    private final Context mContext;
    private final LifecycleOwner mLifecycleOwner;
    private ExecutorService mCameraExecutor;
    private ProcessCameraProvider mCameraProvider;
    private boolean mIsRunning = false;

    // MediaPipe 偵測器 (在相機執行緒載入與使用)；載入失敗時為 null，改送 NV21 給 C++ 啟發式偵測
    private EfdFaceLandmarkerHelper mFaceLandmarker;
    private volatile boolean mMediaPipeUnavailable = false;
    private long mLastAnalyzedMs = 0;
    private volatile long mLastFaceMs = 0;
    private long mInferenceSumMs = 0;
    private long mConvertSumMs = 0;
    private int mInferenceCount = 0;
    private int mFaceFrameCount = 0;
    private long mLastInferenceLogMs = 0;

    // 省電暫停 (螢幕關閉、冷卻或快篩非採樣時段)：暫停期間的開啟要求只記住預覽畫面，恢復時再綁定
    private boolean mPowerPaused = false;
    @Nullable private WeakReference<androidx.camera.view.PreviewView> mPreviewView;

    public EfdCameraXManager(@NonNull Context context, @NonNull LifecycleOwner lifecycleOwner) {
        this.mContext = context.getApplicationContext();
        this.mLifecycleOwner = lifecycleOwner;
        this.mCameraExecutor = Executors.newSingleThreadExecutor();
        // 先在相機執行緒載入模型 (GPU 約 2 秒)，期間送來的畫面排在後面等候
        mCameraExecutor.execute(() -> {
            mFaceLandmarker = EfdFaceLandmarkerHelper.create(mContext);
            if (mFaceLandmarker == null) {
                mMediaPipeUnavailable = true;
                // 已用 RGBA 綁定的相機改回 YUV，交給啟發式偵測
                ContextCompat.getMainExecutor(mContext).execute(() -> {
                    if (mIsRunning && !mPowerPaused) {
                        androidx.camera.view.PreviewView preview = (mPreviewView != null) ? mPreviewView.get() : null;
                        bindWithProvider(preview);
                    }
                });
            }
        });
    }

    public void startCamera() {
        startCameraWithPreview(null);
    }

    public void startCameraWithPreview(@Nullable androidx.camera.view.PreviewView previewView) {
        mPreviewView = (previewView != null) ? new WeakReference<>(previewView) : null;
        if (mPowerPaused) {
            Log.i(TAG, "相機省電暫停中，恢復時再開啟");
            return;
        }
        bindWithProvider(previewView);
    }

    private void bindWithProvider(@Nullable androidx.camera.view.PreviewView previewView) {
        ListenableFuture<ProcessCameraProvider> future = ProcessCameraProvider.getInstance(mContext);
        future.addListener(() -> {
            try {
                mCameraProvider = future.get();
                if (mPowerPaused) return;
                bindCombinedUseCases(previewView);
                mIsRunning = true;
                Log.i(TAG, "CameraX 前置鏡頭影像分析" + (previewView != null ? "與預覽" : "") + "綁定成功");
            } catch (Throwable t) {
                Log.e(TAG, "開啟 CameraX 失敗: " + t.getMessage(), t);
            }
        }, ContextCompat.getMainExecutor(mContext));
    }

    /** 省電暫停：解除相機綁定 (保留分析執行緒與 MediaPipe 模型，恢復時不必重新載入)。 */
    public void pauseCamera() {
        if (mPowerPaused) return;
        mPowerPaused = true;
        mIsRunning = false;
        try {
            if (mCameraProvider != null) {
                mCameraProvider.unbindAll();
            }
            Log.i(TAG, "相機省電暫停");
        } catch (Throwable t) {
            Log.w(TAG, "暫停相機失敗: " + t.getMessage());
        }
    }

    /** 從省電暫停恢復：沿用暫停前 (或暫停期間要求) 的預覽畫面。 */
    public void resumeCamera() {
        if (!mPowerPaused) return;
        mPowerPaused = false;
        androidx.camera.view.PreviewView preview = (mPreviewView != null) ? mPreviewView.get() : null;
        if (preview != null && !preview.isAttachedToWindow()) {
            preview = null;
        }
        Log.i(TAG, "相機從省電暫停恢復");
        bindWithProvider(preview);
    }

    public boolean isPowerPaused() {
        return mPowerPaused;
    }

    private void bindCombinedUseCases(@Nullable androidx.camera.view.PreviewView previewView) {
        if (mCameraProvider == null) return;

        mCameraProvider.unbindAll();

        CameraSelector cameraSelector = new CameraSelector.Builder()
                .requireLensFacing(CameraSelector.LENS_FACING_FRONT)
                .build();

        // MediaPipe：CameraX 直接輸出 RGBA (在原生層轉換)，toBitmap 只是複製像素；
        // 原本 YUV -> Bitmap -> 旋轉在三星 A71 每幀約 35 ms。MediaPipe 載入失敗時改回 YUV 給啟發式偵測
        final boolean useMediaPipe = !mMediaPipeUnavailable;
        // 640x480 (感光元件方向，4:3)：舊寫法 setTargetResolution 以直式座標解讀，
        // A71 因此選到 1088x1088，每幀像素多 3.9 倍；臉部只佔畫面一部分，MediaPipe 裁切後縮放到 256x256，640x480 已足夠
        ResolutionSelector resolutionSelector = new ResolutionSelector.Builder()
                .setAspectRatioStrategy(AspectRatioStrategy.RATIO_4_3_FALLBACK_AUTO_STRATEGY)
                .setResolutionStrategy(new ResolutionStrategy(new Size(640, 480),
                        ResolutionStrategy.FALLBACK_RULE_CLOSEST_HIGHER_THEN_LOWER))
                .build();
        ImageAnalysis.Builder analysisBuilder = new ImageAnalysis.Builder()
                .setResolutionSelector(resolutionSelector)
                .setBackpressureStrategy(ImageAnalysis.STRATEGY_KEEP_ONLY_LATEST)
                .setOutputImageFormat(useMediaPipe
                        ? ImageAnalysis.OUTPUT_IMAGE_FORMAT_RGBA_8888
                        : ImageAnalysis.OUTPUT_IMAGE_FORMAT_YUV_420_888);
        final Range<Integer> fpsRange = pickLowPowerFpsRange();
        if (fpsRange != null) applyFpsRange(analysisBuilder, fpsRange);
        ImageAnalysis imageAnalysis = analysisBuilder.build();

        mLastFaceMs = SystemClock.elapsedRealtime(); // 剛開相機 (含校準) 先用 10 FPS
        imageAnalysis.setAnalyzer(mCameraExecutor, new ImageAnalysis.Analyzer() {
            @Override
            public void analyze(@NonNull ImageProxy image) {
                try {
                    long now = SystemClock.elapsedRealtime();
                    long interval = (now - mLastFaceMs > NO_FACE_SLOWDOWN_AFTER_MS)
                            ? NO_FACE_FRAME_INTERVAL_MS : MIN_FRAME_INTERVAL_MS;
                    if (now - mLastAnalyzedMs < interval) return;
                    mLastAnalyzedMs = now;

                    if (mFaceLandmarker != null && image.getFormat() == PixelFormat.RGBA_8888) {
                        analyzeWithMediaPipe(image);
                    } else if (mMediaPipeUnavailable && image.getFormat() == ImageFormat.YUV_420_888) {
                        byte[] nv21 = imageToNv21(image);
                        EfdNativeBridge.sendFrame(nv21, image.getWidth(), image.getHeight(),
                                image.getImageInfo().getRotationDegrees());
                    }
                } catch (Throwable t) {
                    Log.w(TAG, "Frame processing error: " + t.getMessage());
                } finally {
                    image.close();
                }
            }
        });

        try {
            androidx.camera.core.Camera camera;
            if (previewView != null) {
                androidx.camera.core.Preview.Builder previewBuilder = new androidx.camera.core.Preview.Builder();
                if (fpsRange != null) applyFpsRange(previewBuilder, fpsRange);
                androidx.camera.core.Preview preview = previewBuilder.build();
                preview.setSurfaceProvider(previewView.getSurfaceProvider());
                camera = mCameraProvider.bindToLifecycle(mLifecycleOwner, cameraSelector, preview, imageAnalysis);
            } else {
                camera = mCameraProvider.bindToLifecycle(mLifecycleOwner, cameraSelector, imageAnalysis);
            }
            meterOnFaceArea(camera);
            androidx.camera.core.ResolutionInfo resolution = imageAnalysis.getResolutionInfo();
            Log.i(TAG, "影像分析解析度: " + (resolution != null ? resolution.getResolution() : "未知"));
        } catch (Throwable t) {
            Log.e(TAG, "Bind CameraX to lifecycle failed: " + t.getMessage(), t);
        }
    }

    /**
     * 相機輸出幀率：預設 30 FPS，但只分析 10 FPS，CameraX 仍會把每一張轉成 RGBA。
     * 從手機支援的範圍中挑「最低幀率 >= 10 (暗處也不低於分析頻率)」且上限最小的一組，
     * A71 前鏡頭為 [15,15]；找不到時維持手機預設。
     */
    @OptIn(markerClass = ExperimentalCamera2Interop.class)
    @Nullable
    private Range<Integer> pickLowPowerFpsRange() {
        try {
            Range<Integer> best = null;
            for (CameraInfo info : mCameraProvider.getAvailableCameraInfos()) {
                Camera2CameraInfo c2 = Camera2CameraInfo.from(info);
                Integer facing = c2.getCameraCharacteristic(CameraCharacteristics.LENS_FACING);
                if (facing == null || facing != CameraCharacteristics.LENS_FACING_FRONT) continue;
                Range<Integer>[] ranges = c2.getCameraCharacteristic(CameraCharacteristics.CONTROL_AE_AVAILABLE_TARGET_FPS_RANGES);
                if (ranges == null) continue;
                for (Range<Integer> r : ranges) {
                    if (r.getLower() < 10) continue;
                    if (best == null || r.getUpper() < best.getUpper()
                            || (r.getUpper().equals(best.getUpper()) && r.getLower() > best.getLower())) {
                        best = r;
                    }
                }
                break;
            }
            Log.i(TAG, "前鏡頭輸出幀率範圍: " + (best != null ? best : "手機預設"));
            return best;
        } catch (Throwable t) {
            Log.w(TAG, "讀取前鏡頭幀率範圍失敗，維持預設: " + t.getMessage());
            return null;
        }
    }

    @OptIn(markerClass = ExperimentalCamera2Interop.class)
    private static <T> void applyFpsRange(androidx.camera.core.ExtendableBuilder<T> builder, Range<Integer> range) {
        new Camera2Interop.Extender<>(builder).setCaptureRequestOption(CaptureRequest.CONTROL_AE_TARGET_FPS_RANGE, range);
    }

    /**
     * MediaPipe 路徑：偵測 478 點 → 只把特徵點送給 C++ (影像不離開這個函式)。
     * 不轉正畫面：MediaPipe 依兩眼連線把臉部裁切轉正，A71 實測 0° / 90° / 270° 都偵測得到，
     * EAR 只取決於點與點的距離，與畫面方向無關；省下旋轉的時間。
     */
    private void analyzeWithMediaPipe(@NonNull ImageProxy image) {
        int reported = image.getImageInfo().getRotationDegrees();
        long convertStart = SystemClock.elapsedRealtime();
        Bitmap frame = image.toBitmap();
        long convertMs = SystemClock.elapsedRealtime() - convertStart;
        try {
            EfdFaceLandmarkerHelper.Result result = mFaceLandmarker.detect(frame);
            EfdNativeBridge.sendLandmarks(result.normalizedXyz, frame.getWidth(), frame.getHeight(), reported,
                    result.blinkLeft, result.blinkRight, result.lookDown);
            if (result.normalizedXyz != null) mLastFaceMs = SystemClock.elapsedRealtime();
            logInferenceTime(result.inferenceMs, convertMs, result.normalizedXyz != null);
        } finally {
            frame.recycle();
        }
    }

    // 每 30 秒記錄平均推論與影像轉換時間、有臉的幀比例，供耗電與發熱評估
    private void logInferenceTime(long inferenceMs, long convertMs, boolean hasFace) {
        mInferenceSumMs += inferenceMs;
        mConvertSumMs += convertMs;
        mInferenceCount++;
        if (hasFace) mFaceFrameCount++;
        long now = SystemClock.elapsedRealtime();
        if (mLastInferenceLogMs == 0) mLastInferenceLogMs = now;
        if (now - mLastInferenceLogMs >= INFERENCE_LOG_INTERVAL_MS) {
            Log.i(TAG, String.format(java.util.Locale.US,
                    "MediaPipe(%s) 平均推論 %.1f ms、影像轉換 %.1f ms，有臉 %d/%d 幀 (%.0f 秒)",
                    mFaceLandmarker.delegate(), (double) mInferenceSumMs / mInferenceCount,
                    (double) mConvertSumMs / mInferenceCount, mFaceFrameCount, mInferenceCount,
                    (now - mLastInferenceLogMs) / 1000.0));
            mInferenceSumMs = 0;
            mConvertSumMs = 0;
            mInferenceCount = 0;
            mFaceFrameCount = 0;
            mLastInferenceLogMs = now;
        }
    }

    /**
     * 以畫面中央 (臉部通常所在) 為曝光測光區域，取代整張畫面平均測光：
     * 手機由下往上拍時天花板燈常在畫面中，整體測光會把臉壓暗到失去膚色 (三星 A71 實測臉部亮度僅 55~77)。
     */
    private void meterOnFaceArea(androidx.camera.core.Camera camera) {
        if (camera == null) return;
        try {
            androidx.camera.core.MeteringPointFactory factory =
                    new androidx.camera.core.SurfaceOrientedMeteringPointFactory(1f, 1f);
            androidx.camera.core.MeteringPoint center = factory.createPoint(0.5f, 0.5f, 0.45f);
            androidx.camera.core.FocusMeteringAction action = new androidx.camera.core.FocusMeteringAction.Builder(
                    center, androidx.camera.core.FocusMeteringAction.FLAG_AE)
                    .disableAutoCancel()
                    .build();
            camera.getCameraControl().startFocusAndMetering(action);
        } catch (Throwable t) {
            Log.w(TAG, "設定中央測光失敗 (沿用系統自動曝光): " + t.getMessage());
        }
    }

    public void bindPreviewView(@NonNull androidx.camera.view.PreviewView previewView) {
        startCameraWithPreview(previewView);
    }

    private static byte[] imageToNv21(ImageProxy image) {
        int width = image.getWidth();
        int height = image.getHeight();
        byte[] nv21 = new byte[width * height * 3 / 2];

        ImageProxy.PlaneProxy[] planes = image.getPlanes();
        ByteBuffer yBuffer = planes[0].getBuffer();
        ByteBuffer uBuffer = planes[1].getBuffer();
        ByteBuffer vBuffer = planes[2].getBuffer();

        int rowStride = planes[0].getRowStride();

        if (rowStride == width) {
            yBuffer.get(nv21, 0, width * height);
        } else {
            int pos = 0;
            for (int row = 0; row < height; row++) {
                yBuffer.get(nv21, pos, width);
                pos += width;
                if (row < height - 1) {
                    yBuffer.position(yBuffer.position() + rowStride - width);
                }
            }
        }

        int vRowStride = planes[2].getRowStride();
        int vPixelStride = planes[2].getPixelStride();
        int uRowStride = planes[1].getRowStride();
        int uPixelStride = planes[1].getPixelStride();

        int uvPos = width * height;
        int uvHeight = height / 2;
        int uvWidth = width / 2;

        byte[] vBytes = new byte[vBuffer.remaining()];
        byte[] uBytes = new byte[uBuffer.remaining()];
        vBuffer.get(vBytes);
        uBuffer.get(uBytes);

        for (int row = 0; row < uvHeight; row++) {
            int vLineOffset = row * vRowStride;
            int uLineOffset = row * uRowStride;
            for (int col = 0; col < uvWidth; col++) {
                int vIdx = vLineOffset + col * vPixelStride;
                int uIdx = uLineOffset + col * uPixelStride;
                if (vIdx < vBytes.length && uIdx < uBytes.length) {
                    nv21[uvPos++] = vBytes[vIdx];
                    nv21[uvPos++] = uBytes[uIdx];
                }
            }
        }

        return nv21;
    }

    public void stopCamera() {
        mIsRunning = false;
        try {
            if (mCameraProvider != null) {
                mCameraProvider.unbindAll();
            }
        } catch (Throwable t) {
            Log.w(TAG, "Error unbinding CameraX: " + t.getMessage());
        }
        try {
            if (mCameraExecutor != null && !mCameraExecutor.isShutdown()) {
                // 模型只在相機執行緒使用，排在最後關閉，等進行中的分析完成
                mCameraExecutor.execute(() -> {
                    if (mFaceLandmarker != null) {
                        mFaceLandmarker.close();
                        mFaceLandmarker = null;
                    }
                });
                mCameraExecutor.shutdown();
            }
        } catch (Throwable t) {
            Log.w(TAG, "Error shutting down executor: " + t.getMessage());
        }
    }

    public boolean isRunning() {
        return mIsRunning;
    }
}
