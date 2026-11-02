package com.efd.eyefatiguedetection;

import android.app.KeyguardManager;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.util.Log;

import androidx.annotation.NonNull;

/**
 * 螢幕開關監聽：由前景服務註冊 (Activity 被劃掉後仍有效)。
 * 螢幕關閉代表使用者沒有在看手機，暫停相機與引擎省電；解鎖後恢復。
 */
public class EfdScreenStateReceiver extends BroadcastReceiver {
    private static final String TAG = "EFD_ScreenStateReceiver";

    public interface Listener {
        void onScreenOff();
        /** 解鎖 (或沒有鎖定畫面時螢幕亮起)，使用者開始使用手機。 */
        void onUserPresent();
    }

    private final Listener mListener;
    private boolean mIsRegistered = false;

    public EfdScreenStateReceiver(@NonNull Listener listener) {
        mListener = listener;
    }

    @Override
    public void onReceive(Context context, Intent intent) {
        if (intent == null || intent.getAction() == null) return;

        String action = intent.getAction();
        if (Intent.ACTION_SCREEN_OFF.equals(action)) {
            Log.i(TAG, "螢幕已關閉，暫停相機與偵測引擎以省電");
            mListener.onScreenOff();
        } else if (Intent.ACTION_USER_PRESENT.equals(action)) {
            Log.i(TAG, "螢幕已解鎖，恢復相機與疲勞監控");
            mListener.onUserPresent();
        } else if (Intent.ACTION_SCREEN_ON.equals(action) && !isKeyguardLocked(context)) {
            // 沒有設定鎖定畫面時不一定會收到 USER_PRESENT
            Log.i(TAG, "螢幕已亮起 (未鎖定)，恢復相機與疲勞監控");
            mListener.onUserPresent();
        }
    }

    private static boolean isKeyguardLocked(Context context) {
        KeyguardManager km = (KeyguardManager) context.getSystemService(Context.KEYGUARD_SERVICE);
        return km != null && km.isKeyguardLocked();
    }

    public void register(Context context) {
        if (mIsRegistered || context == null) return;
        IntentFilter filter = new IntentFilter();
        filter.addAction(Intent.ACTION_SCREEN_OFF);
        filter.addAction(Intent.ACTION_SCREEN_ON);
        filter.addAction(Intent.ACTION_USER_PRESENT);
        context.registerReceiver(this, filter);
        mIsRegistered = true;
        Log.i(TAG, "螢幕開關廣播監聽器已註冊");
    }

    public void unregister(Context context) {
        if (!mIsRegistered || context == null) return;
        try {
            context.unregisterReceiver(this);
        } catch (Throwable t) {
            // ignore
        }
        mIsRegistered = false;
        Log.i(TAG, "螢幕開關廣播監聽器已取消註冊");
    }
}
