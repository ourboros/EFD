package com.efd.eyefatiguedetection;

import android.content.Intent;
import android.os.Binder;
import android.os.Handler;
import android.os.IBinder;
import android.os.Looper;
import android.util.Log;

import androidx.annotation.Nullable;
import androidx.lifecycle.LifecycleService;

import org.json.JSONObject;

public class EfdForegroundService extends LifecycleService {
    private static final String TAG = "EFD_ForegroundService";

    private final IBinder mBinder = new LocalBinder();
    private EfdCameraXManager mCameraManager;
    private EfdNotificationHudManager mHudManager;
    private Handler mHandler;
    private Runnable mTelemetryRunnable;

    public class LocalBinder extends Binder {
        public EfdForegroundService getService() {
            return EfdForegroundService.this;
        }
    }

    @Override
    public void onCreate() {
        super.onCreate();
        Log.i(TAG, "EfdForegroundService onCreate");

        mHudManager = new EfdNotificationHudManager(this);
        startForeground(EfdNotificationHudManager.NOTIFICATION_ID, 
                mHudManager.buildForegroundNotification("系統即時防護中", 12.0f, 0));

        EfdNativeBridge.initEngine();

        mCameraManager = new EfdCameraXManager(this, this);
        mCameraManager.startCamera();

        mHandler = new Handler(Looper.getMainLooper());
        startTelemetryLoop();
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        super.onStartCommand(intent, flags, startId);
        return START_STICKY;
    }

    private void startTelemetryLoop() {
        mTelemetryRunnable = new Runnable() {
            @Override
            public void run() {
                try {
                    String jsonStr = EfdNativeBridge.getTelemetryJson();
                    if (jsonStr != null && jsonStr.length() > 0 && !jsonStr.startsWith("{\"error\"")) {
                        JSONObject obj = new JSONObject(jsonStr);
                        float score = (float) obj.optDouble("fatigueScore", 12.0);
                        int level = obj.optInt("fatigueLevel", 0);

                        String statusText = "生理狀態：正常專注";
                        if (level == 2) statusText = "生理狀態：【重度疲勞警報】";
                        else if (level == 1) statusText = "生理狀態：注意疲勞";

                        mHudManager.updateNotification(statusText, score, level);
                    }
                } catch (Throwable t) {
                    // ignore
                }
                mHandler.postDelayed(this, 1000);
            }
        };
        mHandler.post(mTelemetryRunnable);
    }

    @Override
    public void onDestroy() {
        super.onDestroy();
        Log.i(TAG, "EfdForegroundService onDestroy");
        if (mHandler != null && mTelemetryRunnable != null) {
            mHandler.removeCallbacks(mTelemetryRunnable);
        }
        if (mCameraManager != null) {
            mCameraManager.stopCamera();
        }
        EfdNativeBridge.stopEngine();
    }

    @Nullable
    @Override
    public IBinder onBind(Intent intent) {
        super.onBind(intent);
        return mBinder;
    }
}
