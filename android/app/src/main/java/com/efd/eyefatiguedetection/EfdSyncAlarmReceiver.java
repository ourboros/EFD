package com.efd.eyefatiguedetection;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.util.Log;

public class EfdSyncAlarmReceiver extends BroadcastReceiver {
    private static final String TAG = "EFD_SyncAlarmReceiver";

    @Override
    public void onReceive(Context context, Intent intent) {
        Log.i(TAG, "⏰ 21:00 (9 PM) 鬧鐘廣播精準觸發！開始將當日眼動資料自動同步至 Google 試算表...");

        EfdHttpSyncBridge.uploadEyelidDataAsync("SUBJ-ANDROID-001", 1, "Nightly_2100_AlarmManager_Sync", (success, responseMessage) -> {
            if (success) {
                Log.i(TAG, "✅ 21:00 自動上傳 Google 試算表成功！伺服器回應: " + responseMessage);
            } else {
                Log.e(TAG, "❌ 21:00 自動上傳 Google 試算表失敗: " + responseMessage);
            }
        });
    }
}
