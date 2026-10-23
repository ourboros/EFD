package com.efd.eyefatiguedetection;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.util.Log;

public class EfdSyncAlarmReceiver extends BroadcastReceiver {
    private static final String TAG = "EFD_SyncAlarmReceiver";
    public static final String EXTRA_IS_RETRY = "efd_is_retry";

    @Override
    public void onReceive(Context context, Intent intent) {
        boolean isRetry = intent != null && intent.getBooleanExtra(EXTRA_IS_RETRY, false);
        if (isRetry) {
            // 重試時依當下時間重新判斷 (21:00 前的重試屬於補傳，不可標記為今天)
            Log.i(TAG, "上傳失敗後的 10 分鐘重試鬧鐘觸發");
            EfdSyncManager.checkAndPerformDailySync(context, "Retry_AlarmManager_Sync");
        } else {
            Log.i(TAG, "21:00 (9 PM) 每日同步鬧鐘觸發，呼叫 EfdSyncManager 執行單次同步...");
            EfdSyncManager.checkAndPerformDailySync(context, "Nightly_2100_AlarmManager_Sync");
        }
    }
}
