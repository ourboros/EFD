package com.efd.eyefatiguedetection;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.content.Context;
import android.content.Intent;
import android.os.Build;

import androidx.annotation.NonNull;
import androidx.core.app.NotificationCompat;

public class EfdNotificationHudManager {
    public static final String CHANNEL_ID = "efd_fatigue_hud_channel";
    public static final int NOTIFICATION_ID = 1001;

    private final Context mContext;
    private final NotificationManager mNotificationManager;

    public EfdNotificationHudManager(@NonNull Context context) {
        this.mContext = context.getApplicationContext();
        this.mNotificationManager = (NotificationManager) mContext.getSystemService(Context.NOTIFICATION_SERVICE);
        createNotificationChannel();
    }

    private void createNotificationChannel() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            NotificationChannel channel = new NotificationChannel(
                    CHANNEL_ID,
                    "EFD 眼睛疲勞即時狀態",
                    NotificationManager.IMPORTANCE_HIGH
            );
            channel.setDescription("顯示 EFD 即時眼睛疲勞數值與提醒通知");
            if (mNotificationManager != null) {
                mNotificationManager.createNotificationChannel(channel);
            }
        }
    }

    public Notification buildForegroundNotification(String statusText, float score, int level) {
        Intent launchIntent = new Intent(mContext, MainActivity.class);
        launchIntent.setFlags(Intent.FLAG_ACTIVITY_CLEAR_TOP | Intent.FLAG_ACTIVITY_SINGLE_TOP);
        PendingIntent pendingIntent = PendingIntent.getActivity(
                mContext, 0, launchIntent,
                PendingIntent.FLAG_UPDATE_CURRENT | (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M ? PendingIntent.FLAG_IMMUTABLE : 0)
        );

        String title = "🟢 EFD 眼睛疲勞即時監控中";
        if (level == 2) {
            title = "🚨 EFD 眼睛疲勞警告：請適當休息！";
        } else if (level == 1) {
            title = "⚠️ EFD 用眼注意提醒";
        }

        String content = String.format(java.util.Locale.getDefault(), "%s | 疲勞分數: %.1f / 100", statusText, score);

        return new NotificationCompat.Builder(mContext, CHANNEL_ID)
                .setSmallIcon(android.R.drawable.ic_dialog_alert)
                .setContentTitle(title)
                .setContentText(content)
                .setPriority(NotificationCompat.PRIORITY_HIGH)
                .setOngoing(true)
                .setContentIntent(pendingIntent)
                .build();
    }

    public void updateNotification(String statusText, float score, int level) {
        if (mNotificationManager != null) {
            mNotificationManager.notify(NOTIFICATION_ID, buildForegroundNotification(statusText, score, level));
        }
    }
}
