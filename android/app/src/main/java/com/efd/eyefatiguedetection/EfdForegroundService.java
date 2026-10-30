package com.efd.eyefatiguedetection;

import android.Manifest;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.content.pm.ServiceInfo;
import android.os.Binder;
import android.os.Build;
import android.os.Handler;
import android.os.IBinder;
import android.os.Looper;
import android.util.Log;

import androidx.annotation.Nullable;
import androidx.core.content.ContextCompat;
import androidx.lifecycle.LifecycleService;

import org.json.JSONObject;

/**
 * 常駐前景服務：擁有 C++ 偵測引擎與相機，App 介面被劃掉後仍持續偵測與提醒。
 * 引擎的生命週期只由本服務管理 (Activity 不得停止引擎)。
 */
public class EfdForegroundService extends LifecycleService {
    private static final String TAG = "EFD_ForegroundService";
    private static final long SYNC_CHECK_INTERVAL_MS = 30_000L;

    /** 常駐通知的「停止偵測」按鈕。 */
    public static final String ACTION_STOP_MONITORING = "com.efd.eyefatiguedetection.action.STOP_MONITORING";
    /** 使用者主動停止偵測：看門狗與開機提醒不再要求重新開始，直到使用者再次開啟 App。 */
    public static final String PREF_STOPPED_BY_USER = "monitoring_stopped_by_user";

    public static boolean isStoppedByUser(android.content.Context context) {
        return context.getSharedPreferences("efd_prefs", MODE_PRIVATE).getBoolean(PREF_STOPPED_BY_USER, false);
    }

    public static void setStoppedByUser(android.content.Context context, boolean stopped) {
        context.getSharedPreferences("efd_prefs", MODE_PRIVATE).edit().putBoolean(PREF_STOPPED_BY_USER, stopped).apply();
    }

    /** 前景 Activity 顯示中時接收疲勞警報 (改以對話框呈現，不重複發通知)。 */
    public interface AlertListener {
        void onFatigueAlert(String message);
    }

    private static volatile AlertListener sAlertListener;

    public static void setAlertListener(@Nullable AlertListener listener) {
        sAlertListener = listener;
    }

    private final IBinder mBinder = new LocalBinder();
    private EfdCameraXManager mCameraManager;
    private EfdNotificationHudManager mHudManager;
    private Handler mHandler;
    private Runnable mTelemetryRunnable;
    private long mLastSyncCheckMs = 0L;

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
        Log.i(TAG, "EfdForegroundService onCreate");

        mHudManager = new EfdNotificationHudManager(this);
        if (!enterForeground()) {
            // 沒有相機權限，或服務被系統關閉後在背景自動重啟 (Android 14+ 不允許從背景啟動相機類型前景服務)：
            // 無法繼續偵測，必須讓使用者知道，否則之後完全不會再有任何疲勞提醒
            mHudManager.showMonitoringStoppedReminder();
            stopSelf();
            return;
        }
        sInstance = this;
        mHudManager.cancelStartReminder();
        EfdWatchdogReceiver.schedule(this);

        ensureEngineRunning();

        mCameraManager = new EfdCameraXManager(this, this);
        mCameraManager.startCamera();

        scheduleNightlySyncAlarm();

        mHandler = new Handler(Looper.getMainLooper());
        startTelemetryLoop();
    }

    private boolean enterForeground() {
        if (ContextCompat.checkSelfPermission(this, Manifest.permission.CAMERA) != PackageManager.PERMISSION_GRANTED) {
            Log.w(TAG, "尚未取得相機權限，無法啟動前景偵測服務");
            return false;
        }
        try {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
                startForeground(EfdNotificationHudManager.SERVICE_NOTIFICATION_ID,
                        mHudManager.buildForegroundNotification(),
                        ServiceInfo.FOREGROUND_SERVICE_TYPE_CAMERA);
            } else {
                startForeground(EfdNotificationHudManager.SERVICE_NOTIFICATION_ID,
                        mHudManager.buildForegroundNotification());
            }
            return true;
        } catch (Throwable t) {
            // 例如 Android 14+ 從背景 (開機廣播) 啟動相機類型前景服務
            Log.e(TAG, "startForeground 失敗: " + t.getMessage(), t);
            return false;
        }
    }

    private void ensureEngineRunning() {
        try {
            if (!EfdNativeBridge.isEngineRunning()) {
                EfdNativeBridge.initEngineWithPath(getFilesDir().getAbsolutePath());
            }
        } catch (Throwable t) {
            Log.e(TAG, "Failed to init native engine with path: " + t.getMessage());
        }
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
        if (intent != null && ACTION_STOP_MONITORING.equals(intent.getAction())) {
            Log.i(TAG, "使用者從通知停止偵測");
            setStoppedByUser(this, true);
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.N) {
                stopForeground(STOP_FOREGROUND_REMOVE);
            } else {
                stopForeground(true);
            }
            stopSelf();
            return START_NOT_STICKY;
        }
        if (sInstance == this) {
            ensureEngineRunning();
        }
        return START_STICKY;
    }

    /**
     * 測試通知：延遲後由 C++ 引擎發出一個測試警報，走與真實疲勞提醒相同的路徑
     * (Activity 在前景時顯示對話框；App 被劃掉或在背景時發送通知)。
     */
    public void scheduleTestAlert(long delayMs) {
        if (mHandler == null) return;
        mHandler.postDelayed(EfdNativeBridge::triggerTestAlert, delayMs);
    }

    private void startTelemetryLoop() {
        mTelemetryRunnable = new Runnable() {
            @Override
            public void run() {
                try {
                    // 警報只在這裡取出一次：前景有 Activity 時改用對話框，否則發送通知
                    JSONObject alert = new JSONObject(EfdNativeBridge.consumeAlertJson());
                    if (alert.optInt("level", 0) == 2) {
                        String message = alert.optString("message", "你的眼睛處於疲勞狀態，請適當休息");
                        AlertListener listener = sAlertListener;
                        if (listener != null) {
                            listener.onFatigueAlert(message);
                        } else {
                            Log.w(TAG, "C++ 狀態機發射重度疲勞警報，發送通知提醒");
                            mHudManager.triggerSevereFatigueAlert("EFD 疲勞提醒", message);
                        }
                    }

                    long now = System.currentTimeMillis();
                    if (now - mLastSyncCheckMs >= SYNC_CHECK_INTERVAL_MS) {
                        mLastSyncCheckMs = now;
                        EfdSyncManager.checkAndPerformDailySync(EfdForegroundService.this, "EfdForegroundService");
                    }
                } catch (Throwable t) {
                    Log.w(TAG, "Telemetry loop error: " + t.getMessage());
                }
                mHandler.postDelayed(this, 1000);
            }
        };
        mHandler.post(mTelemetryRunnable);
    }

    @Override
    public void onTaskRemoved(Intent rootIntent) {
        super.onTaskRemoved(rootIntent);
        // 介面被劃掉：引擎由服務擁有，確認仍在運作並改回只做影像分析 (不綁定已消失的預覽畫面)
        Log.i(TAG, "使用者將 App 從任務選單中劃掉，背景前台服務持續常駐運作中");
        ensureEngineRunning();
        if (mCameraManager != null) {
            mCameraManager.startCamera();
        }
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
