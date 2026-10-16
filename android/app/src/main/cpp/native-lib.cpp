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

static efd::EngineTelemetry g_lastTelemetry;
static std::mutex g_telemetryMutex;

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

        g_engine->setTelemetryCallback([](const efd::EngineTelemetry& telemetry) {
            std::lock_guard<std::mutex> lock(g_telemetryMutex);
            g_lastTelemetry = telemetry;
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

JNIEXPORT void JNICALL
Java_com_efd_eyefatiguedetection_EfdNativeBridge_nativeSendFrame(JNIEnv* env, jobject /*thiz*/, jbyteArray frameData, jint width, jint height, jint /*rotation*/) {
    if (!g_engine || !frameData || width <= 0 || height <= 0) return;

    jsize len = env->GetArrayLength(frameData);
    if (len <= 0) return;

    jbyte* bytes = env->GetByteArrayElements(frameData, nullptr);
    if (!bytes) return;

    // 接收實體相機影格，關閉背景 Synthetic 虛擬驅動
    g_engine->getCameraService().stopSyntheticIfRunning();

    efd::RawFrame frame;
    frame.width = width;
    frame.height = height;
    frame.channels = 3;
    frame.format = efd::PixelFormat::RGB888;
    frame.isSynthetic = false;
    frame.timestampMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();

    size_t requiredSize = static_cast<size_t>(width * height * 3);
    frame.data.resize(requiredSize, 255);

    const uint8_t* nv21 = reinterpret_cast<const uint8_t*>(bytes);
    int frameSize = width * height;

    if (static_cast<size_t>(len) >= static_cast<size_t>(frameSize * 3 / 2)) {
        const uint8_t* yPlane = nv21;
        const uint8_t* vuPlane = nv21 + frameSize;

        for (int y = 0; y < height; ++y) {
            int yIndex = y * width;
            int uvIndex = (y / 2) * width;

            for (int x = 0; x < width; ++x) {
                int yVal = yPlane[yIndex + x] & 0xFF;
                int vVal = (vuPlane[uvIndex + (x & ~1)] & 0xFF) - 128;
                int uVal = (vuPlane[uvIndex + (x & ~1) + 1] & 0xFF) - 128;

                int r = static_cast<int>(static_cast<float>(yVal) + 1.402f * static_cast<float>(vVal));
                int g = static_cast<int>(static_cast<float>(yVal) - 0.344136f * static_cast<float>(uVal) - 0.714136f * static_cast<float>(vVal));
                int b = static_cast<int>(static_cast<float>(yVal) + 1.772f * static_cast<float>(uVal));

                size_t idx = (yIndex + x) * 3;
                frame.data[idx + 0] = static_cast<uint8_t>(std::clamp(r, 0, 255));
                frame.data[idx + 1] = static_cast<uint8_t>(std::clamp(g, 0, 255));
                frame.data[idx + 2] = static_cast<uint8_t>(std::clamp(b, 0, 255));
            }
        }
    } else {
        for (int i = 0; i < frameSize; ++i) {
            uint8_t yVal = nv21[i];
            size_t idx = i * 3;
            frame.data[idx + 0] = yVal;
            frame.data[idx + 1] = yVal;
            frame.data[idx + 2] = yVal;
        }
    }

    env->ReleaseByteArrayElements(frameData, bytes, JNI_ABORT);
    g_engine->getCameraService().onDriverFrame(frame);
}

JNIEXPORT jstring JNICALL
Java_com_efd_eyefatiguedetection_EfdNativeBridge_nativeGetTelemetryJson(JNIEnv* env, jobject /*thiz*/) {
    if (!g_engine) {
        return env->NewStringUTF("{\"error\":\"Engine not initialized\"}");
    }

    std::lock_guard<std::mutex> lock(g_alertMutex);
    std::lock_guard<std::mutex> tLock(g_telemetryMutex);

    // 獲取最新狀態與完整遙測指標
    auto state = g_lastTelemetry.systemState;
    float threshold = g_lastTelemetry.currentThreshold;
    auto eyeMetrics = g_lastTelemetry.eyeMetrics;
    auto complexityMetrics = g_lastTelemetry.complexityMetrics;
    bool hasFace = g_lastTelemetry.detection.hasFace;

    std::ostringstream oss;
    oss << std::fixed << std::setprecision(3);
    oss << "{"
        << "\"fatigueScore\":" << state.currentFatigueScore << ","
        << "\"fatigueLevel\":" << static_cast<int>(state.fatigueLevel) << ","
        << "\"cooldownState\":" << static_cast<int>(state.cooldownState) << ","
        << "\"cooldownRemaining\":" << state.cooldownRemainingSeconds << ","
        << "\"threshold\":" << threshold << ","
        << "\"hasFace\":" << (hasFace ? "true" : "false") << ","
        << "\"currentEar\":" << eyeMetrics.earAvg << ","
        << "\"perclos\":" << (eyeMetrics.perclos * 100.0f) << ","
        << "\"blinkRate\":" << eyeMetrics.blinkRatePerMin << ","
        << "\"complexityCI\":" << complexityMetrics.complexityIndex << ","
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

