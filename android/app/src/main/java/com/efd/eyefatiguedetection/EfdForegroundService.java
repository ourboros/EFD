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
    private boolean mHasAlerted = false;
    private int mSyncDateInt = 0;

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
        startForeground(EfdNotificationHudManager.SERVICE_NOTIFICATION_ID, 
                mHudManager.buildForegroundNotification());

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
                        int level = obj.optInt("fatigueLevel", 0);

                        // 僅在「過度疲勞 (level == 2 / SevereWarning)」時觸發警告提醒通知，平時靜默不騷擾
                        if (level == 2) {
                            if (!mHasAlerted) {
                                mHasAlerted = true;
                                Log.w(TAG, "偵測到過度疲勞 (SevereWarning)，調出疲勞警告提醒！");
                                mHudManager.triggerSevereFatigueAlert(
                                        "🚨 EFD 重度疲勞警告提醒",
                                        "你的眼睛處於過度疲勞狀態，請立即閉眼休息 5 分鐘！"
                                );
                            }
                        } else {
                            mHasAlerted = false; // 恢復正常時重置，以便下次過度疲勞時能再次觸發提醒
                        }

                        // 每晚 21:00 (9 PM) 自動上傳當日眼動資料至 Google 試算表
                        java.util.Calendar cal = java.util.Calendar.getInstance();
                        int hour = cal.get(java.util.Calendar.HOUR_OF_DAY);
                        int todayDateInt = cal.get(java.util.Calendar.YEAR) * 10000 +
                                (cal.get(java.util.Calendar.MONTH) + 1) * 100 +
                                cal.get(java.util.Calendar.DAY_OF_MONTH);

                        if (hour >= 21 && mSyncDateInt != todayDateInt) {
                            mSyncDateInt = todayDateInt;
                            Log.i(TAG, "21:00 定時觸發：自動上傳當日眼動資料至 Google 試算表...");
                            EfdHttpSyncBridge.uploadEyelidDataAsync("SUBJ-ANDROID-001", 1, "Daily_2100_Evening_Sync", (success, responseMessage) -> {
                                Log.i(TAG, "21:00 自動上傳試算表結果: " + success + " | " + responseMessage);
                            });
                        }
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
    public void onTaskRemoved(Intent rootIntent) {
        super.onTaskRemoved(rootIntent);
        Log.w(TAG, "使用者將 App 從任務選單中滑掉關閉，啟動鬧鐘級 1 秒內自動重啟機制...");

        try {
            Intent restartServiceIntent = new Intent(getApplicationContext(), EfdForegroundService.class);
            android.app.PendingIntent restartPendingIntent = android.app.PendingIntent.getService(
                    getApplicationContext(), 1, restartServiceIntent,
                    android.app.PendingIntent.FLAG_ONE_SHOT | (android.os.Build.VERSION.SDK_INT >= android.os.Build.VERSION_CODES.M ? android.app.PendingIntent.FLAG_IMMUTABLE : 0));

            android.app.AlarmManager alarmService = (android.app.AlarmManager) getApplicationContext().getSystemService(android.content.Context.ALARM_SERVICE);
            if (alarmService != null) {
                alarmService.set(android.app.AlarmManager.RTC_WAKEUP, System.currentTimeMillis() + 1000, restartPendingIntent);
            }
        } catch (Throwable t) {
            Log.e(TAG, "自動重啟服務失敗: " + t.getMessage());
        }
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
