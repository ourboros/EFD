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
    private boolean mIsSyncInProgress = false;

    private static EfdForegroundService sInstance = null;

    public static EfdForegroundService getInstance() {
        return sInstance;
    }

    public EfdCameraXManager getCameraManager() {
        return mCameraManager;
    }

    public class LocalBinder extends Binder {
        public EfdForegroundService getService() {
            return EfdForegroundService.this;
        }
    }

    @Override
    public void onCreate() {
        super.onCreate();
        sInstance = this;
        Log.i(TAG, "EfdForegroundService onCreate");

        mHudManager = new EfdNotificationHudManager(this);
        startForeground(EfdNotificationHudManager.SERVICE_NOTIFICATION_ID, 
                mHudManager.buildForegroundNotification());

        try {
            EfdNativeBridge.initEngineWithPath(getFilesDir().getAbsolutePath());
        } catch (Throwable t) {
            Log.e(TAG, "Failed to init native engine with path: " + t.getMessage());
        }

        mCameraManager = new EfdCameraXManager(this, this);
        mCameraManager.startCamera();

        scheduleNightlySyncAlarm();

        mHandler = new Handler(Looper.getMainLooper());
        startTelemetryLoop();
    }

    private void scheduleNightlySyncAlarm() {
        try {
            java.util.Calendar cal = java.util.Calendar.getInstance();
            cal.set(java.util.Calendar.HOUR_OF_DAY, 21);
            cal.set(java.util.Calendar.MINUTE, 0);
            cal.set(java.util.Calendar.SECOND, 0);
            if (cal.getTimeInMillis() < System.currentTimeMillis()) {
                cal.add(java.util.Calendar.DAY_OF_YEAR, 1);
            }

            Intent alarmIntent = new Intent(this, EfdSyncAlarmReceiver.class);
            android.app.PendingIntent pendingIntent = android.app.PendingIntent.getBroadcast(
                    this, 2100, alarmIntent,
                    android.app.PendingIntent.FLAG_UPDATE_CURRENT | (android.os.Build.VERSION.SDK_INT >= android.os.Build.VERSION_CODES.M ? android.app.PendingIntent.FLAG_IMMUTABLE : 0));

            android.app.AlarmManager alarmManager = (android.app.AlarmManager) getSystemService(android.content.Context.ALARM_SERVICE);
            if (alarmManager != null) {
                alarmManager.setInexactRepeating(
                        android.app.AlarmManager.RTC_WAKEUP,
                        cal.getTimeInMillis(),
                        android.app.AlarmManager.INTERVAL_DAY,
                        pendingIntent
                );
                Log.i(TAG, "每日 21:00 (9 PM) 背景自動同步鬧鐘註冊成功！預計下次執行時間: " + cal.getTime());
            }
        } catch (Throwable t) {
            Log.w(TAG, "註冊 21:00 鬧鐘失敗: " + t.getMessage());
        }
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
                        int lastAlertLevel = obj.optInt("lastAlertLevel", 0);

                        // 僅當 C++ 狀態機發射單次警報脈衝 (lastAlertLevel == 2) 時調出提醒，20 分鐘冷卻期內絕對靜默
                        if (lastAlertLevel == 2) {
                            Log.w(TAG, "C++ 狀態機發射重度疲勞警報脈衝，調出疲勞警告提醒！");
                            mHudManager.triggerSevereFatigueAlert(
                                    "🚨 EFD 重度疲勞警告提醒",
                                    "你的眼睛處於過度疲勞狀態，請立即閉眼休息 5 分鐘！"
                            );
                        }

                        // 每日 21:00 (9 PM) 檢查是否需要同步當日資料 (透過 EfdSyncManager 鎖定單次同步)
                        EfdSyncManager.checkAndPerformDailySync(EfdForegroundService.this, "EfdForegroundService");
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
        Log.i(TAG, "使用者將 App 從任務選單中劃掉，背景前台服務持續常駐運作中");
    }

    @Override
    public void onDestroy() {
        super.onDestroy();
        Log.i(TAG, "EfdForegroundService onDestroy");
        if (sInstance == this) {
            sInstance = null;
        }
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
