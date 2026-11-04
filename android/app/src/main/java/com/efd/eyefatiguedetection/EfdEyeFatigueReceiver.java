package com.efd.eyefatiguedetection;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.util.Log;
import android.widget.Toast;

import androidx.core.app.NotificationManagerCompat;

/**
 * 疲勞提醒通知上的 0~10 自評按鈕：點一下即記錄，不必開啟 App；劃掉通知記為略過 (-1)。
 * 與 App 開啟時的補問共用同一筆「待詢問提醒」，每次提醒只會記錄一次。
 */
public class EfdEyeFatigueReceiver extends BroadcastReceiver {
    private static final String TAG = "EfdEyeFatigueReceiver";
    public static final String ACTION_ANSWER = "com.efd.eyefatiguedetection.EYE_FATIGUE_ANSWER";
    public static final String EXTRA_ALERT_TIME_MS = "alert_time_ms";
    public static final String EXTRA_VALUE = "eye_fatigue";

    @Override
    public void onReceive(Context context, Intent intent) {
        if (intent == null || !ACTION_ANSWER.equals(intent.getAction())) return;
        long alertTimeMs = intent.getLongExtra(EXTRA_ALERT_TIME_MS, 0L);
        int value = intent.getIntExtra(EXTRA_VALUE, -1);
        NotificationManagerCompat.from(context).cancel(EfdNotificationHudManager.ALERT_NOTIFICATION_ID);

        // 測試通知 (timeMs = 0)：只確認按鈕可用，不寫入研究資料
        if (alertTimeMs <= 0) {
            if (value >= 0) {
                Toast.makeText(context, "測試通知：按鈕正常 (" + value + " 分，不會記錄)", Toast.LENGTH_SHORT).show();
            }
            return;
        }
        if (!EfdForegroundService.takePendingEyeFatigueAlert(context, alertTimeMs)) {
            Log.i(TAG, "這次提醒已作答或已逾時，不重複記錄");
            return;
        }

        if (!EfdNativeBridge.isEngineRunning()) {
            EfdNativeBridge.initEngineWithPath(context.getFilesDir().getAbsolutePath());
        }
        float responseSec = Math.max(0L, System.currentTimeMillis() - alertTimeMs) / 1000f;
        EfdNativeBridge.recordEyeFatigueResponse(alertTimeMs, value, responseSec);
        if (value >= 0) {
            Toast.makeText(context, "已記錄：眼睛疲勞 " + value + " 分，請適當休息", Toast.LENGTH_SHORT).show();
        }
    }
}
