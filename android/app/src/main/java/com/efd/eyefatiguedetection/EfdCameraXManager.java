package com.efd.eyefatiguedetection;

import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.Matrix;
import android.os.SystemClock;
import android.util.Log;
import android.util.Size;

import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import androidx.camera.core.CameraSelector;
import androidx.camera.core.ImageAnalysis;
import androidx.camera.core.ImageProxy;
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

    // 分析頻率上限 15 FPS：每 67 ms 一張仍能抓到一般 100~400 ms 的眨眼，耗電約為 30 FPS 的一半
    private static final long MIN_FRAME_INTERVAL_MS = 66;
    private static final long INFERENCE_LOG_INTERVAL_MS = 30_000;

    private final Context mContext;
    private final LifecycleOwner mLifecycleOwner;
    private ExecutorService mCameraExecutor;
    private ProcessCameraProvider mCameraProvider;
    private boolean mIsRunning = false;

    // MediaPipe 偵測器 (只在相機執行緒存取)；載入失敗時為 null，改送 NV21 給 C++ 啟發式偵測
    private EfdFaceLandmarkerHelper mFaceLandmarker;
    private boolean mFaceLandmarkerTried = false;
    private long mLastAnalyzedMs = 0;
    private long mInferenceSumMs = 0;
    private int mInferenceCount = 0;
    private long mLastInferenceLogMs = 0;

    // 省電暫停 (螢幕關閉、冷卻或快篩非採樣時段)：暫停期間的開啟要求只記住預覽畫面，恢復時再綁定
    private boolean mPowerPaused = false;
    @Nullable private WeakReference<androidx.camera.view.PreviewView> mPreviewView;

    public EfdCameraXManager(@NonNull Context context, @NonNull LifecycleOwner lifecycleOwner) {
        this.mContext = context.getApplicationContext();
        this.mLifecycleOwner = lifecycleOwner;
        this.mCameraExecutor = Executors.newSingleThreadExecutor();
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

        ImageAnalysis imageAnalysis = new ImageAnalysis.Builder()
                .setTargetResolution(new Size(640, 480))
                .setBackpressureStrategy(ImageAnalysis.STRATEGY_KEEP_ONLY_LATEST)
                .setOutputImageFormat(ImageAnalysis.OUTPUT_IMAGE_FORMAT_YUV_420_888)
                .build();

        imageAnalysis.setAnalyzer(mCameraExecutor, new ImageAnalysis.Analyzer() {
            @Override
            public void analyze(@NonNull ImageProxy image) {
                try {
                    long now = SystemClock.elapsedRealtime();
                    if (now - mLastAnalyzedMs < MIN_FRAME_INTERVAL_MS) return;
                    mLastAnalyzedMs = now;

                    if (!mFaceLandmarkerTried) {
                        mFaceLandmarkerTried = true;
                        mFaceLandmarker = EfdFaceLandmarkerHelper.create(mContext);
                    }
                    if (mFaceLandmarker != null) {
                        analyzeWithMediaPipe(image);
                    } else {
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
                androidx.camera.core.Preview preview = new androidx.camera.core.Preview.Builder().build();
                preview.setSurfaceProvider(previewView.getSurfaceProvider());
                camera = mCameraProvider.bindToLifecycle(mLifecycleOwner, cameraSelector, preview, imageAnalysis);
            } else {
                camera = mCameraProvider.bindToLifecycle(mLifecycleOwner, cameraSelector, imageAnalysis);
            }
            meterOnFaceArea(camera);
        } catch (Throwable t) {
            Log.e(TAG, "Bind CameraX to lifecycle failed: " + t.getMessage(), t);
        }
    }

    /**
     * MediaPipe 路徑：轉正畫面 → 偵測 478 點 → 只把特徵點送給 C++ (影像不離開這個函式)。
     * 轉正角度 = CameraX 回報角度 + C++ 自動方向搜尋的額外角度 (部分手機在背景服務回報的角度不正確)。
     */
    private void analyzeWithMediaPipe(@NonNull ImageProxy image) {
        int reported = image.getImageInfo().getRotationDegrees();
        int rotation = (reported + EfdNativeBridge.getExtraRotation()) % 360;
        Bitmap frame = image.toBitmap();
        Bitmap upright = frame;
        if (rotation != 0) {
            Matrix matrix = new Matrix();
            matrix.postRotate(rotation);
            upright = Bitmap.createBitmap(frame, 0, 0, frame.getWidth(), frame.getHeight(), matrix, true);
            frame.recycle();
        }
        try {
            EfdFaceLandmarkerHelper.Result result = mFaceLandmarker.detect(upright);
            byte[] rgb = EfdNativeBridge.isDebugCaptureRequested() ? bitmapToRgb(upright) : null;
            EfdNativeBridge.sendLandmarks(result.normalizedXyz, upright.getWidth(), upright.getHeight(), reported,
                    result.blinkLeft, result.blinkRight, rgb);
            logInferenceTime(result.inferenceMs);
        } finally {
            upright.recycle();
        }
    }

    // 每 30 秒記錄平均推論時間，供耗電與發熱評估
    private void logInferenceTime(long inferenceMs) {
        mInferenceSumMs += inferenceMs;
        mInferenceCount++;
        long now = SystemClock.elapsedRealtime();
        if (mLastInferenceLogMs == 0) mLastInferenceLogMs = now;
        if (now - mLastInferenceLogMs >= INFERENCE_LOG_INTERVAL_MS) {
            Log.i(TAG, String.format(java.util.Locale.US, "MediaPipe 平均推論 %.1f ms (%d 幀 / %.0f 秒)",
                    (double) mInferenceSumMs / mInferenceCount, mInferenceCount, (now - mLastInferenceLogMs) / 1000.0));
            mInferenceSumMs = 0;
            mInferenceCount = 0;
            mLastInferenceLogMs = now;
        }
    }

    // 診斷擷取用：ARGB 像素轉成 C++ 使用的 RGB888
    private static byte[] bitmapToRgb(@NonNull Bitmap bitmap) {
        int w = bitmap.getWidth();
        int h = bitmap.getHeight();
        int[] argb = new int[w * h];
        bitmap.getPixels(argb, 0, w, 0, 0, w, h);
        byte[] rgb = new byte[w * h * 3];
        for (int i = 0; i < argb.length; i++) {
            int c = argb[i];
            rgb[i * 3] = (byte) ((c >> 16) & 0xFF);
            rgb[i * 3 + 1] = (byte) ((c >> 8) & 0xFF);
            rgb[i * 3 + 2] = (byte) (c & 0xFF);
        }
        return rgb;
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
