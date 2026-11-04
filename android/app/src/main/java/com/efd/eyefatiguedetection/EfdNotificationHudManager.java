package com.efd.eyefatiguedetection;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.content.Context;
import android.content.Intent;
import android.os.Build;
import android.widget.RemoteViews;

import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import androidx.core.app.NotificationCompat;
import androidx.core.app.NotificationManagerCompat;

public class EfdNotificationHudManager {
    public static final String SERVICE_CHANNEL_ID = "efd_service_channel";
    public static final String ALERT_CHANNEL_ID = "efd_fatigue_alert_channel";
    public static final int SERVICE_NOTIFICATION_ID = 1001;
    public static final int ALERT_NOTIFICATION_ID = 1002;
    public static final int START_REMINDER_NOTIFICATION_ID = 1003;

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

        // 「停止偵測」：使用者隨時可關閉相機偵測 (清楚揭露相機用途並提供停止方式)
        Intent stopIntent = new Intent(mContext, EfdForegroundService.class);
        stopIntent.setAction(EfdForegroundService.ACTION_STOP_MONITORING);
        PendingIntent stopPendingIntent = PendingIntent.getService(
                mContext, 1, stopIntent,
                PendingIntent.FLAG_UPDATE_CURRENT | (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M ? PendingIntent.FLAG_IMMUTABLE : 0)
        );

