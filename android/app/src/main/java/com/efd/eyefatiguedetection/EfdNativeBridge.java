package com.efd.eyefatiguedetection;

import android.util.Log;

public class EfdNativeBridge {
    private static final String TAG = "EFD_NativeBridge";
    private static boolean sLibraryLoaded = false;

    // 引擎未啟動或原生層異常時回傳的狀態；UI 需據此顯示「偵測引擎未啟動」，不可顯示假數值
    public static final String ENGINE_NOT_RUNNING_JSON = "{\"engineRunning\":false}";

    static {
        try {
            System.loadLibrary("efd_jni");
            sLibraryLoaded = true;
            Log.i(TAG, "libefd_jni.so successfully loaded.");
        } catch (Throwable t) {
            Log.e(TAG, "Failed to load libefd_jni.so: " + t.getMessage(), t);
            sLibraryLoaded = false;
        }
    }

    public static boolean isLoaded() {
        return sLibraryLoaded;
    }

    public static boolean initEngine() {
        if (!sLibraryLoaded) return false;
        try {
            return nativeInitEngine();
        } catch (Throwable t) {
            Log.e(TAG, "nativeInitEngine failed: " + t.getMessage(), t);
            return false;
        }
    }

    public static boolean initEngineWithPath(String storageDir) {
        if (!sLibraryLoaded) return false;
        try {
            return nativeInitEngineWithPath(storageDir);
        } catch (Throwable t) {
            Log.e(TAG, "nativeInitEngineWithPath failed: " + t.getMessage(), t);
            return false;
        }
    }

    public static boolean isEngineRunning() {
        if (!sLibraryLoaded) return false;
        try {
            return nativeIsEngineRunning();
        } catch (Throwable t) {
            Log.e(TAG, "nativeIsEngineRunning failed: " + t.getMessage(), t);
            return false;
        }
    }

    // 兩段式校準：beginCalibration() 開始收集 -> 數秒後 finishCalibration() 驗證有效人臉幀
    public static void beginCalibration() {
        if (!sLibraryLoaded) return;
        try {
            nativeBeginCalibration();
        } catch (Throwable t) {
            Log.e(TAG, "nativeBeginCalibration failed: " + t.getMessage(), t);
        }
    }

    public static boolean finishCalibration() {
        if (!sLibraryLoaded) return false;
        try {
            return nativeFinishCalibration();
        } catch (Throwable t) {
            Log.e(TAG, "nativeFinishCalibration failed: " + t.getMessage(), t);
            return false;
        }
    }

    public static boolean hasSavedCalibration() {
        if (!sLibraryLoaded) return false;
        try {
            return nativeHasSavedCalibration();
        } catch (Throwable t) {
            Log.e(TAG, "nativeHasSavedCalibration failed: " + t.getMessage(), t);
            return false;
        }
    }

    public static void setSimulatedEyeOpenness(float openness) {
        if (!sLibraryLoaded) return;
        try {
            nativeSetSimulatedEyeOpenness(openness);
        } catch (Throwable t) {
            Log.e(TAG, "nativeSetSimulatedEyeOpenness failed: " + t.getMessage(), t);
        }
    }

    public static String getTelemetryJson() {
        if (!sLibraryLoaded) {
            return ENGINE_NOT_RUNNING_JSON;
        }
        try {
            return nativeGetTelemetryJson();
        } catch (Throwable t) {
            Log.e(TAG, "nativeGetTelemetryJson failed: " + t.getMessage(), t);
            return ENGINE_NOT_RUNNING_JSON;
        }
    }

    // 取出待發送的疲勞警報 (讀後即清除)；只應由 EfdForegroundService 呼叫
    public static String consumeAlertJson() {
        if (!sLibraryLoaded) return "{\"level\":0}";
        try {
            return nativeConsumeAlertJson();
        } catch (Throwable t) {
            Log.e(TAG, "nativeConsumeAlertJson failed: " + t.getMessage(), t);
            return "{\"level\":0}";
        }
    }

    // 產生並保存解鎖憑證；失敗時回傳空字串 (不再回傳假憑證)
    public static String submitQuestionnaire(String surveyData) {
        if (!sLibraryLoaded) return "";
        try {
            String token = nativeSubmitQuestionnaire(surveyData);
            return (token == null || "ERROR".equals(token)) ? "" : token;
        } catch (Throwable t) {
            Log.e(TAG, "nativeSubmitQuestionnaire failed: " + t.getMessage(), t);
            return "";
        }
    }

