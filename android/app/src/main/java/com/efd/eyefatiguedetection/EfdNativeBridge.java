package com.efd.eyefatiguedetection;

public class EfdNativeBridge {
    static {
        try {
            System.loadLibrary("efd_jni");
        } catch (UnsatisfiedLinkError e) {
            e.printStackTrace();
        }
    }

    public static native boolean nativeInitEngine();
    public static native void nativeCalibrate(float durationSec);
    public static native void nativeSetSimulatedEyeOpenness(float openness);
    public static native String nativeGetTelemetryJson();
    public static native String nativeSubmitQuestionnaire(String surveyData);
    public static native void nativeTriggerTestAlert();
    public static native void nativeStopEngine();
}

