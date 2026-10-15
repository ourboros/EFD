package com.efd.eyefatiguedetection;

import android.content.Context;
import android.util.Log;
import android.util.Size;

import androidx.annotation.NonNull;
import androidx.camera.core.CameraSelector;
import androidx.camera.core.ImageAnalysis;
import androidx.camera.core.ImageProxy;
import androidx.camera.lifecycle.ProcessCameraProvider;
import androidx.core.content.ContextCompat;
import androidx.lifecycle.LifecycleOwner;

import com.google.common.util.concurrent.ListenableFuture;

import java.nio.ByteBuffer;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

public class EfdCameraXManager {
    private static final String TAG = "EFD_CameraXManager";

    private final Context mContext;
    private final LifecycleOwner mLifecycleOwner;
    private ExecutorService mCameraExecutor;
    private ProcessCameraProvider mCameraProvider;
    private boolean mIsRunning = false;

    public EfdCameraXManager(@NonNull Context context, @NonNull LifecycleOwner lifecycleOwner) {
        this.mContext = context.getApplicationContext();
        this.mLifecycleOwner = lifecycleOwner;
        this.mCameraExecutor = Executors.newSingleThreadExecutor();
    }

    public void startCamera() {
        if (mIsRunning) return;

        ListenableFuture<ProcessCameraProvider> future = ProcessCameraProvider.getInstance(mContext);
        future.addListener(() -> {
            try {
                mCameraProvider = future.get();
                bindCameraUseCases();
                mIsRunning = true;
                Log.i(TAG, "CameraX 前置鏡頭全彩 NV21 串流分析綁定成功 (30 FPS)");
            } catch (Throwable t) {
                Log.e(TAG, "開啟 CameraX 失敗: " + t.getMessage(), t);
            }
        }, ContextCompat.getMainExecutor(mContext));
    }

    private void bindCameraUseCases() {
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
                    byte[] nv21 = imageToNv21(image);
                    int width = image.getWidth();
                    int height = image.getHeight();
                    int rotation = image.getImageInfo().getRotationDegrees();

                    EfdNativeBridge.sendFrame(nv21, width, height, rotation);
                } catch (Throwable t) {
                    Log.w(TAG, "Frame processing error: " + t.getMessage());
                } finally {
                    image.close();
                }
            }
        });

        try {
            mCameraProvider.bindToLifecycle(mLifecycleOwner, cameraSelector, imageAnalysis);
        } catch (Throwable t) {
            Log.e(TAG, "Bind CameraX to lifecycle failed: " + t.getMessage(), t);
        }
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
        if (mCameraProvider != null) {
            try {
                mCameraProvider.unbindAll();
            } catch (Throwable t) {
                // ignore
            }
        }
        if (mCameraExecutor != null && !mCameraExecutor.isShutdown()) {
            mCameraExecutor.shutdown();
        }
    }

    public boolean isRunning() {
        return mIsRunning;
    }
}
