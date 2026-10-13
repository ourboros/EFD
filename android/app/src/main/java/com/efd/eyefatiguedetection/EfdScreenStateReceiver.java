package com.efd.eyefatiguedetection;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.util.Log;

public class EfdScreenStateReceiver extends BroadcastReceiver {
    private static final String TAG = "EFD_ScreenStateReceiver";
    private boolean mIsRegistered = false;

    @Override
    public void onReceive(Context context, Intent intent) {
        if (intent == null || intent.getAction() == null) return;

        String action = intent.getAction();
        if (Intent.ACTION_SCREEN_OFF.equals(action)) {
            Log.i(TAG, "螢幕已鎖屏/關閉，暫停相機與推論管線以省電");
            // 釋放資源省電
        } else if (Intent.ACTION_USER_PRESENT.equals(action) || Intent.ACTION_SCREEN_ON.equals(action)) {
            Log.i(TAG, "螢幕已解鎖，熱重啟相機與疲勞監控");
            // 熱重啟監控
        }
    }

    public void register(Context context) {
        if (mIsRegistered || context == null) return;
        IntentFilter filter = new IntentFilter();
        filter.addAction(Intent.ACTION_SCREEN_OFF);
        filter.addAction(Intent.ACTION_SCREEN_ON);
        filter.addAction(Intent.ACTION_USER_PRESENT);
        context.registerReceiver(this, filter);
        mIsRegistered = true;
        Log.i(TAG, "螢幕鎖屏廣播監聽器已註冊");
    }

    public void unregister(Context context) {
        if (!mIsRegistered || context == null) return;
        try {
            context.unregisterReceiver(this);
        } catch (Throwable t) {
            // ignore
        }
        mIsRegistered = false;
        Log.i(TAG, "螢幕鎖屏廣播監聽器已取消註冊");
    }
}
