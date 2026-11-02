package com.efd.eyefatiguedetection;

import android.content.Context;
import android.graphics.Bitmap;
import android.os.SystemClock;
import android.util.Log;

import androidx.annotation.NonNull;
import androidx.annotation.Nullable;

import com.google.mediapipe.framework.image.BitmapImageBuilder;
import com.google.mediapipe.framework.image.MPImage;
import com.google.mediapipe.tasks.components.containers.Category;
import com.google.mediapipe.tasks.components.containers.NormalizedLandmark;
import com.google.mediapipe.tasks.core.BaseOptions;
import com.google.mediapipe.tasks.core.Delegate;
import com.google.mediapipe.tasks.vision.core.RunningMode;
import com.google.mediapipe.tasks.vision.facelandmarker.FaceLandmarker;
import com.google.mediapipe.tasks.vision.facelandmarker.FaceLandmarkerResult;

import java.util.List;

/**
 * MediaPipe 臉部特徵點偵測 (478 點 + 眨眼 blendshape 分數)。
 * 影像只在記憶體中分析，不存檔、不上傳；特徵點交給 C++ 換算 EAR 後即丟棄。
 * 只在相機執行緒使用，不可跨執行緒共用。
 */
final class EfdFaceLandmarkerHelper {
    private static final String TAG = "EFD_FaceLandmarker";
    static final String MODEL_ASSET = "face_landmarker.task";

    static final class Result {
        /** 0~1 正規化特徵點 (x, y, z) 依序排列；null 代表沒有臉。 */
        @Nullable final float[] normalizedXyz;
        /** blendshape 眨眼分數 (0 睜 ~ 1 閉)；-1 代表未提供。 */
        final float blinkLeft;
        final float blinkRight;
        /** blendshape 眼球往下看 (兩眼平均)；低頭看手機時眼瞼下垂，用來與閉眼區分；-1 代表未提供。 */
        final float lookDown;
        final long inferenceMs;

        Result(@Nullable float[] normalizedXyz, float blinkLeft, float blinkRight, float lookDown, long inferenceMs) {
            this.normalizedXyz = normalizedXyz;
            this.blinkLeft = blinkLeft;
            this.blinkRight = blinkRight;
            this.lookDown = lookDown;
            this.inferenceMs = inferenceMs;
        }
    }

    private final FaceLandmarker mLandmarker;
    private final Delegate mDelegate;
    private long mLastTimestampMs = -1;

    private EfdFaceLandmarkerHelper(FaceLandmarker landmarker, Delegate delegate) {
        mLandmarker = landmarker;
        mDelegate = delegate;
    }

    /**
     * 載入模型：優先用 CPU，無法使用時改用 GPU；兩者都失敗 (模型缺少、原生程式庫無法載入) 時回傳 null，
     * 呼叫端改用啟發式偵測。三星 A71 實測 (10 FPS)：CPU 每幀 51~74 ms、本 App 佔一顆核心 83~100%；
     * GPU 每幀 64~66 ms、87~113%，沒有比較省，且載入慢 (1.8~4.7 秒 vs 0.5 秒)、記憶體多約 30 MB。
     */
    @Nullable
    static EfdFaceLandmarkerHelper create(@NonNull Context context) {
        for (Delegate delegate : new Delegate[] {Delegate.CPU, Delegate.GPU}) {
            try {
                long start = SystemClock.elapsedRealtime();
                FaceLandmarker landmarker = FaceLandmarker.createFromOptions(context.getApplicationContext(), buildOptions(delegate));
                Log.i(TAG, "MediaPipe FaceLandmarker 載入完成 (" + delegate + "，" + (SystemClock.elapsedRealtime() - start) + " ms)");
                return new EfdFaceLandmarkerHelper(landmarker, delegate);
            } catch (Throwable t) {
                Log.w(TAG, "MediaPipe FaceLandmarker 以 " + delegate + " 載入失敗: " + t.getMessage());
            }
        }
        Log.e(TAG, "MediaPipe FaceLandmarker 無法載入，改用啟發式偵測");
        return null;
    }

    private static FaceLandmarker.FaceLandmarkerOptions buildOptions(Delegate delegate) {
        return FaceLandmarker.FaceLandmarkerOptions.builder()
                .setBaseOptions(BaseOptions.builder()
                        .setModelAssetPath(MODEL_ASSET)
                        .setDelegate(delegate)
                        .build())
                .setRunningMode(RunningMode.VIDEO)
                .setNumFaces(1)
                .setMinFaceDetectionConfidence(0.5f)
                .setMinFacePresenceConfidence(0.5f)
                .setMinTrackingConfidence(0.5f)
                .setOutputFaceBlendshapes(true)
                .build();
    }

    Delegate delegate() {
        return mDelegate;
    }

    /** 分析一張已轉正的畫面。VIDEO 模式要求時間戳嚴格遞增，鎖定臉部後只追蹤不重新偵測。 */
    @NonNull
    Result detect(@NonNull Bitmap upright) {
        long timestampMs = SystemClock.uptimeMillis();
        if (timestampMs <= mLastTimestampMs) timestampMs = mLastTimestampMs + 1;
        mLastTimestampMs = timestampMs;

        long start = SystemClock.elapsedRealtime();
        MPImage image = new BitmapImageBuilder(upright).build();
        FaceLandmarkerResult result = mLandmarker.detectForVideo(image, timestampMs);
        long inferenceMs = SystemClock.elapsedRealtime() - start;

        List<List<NormalizedLandmark>> faces = result.faceLandmarks();
        if (faces == null || faces.isEmpty()) {
            return new Result(null, -1f, -1f, -1f, inferenceMs);
        }
        List<NormalizedLandmark> points = faces.get(0);
        float[] xyz = new float[points.size() * 3];
        for (int i = 0; i < points.size(); i++) {
            NormalizedLandmark p = points.get(i);
            xyz[i * 3] = p.x();
            xyz[i * 3 + 1] = p.y();
            xyz[i * 3 + 2] = p.z();
        }

        float blinkLeft = -1f;
        float blinkRight = -1f;
        float lookDownSum = 0f;
        int lookDownCount = 0;
        if (result.faceBlendshapes().isPresent() && !result.faceBlendshapes().get().isEmpty()) {
            for (Category c : result.faceBlendshapes().get().get(0)) {
                String name = c.categoryName();
                if ("eyeBlinkLeft".equals(name)) blinkLeft = c.score();
                else if ("eyeBlinkRight".equals(name)) blinkRight = c.score();
                else if ("eyeLookDownLeft".equals(name) || "eyeLookDownRight".equals(name)) {
                    lookDownSum += c.score();
                    lookDownCount++;
                }
            }
        }
        float lookDown = lookDownCount > 0 ? lookDownSum / lookDownCount : -1f;
        return new Result(xyz, blinkLeft, blinkRight, lookDown, inferenceMs);
    }

    void close() {
        try {
            mLandmarker.close();
        } catch (Throwable t) {
            Log.w(TAG, "關閉 FaceLandmarker 失敗: " + t.getMessage());
        }
    }
}
