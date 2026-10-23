package com.efd.eyefatiguedetection;

import android.app.AlarmManager;
import android.app.PendingIntent;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.os.Build;
import android.util.Log;

import java.util.Calendar;

/**
 * Android 唯一的每日上傳入口 (C++ 引擎端的夜間同步在 Android 上已關閉)。
 * - 每晚 21:00 後上傳當日資料
 * - 補傳：若昨天 (含) 以前未成功上傳，任何時間觸發都會立即補傳
 * - 上傳成功才標記；失敗 10 分鐘後重試
 */
public class EfdSyncManager {
    private static final String TAG = "EFD_SyncManager";
    private static final String PREF_NAME = "efd_sync_prefs";
    private static final String KEY_LAST_SUCCESS_SYNC_DATE = "last_success_sync_date";
    private static final String KEY_FIRST_SEEN_DATE = "first_seen_date";

    private static boolean sIsSyncing = false;

    private static int toDateInt(Calendar cal) {
        return cal.get(Calendar.YEAR) * 10000 +
                (cal.get(Calendar.MONTH) + 1) * 100 +
                cal.get(Calendar.DAY_OF_MONTH);
    }

    public static synchronized void checkAndPerformDailySync(Context context, String triggerSource) {
        if (context == null) return;
        Context appContext = context.getApplicationContext();

        SharedPreferences prefs = appContext.getSharedPreferences(PREF_NAME, Context.MODE_PRIVATE);
        int lastSuccessSyncDate = prefs.getInt(KEY_LAST_SUCCESS_SYNC_DATE, 0);

        Calendar cal = Calendar.getInstance();
        int hour = cal.get(Calendar.HOUR_OF_DAY);
        final int todayDateInt = toDateInt(cal);
        Calendar yesterday = (Calendar) cal.clone();
        yesterday.add(Calendar.DAY_OF_YEAR, -1);
        final int yesterdayDateInt = toDateInt(yesterday);

        // 記錄第一次執行的日期：安裝當天之前不可能有資料，避免首次啟動就誤判為「需要補傳」
        int firstSeenDate = prefs.getInt(KEY_FIRST_SEEN_DATE, 0);
        if (firstSeenDate == 0) {
            firstSeenDate = todayDateInt;
            prefs.edit().putInt(KEY_FIRST_SEEN_DATE, firstSeenDate).apply();
        }

        if (lastSuccessSyncDate >= todayDateInt) {
            cancelRetryAlarm(appContext);
            return;
        }

        final int markDate;
        boolean isNightlyWindow = hour >= 21 || "Nightly_2100_AlarmManager_Sync".equals(triggerSource);
        if (isNightlyWindow) {
            markDate = todayDateInt;
        } else if (lastSuccessSyncDate < yesterdayDateInt && firstSeenDate < todayDateInt) {
            // 補傳：例如昨晚 21:00 手機關機或 App 被系統終止
            markDate = yesterdayDateInt;
        } else {
            return;
        }

        if (sIsSyncing) {
            Log.i(TAG, "每日同步正在進行中，忽略重複呼叫 (觸發源: " + triggerSource + ")");
            return;
        }

        // 上傳內容由 C++ 資料庫產生；服務未啟動時 (例如開機、鬧鐘喚醒) 先載入引擎
        if (!EfdNativeBridge.isEngineRunning()) {
            EfdNativeBridge.initEngineWithPath(appContext.getFilesDir().getAbsolutePath());
        }

        sIsSyncing = true;
        Log.i(TAG, "開始執行每日資料同步 (標記日期 " + markDate + "，觸發源: " + triggerSource + ")...");

        EfdHttpSyncBridge.uploadFullPayloadAsync(null, (success, responseMessage) -> {
            synchronized (EfdSyncManager.class) {
                sIsSyncing = false;
                if (success) {
                    prefs.edit().putInt(KEY_LAST_SUCCESS_SYNC_DATE, markDate).apply();
                    Log.i(TAG, "資料同步至 Google 試算表成功，標記日期 " + markDate);
                    cancelRetryAlarm(appContext);
                } else {
                    Log.w(TAG, "資料同步上傳失敗，設定 10 分鐘後重試: " + responseMessage);
                    scheduleRetryAlarm(appContext);
                }
            }
        });
    }

    private static void scheduleRetryAlarm(Context context) {
        try {
            AlarmManager alarmManager = (AlarmManager) context.getSystemService(Context.ALARM_SERVICE);
            if (alarmManager == null) return;

            Intent intent = new Intent(context, EfdSyncAlarmReceiver.class);
            intent.putExtra(EfdSyncAlarmReceiver.EXTRA_IS_RETRY, true);
            PendingIntent pendingIntent = PendingIntent.getBroadcast(
                    context, 2101, intent,
                    PendingIntent.FLAG_UPDATE_CURRENT | (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M ? PendingIntent.FLAG_IMMUTABLE : 0));

            long triggerTime = System.currentTimeMillis() + 10 * 60 * 1000; // 10 分鐘後
            alarmManager.set(AlarmManager.RTC_WAKEUP, triggerTime, pendingIntent);
            Log.i(TAG, "已設定 10 分鐘後重試上傳鬧鐘");
        } catch (Throwable t) {
            Log.w(TAG, "設定重試鬧鐘失敗: " + t.getMessage());
        }
    }

    private static void cancelRetryAlarm(Context context) {
        try {
            AlarmManager alarmManager = (AlarmManager) context.getSystemService(Context.ALARM_SERVICE);
            if (alarmManager == null) return;

            Intent intent = new Intent(context, EfdSyncAlarmReceiver.class);
            PendingIntent pendingIntent = PendingIntent.getBroadcast(
                    context, 2101, intent,
                    PendingIntent.FLAG_NO_CREATE | (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M ? PendingIntent.FLAG_IMMUTABLE : 0));

            if (pendingIntent != null) {
                alarmManager.cancel(pendingIntent);
                pendingIntent.cancel();
                Log.i(TAG, "已成功取消重試上傳鬧鐘");
            }
        } catch (Throwable t) {
            Log.w(TAG, "取消重試鬧鐘失敗: " + t.getMessage());
        }
    }
}
