package com.efd.eyefatiguedetection;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.os.Build;
import android.util.Log;

public class EfdBootReceiver extends BroadcastReceiver {
    private static final String TAG = "EFD_BootReceiver";

    @Override
    public void onReceive(Context context, Intent intent) {
        if (intent == null || intent.getAction() == null) return;

        String action = intent.getAction();
        if (Intent.ACTION_BOOT_COMPLETED.equals(action) ||
            "android.intent.action.QUICKBOOT_POWERON".equals(action) ||
            Intent.ACTION_MY_PACKAGE_REPLACED.equals(action)) {

            Log.i(TAG, "📱 手機開機/重啟，自動發動 EFD 背景疲勞常駐監控服務...");
            try {
                Intent serviceIntent = new Intent(context, EfdForegroundService.class);
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
                    context.startForegroundService(serviceIntent);
                } else {
                    context.startService(serviceIntent);
                }
            } catch (Throwable t) {
                Log.e(TAG, "開機自動啟動服務失敗: " + t.getMessage(), t);
            }
        }
    }
}
