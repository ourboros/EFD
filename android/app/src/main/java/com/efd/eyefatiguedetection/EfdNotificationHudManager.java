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
    public static final String SERVICE_CHANNEL_ID = "efd_service_channel";
    public static final String ALERT_CHANNEL_ID = "efd_fatigue_alert_channel";
    public static final int SERVICE_NOTIFICATION_ID = 1001;
    public static final int ALERT_NOTIFICATION_ID = 1002;

    private final Context mContext;
    private final NotificationManager mNotificationManager;

    public EfdNotificationHudManager(@NonNull Context context) {
        this.mContext = context.getApplicationContext();
        this.mNotificationManager = (NotificationManager) mContext.getSystemService(Context.NOTIFICATION_SERVICE);
        createNotificationChannels();
    }

    private void createNotificationChannels() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            // 靜默背景服務通道 (LOW IMPORTANCE，不安擾使用者)
            NotificationChannel serviceChannel = new NotificationChannel(
                    SERVICE_CHANNEL_ID,
                    "EFD 背景靜默監控服務",
                    NotificationManager.IMPORTANCE_LOW
            );
            serviceChannel.setDescription("保持 EFD 背景相機與眼動疲勞檢測持續運作");

            // 疲勞警報通道 (HIGH IMPORTANCE，過度疲勞時調出提醒)
            NotificationChannel alertChannel = new NotificationChannel(
                    ALERT_CHANNEL_ID,
                    "EFD 眼睛疲勞警告提醒",
                    NotificationManager.IMPORTANCE_HIGH
            );
            alertChannel.setDescription("當檢測到過度疲勞時彈出高優先級提醒橫幅");

            if (mNotificationManager != null) {
                mNotificationManager.createNotificationChannel(serviceChannel);
                mNotificationManager.createNotificationChannel(alertChannel);
            }
        }
    }

    public Notification buildForegroundNotification() {
        Intent launchIntent = new Intent(mContext, MainActivity.class);
        launchIntent.setFlags(Intent.FLAG_ACTIVITY_CLEAR_TOP | Intent.FLAG_ACTIVITY_SINGLE_TOP);
        PendingIntent pendingIntent = PendingIntent.getActivity(
                mContext, 0, launchIntent,
                PendingIntent.FLAG_UPDATE_CURRENT | (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M ? PendingIntent.FLAG_IMMUTABLE : 0)
        );

        return new NotificationCompat.Builder(mContext, SERVICE_CHANNEL_ID)
                .setSmallIcon(android.R.drawable.ic_dialog_info)
                .setContentTitle("EFD 眼睛疲勞背景監控中")
                .setContentText("系統即時防護持續運作中，僅於過度疲勞時跳出提醒")
                .setPriority(NotificationCompat.PRIORITY_LOW)
                .setOngoing(true)
                .setContentIntent(pendingIntent)
                .build();
    }

    public void triggerSevereFatigueAlert(String title, String message) {
        if (mNotificationManager == null) return;

        Intent launchIntent = new Intent(mContext, MainActivity.class);
        launchIntent.setFlags(Intent.FLAG_ACTIVITY_CLEAR_TOP | Intent.FLAG_ACTIVITY_SINGLE_TOP);
        PendingIntent pendingIntent = PendingIntent.getActivity(
                mContext, 0, launchIntent,
                PendingIntent.FLAG_UPDATE_CURRENT | (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M ? PendingIntent.FLAG_IMMUTABLE : 0)
        );

        Notification alertNotification = new NotificationCompat.Builder(mContext, ALERT_CHANNEL_ID)
                .setSmallIcon(android.R.drawable.ic_dialog_alert)
                .setContentTitle(title)
                .setContentText(message)
                .setPriority(NotificationCompat.PRIORITY_HIGH)
                .setDefaults(Notification.DEFAULT_ALL)
                .setAutoCancel(true)
                .setContentIntent(pendingIntent)
                .build();

        mNotificationManager.notify(ALERT_NOTIFICATION_ID, alertNotification);
    }
}
