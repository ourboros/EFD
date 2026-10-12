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
                Log.i(TAG, "CameraX 前置鏡頭分析綁定成功 (30 FPS 串流開啟)");
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
                    ImageProxy.PlaneProxy[] planes = image.getPlanes();
                    if (planes != null && planes.length > 0) {
                        ByteBuffer yBuffer = planes[0].getBuffer();
                        byte[] yBytes = new byte[yBuffer.remaining()];
                        yBuffer.get(yBytes);

                        int width = image.getWidth();
                        int height = image.getHeight();
                        int rotation = image.getImageInfo().getRotationDegrees();

                        EfdNativeBridge.sendFrame(yBytes, width, height, rotation);
                    }
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
