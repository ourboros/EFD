package com.efd.eyefatiguedetection;

import android.app.AlarmManager;
import android.app.PendingIntent;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.os.Build;
import android.util.Log;

import java.util.Calendar;

public class EfdSyncManager {
    private static final String TAG = "EFD_SyncManager";
    private static final String PREF_NAME = "efd_sync_prefs";
    private static final String KEY_LAST_SUCCESS_SYNC_DATE = "last_success_sync_date";

    private static boolean sIsSyncing = false;

    public static synchronized void checkAndPerformDailySync(Context context, String triggerSource) {
        if (context == null) return;
        Context appContext = context.getApplicationContext();

        SharedPreferences prefs = appContext.getSharedPreferences(PREF_NAME, Context.MODE_PRIVATE);
        int lastSuccessSyncDate = prefs.getInt(KEY_LAST_SUCCESS_SYNC_DATE, 0);

        Calendar cal = Calendar.getInstance();
        int hour = cal.get(Calendar.HOUR_OF_DAY);
        int todayDateInt = cal.get(Calendar.YEAR) * 10000 +
                (cal.get(Calendar.MONTH) + 1) * 100 +
                cal.get(Calendar.DAY_OF_MONTH);

        // 1. 若當天已成功同步過，徹底攔截並關閉重試 (0 次重複上傳)
        if (lastSuccessSyncDate == todayDateInt) {
            Log.i(TAG, "今日 (" + todayDateInt + ") 已成功上傳過 Google 試算表，自動關閉重試 (觸發源: " + triggerSource + ")");
            cancelRetryAlarm(appContext);
            return;
        }

        // 2. 只有在時間 >= 21:00 (9 PM) 且未進行中時才嘗試同步
        if (hour < 21 && !"Nightly_2100_AlarmManager_Sync".equals(triggerSource)) {
            return;
        }

        if (sIsSyncing) {
            Log.i(TAG, "每日同步正在進行中，忽略重複呼叫 (觸發源: " + triggerSource + ")");
            return;
        }

        sIsSyncing = true;
        Log.i(TAG, "🚀 開始執行 21:00 每日資料同步 (觸發源: " + triggerSource + ")...");

        EfdHttpSyncBridge.uploadFullPayloadAsync(null, (success, responseMessage) -> {
            synchronized (EfdSyncManager.class) {
                sIsSyncing = false;
                if (success) {
                    prefs.edit().putInt(KEY_LAST_SUCCESS_SYNC_DATE, todayDateInt).apply();
                    Log.i(TAG, "✅ 今日 (" + todayDateInt + ") 每日資料同步至 Google 試算表成功落盤註記！");
                    cancelRetryAlarm(appContext);
                } else {
                    Log.w(TAG, "❌ 每日資料同步上傳失敗，設定 10 分鐘後重試: " + responseMessage);
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
