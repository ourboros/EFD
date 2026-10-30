package com.efd.eyefatiguedetection;

import android.app.AlarmManager;
import android.app.PendingIntent;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.os.Build;
import android.os.SystemClock;
import android.util.Log;

import java.util.Calendar;

/**
 * 背景偵測看門狗：每 15 分鐘檢查前景服務是否仍在執行。
 *
 * 小米、OPPO、vivo、三星等廠牌在 App 被劃掉時常會一併關閉前景服務；
 * Android 12+ 不允許從背景重新啟動前景服務，因此改發「EFD 疲勞偵測已停止」通知，
 * 讓使用者點一下回到 App (Activity 在前景時會重新啟動服務)。
 * 只在 07:00~23:00 提醒，避免夜間打擾。
 */
public class EfdWatchdogReceiver extends BroadcastReceiver {
    private static final String TAG = "EFD_WatchdogReceiver";
    private static final int REQUEST_CODE = 1500;

    public static void schedule(Context context) {
        try {
            AlarmManager alarmManager = (AlarmManager) context.getSystemService(Context.ALARM_SERVICE);
            if (alarmManager == null) return;
            alarmManager.setInexactRepeating(
                    AlarmManager.ELAPSED_REALTIME,
                    SystemClock.elapsedRealtime() + AlarmManager.INTERVAL_FIFTEEN_MINUTES,
                    AlarmManager.INTERVAL_FIFTEEN_MINUTES,
                    pendingIntent(context));
        } catch (Throwable t) {
            Log.w(TAG, "註冊背景偵測看門狗失敗: " + t.getMessage());
        }
    }

    private static PendingIntent pendingIntent(Context context) {
        Intent intent = new Intent(context, EfdWatchdogReceiver.class);
        return PendingIntent.getBroadcast(context, REQUEST_CODE, intent,
                PendingIntent.FLAG_UPDATE_CURRENT | (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M ? PendingIntent.FLAG_IMMUTABLE : 0));
    }

    @Override
    public void onReceive(Context context, Intent intent) {
        if (EfdForegroundService.getInstance() != null) {
            return; // 服務仍在執行
        }
        if (EfdForegroundService.isStoppedByUser(context)) {
            return; // 使用者從通知主動停止偵測，不再提醒
        }

        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.S) {
            // Android 11 以下允許從背景啟動前景服務：直接恢復偵測
            try {
                Intent serviceIntent = new Intent(context, EfdForegroundService.class);
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
                    context.startForegroundService(serviceIntent);
                } else {
                    context.startService(serviceIntent);
                }
                Log.i(TAG, "背景偵測服務已停止，已自動重新啟動");
                return;
            } catch (Throwable t) {
                Log.w(TAG, "自動重新啟動背景偵測失敗: " + t.getMessage());
            }
        }

        int hour = Calendar.getInstance().get(Calendar.HOUR_OF_DAY);
        if (hour < 7 || hour >= 23) {
            return;
        }
        Log.w(TAG, "背景偵測服務已被系統關閉，提醒使用者重新開始");
        new EfdNotificationHudManager(context).showMonitoringStoppedReminder();
    }
}
