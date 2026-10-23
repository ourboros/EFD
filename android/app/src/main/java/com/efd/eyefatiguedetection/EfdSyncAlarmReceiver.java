package com.efd.eyefatiguedetection;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.util.Log;

public class EfdSyncAlarmReceiver extends BroadcastReceiver {
    private static final String TAG = "EFD_SyncAlarmReceiver";

    @Override
    public void onReceive(Context context, Intent intent) {
        Log.i(TAG, "⏰ 21:00 (9 PM) 鬧鐘廣播精準觸發！呼叫 EfdSyncManager 執行單次同步...");
        EfdSyncManager.checkAndPerformDailySync(context, "Nightly_2100_AlarmManager_Sync");
    }
}
