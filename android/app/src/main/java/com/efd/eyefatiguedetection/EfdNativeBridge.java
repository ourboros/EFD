package com.efd.eyefatiguedetection;

import android.util.Log;

public class EfdNativeBridge {
    private static final String TAG = "EFD_NativeBridge";
    private static boolean sLibraryLoaded = false;

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

    public static void calibrate(float durationSec) {
        if (!sLibraryLoaded) return;
        try {
            nativeCalibrate(durationSec);
        } catch (Throwable t) {
            Log.e(TAG, "nativeCalibrate failed: " + t.getMessage(), t);
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
            return "{\"fatigueScore\":15.0,\"fatigueLevel\":0,\"threshold\":0.265,\"lastAlertLevel\":0,\"lastAlertMsg\":\"\"}";
        }
        try {
            return nativeGetTelemetryJson();
        } catch (Throwable t) {
            Log.e(TAG, "nativeGetTelemetryJson failed: " + t.getMessage(), t);
            return "{\"fatigueScore\":15.0,\"fatigueLevel\":0,\"threshold\":0.265,\"lastAlertLevel\":0,\"lastAlertMsg\":\"\"}";
        }
    }

    public static String submitQuestionnaire(String surveyData) {
        if (!sLibraryLoaded) {
            return "EFD-14D-FALLBACK-TOKEN";
        }
        try {
            return nativeSubmitQuestionnaire(surveyData);
        } catch (Throwable t) {
            Log.e(TAG, "nativeSubmitQuestionnaire failed: " + t.getMessage(), t);
            return "EFD-14D-FALLBACK-TOKEN";
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

    public static void stopEngine() {
        if (!sLibraryLoaded) return;
        try {
            nativeStopEngine();
        } catch (Throwable t) {
            Log.e(TAG, "nativeStopEngine failed: " + t.getMessage(), t);
        }
    }

    private static native boolean nativeInitEngine();
    private static native void nativeCalibrate(float durationSec);
    private static native void nativeSetSimulatedEyeOpenness(float openness);
    private static native String nativeGetTelemetryJson();
    private static native String nativeSubmitQuestionnaire(String surveyData);
    private static native void nativeTriggerTestAlert();
    private static native void nativeStopEngine();
}