    // 已保存的解鎖憑證；尚未填寫後測問卷或引擎未啟動時為空字串
    public static String getUnlockToken() {
        if (!sLibraryLoaded) return "";
        try {
            String token = nativeGetUnlockToken();
            return token == null ? "" : token;
        } catch (Throwable t) {
            Log.e(TAG, "nativeGetUnlockToken failed: " + t.getMessage(), t);
            return "";
        }
    }

    // 提示靈敏度 (0 低 / 1 標準 / 2 高)；由 C++ 保存到設定檔
    public static void setSensitivity(int level) {
        if (!sLibraryLoaded) return;
        try {
            nativeSetSensitivity(level);
        } catch (Throwable t) {
            Log.e(TAG, "nativeSetSensitivity failed: " + t.getMessage(), t);
        }
    }

    // 引擎未啟動時回傳 -1
    public static int getSensitivity() {
        if (!sLibraryLoaded) return -1;
        try {
            return nativeGetSensitivity();
        } catch (Throwable t) {
            Log.e(TAG, "nativeGetSensitivity failed: " + t.getMessage(), t);
            return -1;
        }
    }

    public static void triggerTestAlert() {
        if (!sLibraryLoaded) return;
        try {
            nativeTriggerTestAlert();
        } catch (Throwable t) {
            Log.e(TAG, "nativeTriggerTestAlert failed: " + t.getMessage(), t);
        }
    }

    public static void sendFrame(byte[] frameData, int width, int height, int rotation) {
        if (!sLibraryLoaded || frameData == null) return;
        try {
            nativeSendFrame(frameData, width, height, rotation);
        } catch (Throwable t) {
            Log.e(TAG, "nativeSendFrame failed: " + t.getMessage(), t);
        }
    }

    // 引擎未啟動時回傳空字串，由 UI 顯示「--」
    public static String getSubjectUuid() {
        if (!sLibraryLoaded) return "";
        try {
            return nativeGetSubjectUuid();
        } catch (Throwable t) {
            Log.e(TAG, "nativeGetSubjectUuid failed: " + t.getMessage(), t);
            return "";
        }
    }

    // 引擎未啟動時回傳 0
    public static int getCurrentDay() {
        if (!sLibraryLoaded) return 0;
        try {
            return nativeGetCurrentDay();
        } catch (Throwable t) {
            Log.e(TAG, "nativeGetCurrentDay failed: " + t.getMessage(), t);
            return 0;
        }
    }

    public static String getCloudSyncPayloadJson() {
        if (!sLibraryLoaded) return "";
        try {
            return nativeGetCloudSyncPayloadJson();
        } catch (Throwable t) {
            Log.e(TAG, "nativeGetCloudSyncPayloadJson failed: " + t.getMessage(), t);
            return "";
        }
    }

    public static void stopEngine() {
        if (!sLibraryLoaded) return;
        try {
            nativeStopEngine();
        } catch (Throwable t) {
            Log.e(TAG, "nativeStopEngine failed: " + t.getMessage(), t);
        }
    }

    private static native boolean nativeInitEngine();
    private static native boolean nativeInitEngineWithPath(String storageDir);
    private static native boolean nativeIsEngineRunning();
    private static native void nativeBeginCalibration();
    private static native boolean nativeFinishCalibration();
    private static native boolean nativeHasSavedCalibration();
    private static native void nativeSetSimulatedEyeOpenness(float openness);
    private static native void nativeSendFrame(byte[] frameData, int width, int height, int rotation);
    private static native String nativeGetTelemetryJson();
    private static native String nativeConsumeAlertJson();
    private static native String nativeGetSubjectUuid();
    private static native int nativeGetCurrentDay();
    private static native String nativeGetCloudSyncPayloadJson();
    private static native String nativeSubmitQuestionnaire(String surveyData);
    private static native String nativeGetUnlockToken();
    private static native void nativeSetSensitivity(int level);
    private static native int nativeGetSensitivity();
    private static native void nativeTriggerTestAlert();
    private static native void nativeStopEngine();
}