        return new NotificationCompat.Builder(mContext, SERVICE_CHANNEL_ID)
                .setSmallIcon(android.R.drawable.ic_menu_camera)
                .setContentTitle("EFD 正在使用前鏡頭偵測眼睛疲勞")
                .setContentText("影像只在手機上分析、不會上傳；疲勞時才會提醒")
                .setStyle(new NotificationCompat.BigTextStyle().bigText(
                        "EFD 正在使用前鏡頭偵測眼睛疲勞。影像只在這支手機上即時分析，不會儲存或上傳；"
                        + "每日只上傳疲勞數值統計。疲勞時才會提醒你休息。"))
                .setPriority(NotificationCompat.PRIORITY_LOW)
                .setOngoing(true)
                .setContentIntent(pendingIntent)
                .addAction(android.R.drawable.ic_menu_close_clear_cancel, "停止偵測", stopPendingIntent)
                .build();
    }

    /**
     * 疲勞提醒能否實際顯示：Android 13+ 的通知權限被拒絕、或使用者在系統設定關閉了
     * 「EFD 眼睛疲勞警告提醒」類別時，notify() 會無聲無息地被丟棄。
     */
    public static boolean canPostFatigueAlerts(@NonNull Context context) {
        if (!NotificationManagerCompat.from(context).areNotificationsEnabled()) return false;
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            NotificationManager nm = (NotificationManager) context.getSystemService(Context.NOTIFICATION_SERVICE);
            NotificationChannel channel = (nm != null) ? nm.getNotificationChannel(ALERT_CHANNEL_ID) : null;
            return channel == null || channel.getImportance() != NotificationManager.IMPORTANCE_NONE;
        }
        return true;
    }

    /**
     * 背景偵測沒有在執行時提醒使用者 (例如廠商系統在 App 被劃掉時一併關閉服務，
     * 而 Android 14+ 不允許從背景重新啟動相機類型的前景服務)。
     */
    public void showMonitoringStoppedReminder() {
        postStartReminder("EFD 疲勞偵測已停止", "背景偵測被系統關閉了，點此重新開始偵測");
    }

    public void cancelStartReminder() {
        if (mNotificationManager != null) {
            mNotificationManager.cancel(START_REMINDER_NOTIFICATION_ID);
        }
    }

    /** 開機後 (Android 14+ 不允許自動開相機) 提醒使用者點一下以開始今日偵測。 */
    public void showStartMonitoringReminder() {
        postStartReminder("EFD 尚未開始今日偵測", "點此開啟 EFD，背景疲勞偵測才會啟動");
    }

    private void postStartReminder(String title, String text) {
        if (mNotificationManager == null) return;

        Intent launchIntent = new Intent(mContext, MainActivity.class);
        launchIntent.setFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_CLEAR_TOP);
        PendingIntent pendingIntent = PendingIntent.getActivity(
                mContext, START_REMINDER_NOTIFICATION_ID, launchIntent,
                PendingIntent.FLAG_UPDATE_CURRENT | (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M ? PendingIntent.FLAG_IMMUTABLE : 0)
        );

        Notification notification = new NotificationCompat.Builder(mContext, ALERT_CHANNEL_ID)
                .setSmallIcon(android.R.drawable.ic_dialog_info)
                .setContentTitle(title)
                .setContentText(text)
                .setPriority(NotificationCompat.PRIORITY_HIGH)
                .setOnlyAlertOnce(true)
                .setAutoCancel(true)
                .setContentIntent(pendingIntent)
                .build();

        try {
            mNotificationManager.notify(START_REMINDER_NOTIFICATION_ID, notification);
        } catch (SecurityException e) {
            // Android 13+ 未授權通知權限
        }
    }

    // 通知上的自評按鈕 id (依 0~10 排列)
    private static final int[] EYE_FATIGUE_BUTTON_IDS = {
            R.id.eye_fatigue_0, R.id.eye_fatigue_1, R.id.eye_fatigue_2, R.id.eye_fatigue_3,
            R.id.eye_fatigue_4, R.id.eye_fatigue_5, R.id.eye_fatigue_6, R.id.eye_fatigue_7,
            R.id.eye_fatigue_8, R.id.eye_fatigue_9, R.id.eye_fatigue_10};
    // 待詢問提醒的有效時間，與 App 開啟時補問的期限相同
    private static final long EYE_FATIGUE_ANSWER_TIMEOUT_MS = 30 * 60_000L;

    private PendingIntent eyeFatigueAnswerIntent(long alertTimeMs, int value) {
        Intent intent = new Intent(mContext, EfdEyeFatigueReceiver.class);
        intent.setAction(EfdEyeFatigueReceiver.ACTION_ANSWER);
        intent.putExtra(EfdEyeFatigueReceiver.EXTRA_ALERT_TIME_MS, alertTimeMs);
        intent.putExtra(EfdEyeFatigueReceiver.EXTRA_VALUE, value);
        // 每個分數各自一個 requestCode，否則 PendingIntent 會互相覆蓋
        return PendingIntent.getBroadcast(
                mContext, 2000 + value, intent,
                PendingIntent.FLAG_UPDATE_CURRENT | (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M ? PendingIntent.FLAG_IMMUTABLE : 0)
        );
    }

    private RemoteViews buildEyeFatigueView(int layoutId, long alertTimeMs, @Nullable String message) {
        RemoteViews views = new RemoteViews(mContext.getPackageName(), layoutId);
        if (message != null) views.setTextViewText(R.id.eye_fatigue_message, message);
        for (int v = 0; v < EYE_FATIGUE_BUTTON_IDS.length; v++) {
            views.setOnClickPendingIntent(EYE_FATIGUE_BUTTON_IDS[v], eyeFatigueAnswerIntent(alertTimeMs, v));
        }
        return views;
    }

    /**
     * 重度疲勞提醒：通知上直接顯示 0~10 自評按鈕，點一下即記錄 (EfdEyeFatigueReceiver)；
     * 點通知本身開啟 App 時照舊補問，劃掉通知記為略過。alertTimeMs = 0 為測試通知 (按鈕不記錄)。
     */
    public void triggerSevereFatigueAlert(String title, String message, long alertTimeMs) {
        if (mNotificationManager == null) return;

        Intent launchIntent = new Intent(mContext, MainActivity.class);
        launchIntent.setFlags(Intent.FLAG_ACTIVITY_CLEAR_TOP | Intent.FLAG_ACTIVITY_SINGLE_TOP);
        PendingIntent pendingIntent = PendingIntent.getActivity(
                mContext, 0, launchIntent,
                PendingIntent.FLAG_UPDATE_CURRENT | (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M ? PendingIntent.FLAG_IMMUTABLE : 0)
        );

        RemoteViews bigView = buildEyeFatigueView(R.layout.notification_eye_fatigue_big, alertTimeMs, message);
        Notification alertNotification = new NotificationCompat.Builder(mContext, ALERT_CHANNEL_ID)
                .setSmallIcon(android.R.drawable.ic_dialog_alert)
                .setContentTitle(title)
                .setContentText(message)
                .setStyle(new NotificationCompat.DecoratedCustomViewStyle())
                .setCustomContentView(buildEyeFatigueView(R.layout.notification_eye_fatigue, alertTimeMs, null))
                .setCustomBigContentView(bigView)
                .setCustomHeadsUpContentView(bigView)
                .setPriority(NotificationCompat.PRIORITY_HIGH)
                .setDefaults(Notification.DEFAULT_ALL)
                .setAutoCancel(true)
                .setTimeoutAfter(EYE_FATIGUE_ANSWER_TIMEOUT_MS)
                .setContentIntent(pendingIntent)
                .setDeleteIntent(eyeFatigueAnswerIntent(alertTimeMs, -1))
                .build();

        try {
            mNotificationManager.notify(ALERT_NOTIFICATION_ID, alertNotification);
        } catch (SecurityException e) {
            // Android 13+ 未授權通知權限：由 MainActivity 引導使用者開啟
        }
    }
}
