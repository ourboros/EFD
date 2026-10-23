package com.efd.eyefatiguedetection;

import android.Manifest;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.os.Build;
import android.util.Log;

import androidx.core.content.ContextCompat;

public class EfdBootReceiver extends BroadcastReceiver {
    private static final String TAG = "EFD_BootReceiver";

    @Override
    public void onReceive(Context context, Intent intent) {
        if (intent == null || intent.getAction() == null) return;

        String action = intent.getAction();
        if (Intent.ACTION_BOOT_COMPLETED.equals(action) ||
            "android.intent.action.QUICKBOOT_POWERON".equals(action) ||
            Intent.ACTION_MY_PACKAGE_REPLACED.equals(action)) {

            Log.i(TAG, "手機開機/App 更新，恢復 EFD 背景監控與資料同步...");
            boolean hasCamera = ContextCompat.checkSelfPermission(context, Manifest.permission.CAMERA)
                    == PackageManager.PERMISSION_GRANTED;

            // Android 14 起禁止從開機廣播啟動「相機類型」前景服務，改發通知請使用者點一下開始偵測
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.UPSIDE_DOWN_CAKE || !hasCamera) {
                new EfdNotificationHudManager(context).showStartMonitoringReminder();
            } else {
                try {
                    Intent serviceIntent = new Intent(context, EfdForegroundService.class);
                    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
                        context.startForegroundService(serviceIntent);
                    } else {
                        context.startService(serviceIntent);
                    }
                } catch (Throwable t) {
                    Log.e(TAG, "開機自動啟動服務失敗: " + t.getMessage(), t);
                    new EfdNotificationHudManager(context).showStartMonitoringReminder();
                }
            }

            // 開機時檢查是否有未上傳的資料 (統一經由 EfdSyncManager，避免重複上傳)
            EfdSyncManager.checkAndPerformDailySync(context, "EfdBootReceiver");
        }
    }
}
