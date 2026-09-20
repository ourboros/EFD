#include <jni.h>
#include <string>
#include <memory>
#include <sstream>
#include <iomanip>
#include <android/log.h>

#include "efd/version.hpp"
#include "efd/types.hpp"
#include "engine/AsyncPipelineEngine.hpp"

#define TAG "EFD_Native_JNI"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

static std::unique_ptr<efd::AsyncPipelineEngine> g_engine = nullptr;
static std::string g_lastAlertMessage = "";
static int g_lastAlertLevel = 0;
static float g_lastAlertScore = 0.0f;
static std::mutex g_alertMutex;

extern "C" {

JNIEXPORT jboolean JNICALL
Java_com_efd_eyefatiguedetection_EfdNativeBridge_nativeInitEngine(JNIEnv* /*env*/, jobject /*thiz*/) {
    try {
        g_engine = std::make_unique<efd::AsyncPipelineEngine>(efd::PlatformType::Android);
        
        g_engine->setAlertCallback([](efd::FatigueLevel level, float score, const std::string& msg) {
            std::lock_guard<std::mutex> lock(g_alertMutex);
            g_lastAlertLevel = static_cast<int>(level);
            g_lastAlertScore = score;
            g_lastAlertMessage = msg;
            LOGI("Alert triggered: Level=%d, Score=%.1f, Msg=%s", static_cast<int>(level), score, msg.c_str());
        });

        g_engine->start();
        LOGI("EFD AsyncPipelineEngine started successfully on Android NDK.");
        return JNI_TRUE;
    } catch (const std::exception& e) {
        LOGE("Failed to init EFD Engine: %s", e.what());
        return JNI_FALSE;
    }
}

JNIEXPORT void JNICALL
Java_com_efd_eyefatiguedetection_EfdNativeBridge_nativeCalibrate(JNIEnv* /*env*/, jobject /*thiz*/, jfloat durationSec) {
    if (g_engine) {
        g_engine->calibrate(durationSec);
        LOGI("Baseline calibrated: threshold=%.3f", g_engine->getAdaptiveBaseline().getCurrentThreshold());
    }
}

JNIEXPORT void JNICALL
Java_com_efd_eyefatiguedetection_EfdNativeBridge_nativeSetSimulatedEyeOpenness(JNIEnv* /*env*/, jobject /*thiz*/, jfloat openness) {
    if (g_engine) {
        g_engine->setSimulatedEyeOpenness(openness);
    }
}

JNIEXPORT jstring JNICALL
Java_com_efd_eyefatiguedetection_EfdNativeBridge_nativeGetTelemetryJson(JNIEnv* env, jobject /*thiz*/) {
    if (!g_engine) {
        return env->NewStringUTF("{\"error\":\"Engine not initialized\"}");
    }

    std::lock_guard<std::mutex> lock(g_alertMutex);
    
    // 獲取最新狀態
    auto state = g_engine->getStateMachine().getState();
    float threshold = g_engine->getAdaptiveBaseline().getCurrentThreshold();

    std::ostringstream oss;
    oss << std::fixed << std::setprecision(3);
    oss << "{"
        << "\"fatigueScore\":" << state.currentFatigueScore << ","
        << "\"fatigueLevel\":" << static_cast<int>(state.fatigueLevel) << ","
        << "\"cooldownState\":" << static_cast<int>(state.cooldownState) << ","
        << "\"cooldownRemaining\":" << state.cooldownRemainingSeconds << ","
        << "\"threshold\":" << threshold << ","
        << "\"studyDay\":" << state.studyDay << ","
        << "\"isStudyLocked\":" << (state.isStudyLocked ? "true" : "false") << ","
        << "\"lastAlertLevel\":" << g_lastAlertLevel << ","
        << "\"lastAlertScore\":" << g_lastAlertScore << ","
        << "\"lastAlertMsg\":\"" << g_lastAlertMessage << "\""
        << "}";

    return env->NewStringUTF(oss.str().c_str());
}

JNIEXPORT jstring JNICALL
Java_com_efd_eyefatiguedetection_EfdNativeBridge_nativeSubmitQuestionnaire(JNIEnv* env, jobject /*thiz*/, jstring surveyData) {
    if (!g_engine) {
        return env->NewStringUTF("ERROR");
    }

    const char* str = env->GetStringUTFChars(surveyData, nullptr);
    std::string data(str ? str : "");
    if (str) env->ReleaseStringUTFChars(surveyData, str);

    g_engine->getStudyTracker().submitQuestionnaire(data);
    std::string token = g_engine->getStudyTracker().getUnlockToken();
    return env->NewStringUTF(token.c_str());
}

JNIEXPORT void JNICALL
Java_com_efd_eyefatiguedetection_EfdNativeBridge_nativeTriggerTestAlert(JNIEnv* /*env*/, jobject /*thiz*/) {
    std::lock_guard<std::mutex> lock(g_alertMutex);
    g_lastAlertLevel = 2; // SevereWarning
    g_lastAlertScore = 88.5f;
    g_lastAlertMessage = "你的眼睛處於疲勞狀態，請適當休息";
}

JNIEXPORT void JNICALL
Java_com_efd_eyefatiguedetection_EfdNativeBridge_nativeStopEngine(JNIEnv* /*env*/, jobject /*thiz*/) {
    if (g_engine) {
        g_engine->stop();
        g_engine.reset();
        LOGI("EFD AsyncPipelineEngine stopped.");
    }
}

} // extern "C"

