#include <jni.h>
#include <algorithm>
#include <cmath>
#include <atomic>
#include <vector>
#include <chrono>
#include <ctime>
#include <string>
#include <memory>
#include <mutex>
#include <sstream>
#include <iomanip>
#include <android/log.h>

#include "efd/version.hpp"
#include "efd/types.hpp"
#include "engine/AsyncPipelineEngine.hpp"
#include "vision/ExternalLandmarks.hpp"

#define TAG "EFD_Native_JNI"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

// 引擎由前景服務擁有；相機執行緒、主執行緒與廣播接收器都可能同時存取，
// 因此以 shared_ptr + mutex 保護，避免 stopEngine() 時其他執行緒仍在使用已釋放的引擎
static std::shared_ptr<efd::AsyncPipelineEngine> g_engine;
static std::mutex g_engineMutex;

// 警報為單次脈衝：只由前景服務透過 nativeConsumeAlertJson 取出
static std::string g_lastAlertMessage;
static int g_lastAlertLevel = 0;
static float g_lastAlertScore = 0.0f;
static int64_t g_lastAlertTimeMs = 0;   // 提醒出現的 epoch 毫秒 (眼睛疲勞自評對應用)
static std::mutex g_alertMutex;

// 最新遙測 (不含影像像素，避免每幀複製整張影格)
static efd::EngineTelemetry g_lastTelemetry;
static std::mutex g_telemetryMutex;

// Java 端 MediaPipe 偵測：送進來的是特徵點而非影像 (只在相機執行緒呼叫)
static std::atomic<bool> g_externalDetector{false};
static efd::ExternalPresenceFilter g_presenceFilter;
static std::atomic<bool> g_presenceFilterResetRequested{false};  // 其他執行緒要求重置，由相機執行緒執行

// -----------------------------------------------------------------------------
// 自動畫面方向：部分手機 (例如三星 A71) 由背景服務開相機時，CameraX 回報的旋轉角與實際不符，
// 送進偵測程式的臉會橫躺 90°。因此輪流額外旋轉 0° / 90° / 270° 各 1.5 秒，
// 統計每個方向「通過眼睛檢查」的幀數，一輪結束後選分數明顯最高的方向鎖定。
// 只要某方向連續幾幀過關就鎖定的做法，會被橫躺畫面上偶發的誤判鎖錯 (三星 A71 實測鎖在 90°，
// 之後追蹤黏在下巴、脖子，量眼睛開合的位置不在眼睛上，閉眼率永遠 0%)。
// 每次啟動都重新比較一輪 (約 4.5 秒)；存檔的方向只決定從哪個方向開始試。
// 鎖定後若「在座但幾乎都沒通過眼睛檢查」或長時間沒有通過，代表方向可能錯了，重新比較。
// -----------------------------------------------------------------------------
static std::atomic<int> g_orientationOffset{0};      // 額外旋轉角 (0 / 90 / 270)
static std::atomic<bool> g_orientationLocked{false};
static std::atomic<int> g_lastReportedRotation{0};   // CameraX 回報的旋轉角 (診斷用)
static std::atomic<int> g_lastSensorWidth{0};
static std::atomic<int> g_lastSensorHeight{0};
static std::atomic<int64_t> g_orientationTrialStartMs{0};
static std::atomic<int64_t> g_lastFaceSeenMs{0};
static std::string g_orientationFilePath;
static constexpr int kOrientationCandidates[3] = {0, 90, 270};
static constexpr int64_t kOrientationTrialMs = 1500;     // 每個方向嘗試 1.5 秒
static constexpr int64_t kOrientationSettleMs = 400;     // 切換後 400 ms 內仍可能是舊方向的畫面，不計分
static constexpr int kOrientationMinScore = 3;           // 最佳方向至少 3 幀通過眼睛檢查
static constexpr int64_t kOrientationRelockMs = 30000;   // 鎖定後 30 秒都沒有通過眼睛檢查即重新比較
static constexpr int kOrientationAuditFrames = 900;      // 鎖定後每約 30 秒 (在座幀數) 檢查一次
static constexpr float kOrientationAuditMinRatio = 0.03f; // 在座幀中通過眼睛檢查不到 3% 視為方向可疑

static int64_t steadyNowMsJni() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

static void loadOrientationLock(const std::string& dir) {
    // v2：舊版「單幀即鎖定」可能存下錯誤方向 (三星 A71 實測被鎖在 90°)，改用新檔名讓舊鎖定失效
    g_orientationFilePath = dir.empty() ? std::string() : dir + "/efd_orientation_v2.txt";
    g_orientationOffset.store(0);
    g_orientationLocked.store(false);
    g_orientationTrialStartMs.store(0);
    g_lastFaceSeenMs.store(steadyNowMsJni());
    if (g_orientationFilePath.empty()) return;
    FILE* f = std::fopen(g_orientationFilePath.c_str(), "r");
    if (!f) return;
    int offset = -1;
    if (std::fscanf(f, "%d", &offset) == 1 && (offset == 0 || offset == 90 || offset == 270)) {
        g_orientationOffset.store(offset);
        LOGI("上次鎖定的畫面方向為額外旋轉 %d 度，從此方向開始比較", offset);
    }
    std::fclose(f);
}

// 以下狀態只在分析執行緒 (遙測回呼) 存取
static int g_trialScore[3] = {0, 0, 0};   // 本輪各方向通過眼睛檢查的幀數
static int g_trialsInCycle = 0;           // 本輪已試完的方向數
static int g_failedCycles = 0;            // 重新比較後連續沒有結果的輪數
static int g_previousLockedOffset = -1;   // 重新比較前鎖定的方向 (比較不出結果時退回)
static int g_auditFaceFrames = 0;         // 鎖定後的在座幀數
static int g_auditVerifiedFrames = 0;     // 其中通過眼睛檢查的幀數

static int orientationIndex(int offset) {
    for (int i = 0; i < 3; ++i) {
        if (kOrientationCandidates[i] == offset) return i;
    }
    return 0;
}

static void lockOrientation(int offset, const char* why) {
    g_orientationOffset.store(offset);
    g_orientationLocked.store(true);
    g_auditFaceFrames = 0;
    g_auditVerifiedFrames = 0;
    g_failedCycles = 0;
    g_lastFaceSeenMs.store(steadyNowMsJni());
    LOGI("鎖定畫面方向：額外旋轉 %d 度 (CameraX 回報 %d 度，%s)", offset, g_lastReportedRotation.load(), why);
    if (!g_orientationFilePath.empty()) {
        if (FILE* f = std::fopen(g_orientationFilePath.c_str(), "w")) {
            std::fprintf(f, "%d\n", offset);
            std::fclose(f);
        }
    }
}

static void startOrientationComparison(int64_t now, const char* why) {
    g_previousLockedOffset = g_orientationLocked.load() ? g_orientationOffset.load() : -1;
    g_orientationLocked.store(false);
    g_trialScore[0] = g_trialScore[1] = g_trialScore[2] = 0;
    g_trialsInCycle = 0;
    g_failedCycles = 0;
    g_orientationTrialStartMs.store(now);
    LOGI("重新比較畫面方向 (目前額外旋轉 %d 度)：%s", g_orientationOffset.load(), why);
}

// 每幀偵測結果：推進、鎖定或解除方向
// eyesVerified：這一幀是否通過眼睛檢查 (不含鎖定後靠追蹤維持的在座)；hasFace：目前是否判定在座
static void updateOrientationSearch(bool eyesVerified, bool hasFace) {
    const int64_t now = steadyNowMsJni();
    if (eyesVerified) g_lastFaceSeenMs.store(now);

    if (g_orientationLocked.load()) {
        if (hasFace) {
            g_auditFaceFrames++;
            if (eyesVerified) g_auditVerifiedFrames++;
            if (g_auditFaceFrames >= kOrientationAuditFrames) {
                const float ratio = static_cast<float>(g_auditVerifiedFrames) / g_auditFaceFrames;
                g_auditFaceFrames = 0;
                g_auditVerifiedFrames = 0;
                if (ratio < kOrientationAuditMinRatio) {
                    startOrientationComparison(now, "在座期間幾乎都沒有通過眼睛檢查");
                    return;
                }
            }
        }
        if (now - g_lastFaceSeenMs.load() >= kOrientationRelockMs) {
            g_lastFaceSeenMs.store(now);
            startOrientationComparison(now, "30 秒沒有通過眼睛檢查");
        }
        return;
    }

    int64_t trialStart = g_orientationTrialStartMs.load();
    if (trialStart == 0) {
        startOrientationComparison(now, "啟動");
        return;
    }
    const int idx = orientationIndex(g_orientationOffset.load());
    if (eyesVerified && now - trialStart >= kOrientationSettleMs) {
        g_trialScore[idx]++;
    }
    if (now - trialStart < kOrientationTrialMs) return;

    // 這個方向試完：換下一個方向；三個方向都試完就比較分數
    g_trialsInCycle++;
    if (g_trialsInCycle >= 3) {
        int best = 0;
        for (int i = 1; i < 3; ++i) {
            if (g_trialScore[i] > g_trialScore[best]) best = i;
        }
        int second = 0;
        for (int i = 0; i < 3; ++i) {
            if (i != best) second = std::max(second, g_trialScore[i]);
        }
        LOGI("方向比較分數：0°=%d 90°=%d 270°=%d", g_trialScore[0], g_trialScore[1], g_trialScore[2]);
        if (g_trialScore[best] >= kOrientationMinScore && g_trialScore[best] >= second * 2) {
            lockOrientation(kOrientationCandidates[best], "三個方向中通過眼睛檢查最多");
            return;
        }
        // 比較不出結果 (使用者不在、眼睛不易辨識)：重新比較兩輪後仍無結果就先退回原本的方向，
        // 避免在座但眼睛較難辨識時一直輪轉畫面、追蹤反覆中斷
        g_failedCycles++;
        if (g_previousLockedOffset >= 0 && g_failedCycles >= 2) {
            lockOrientation(g_previousLockedOffset, "比較不出更好的方向，沿用原方向");
            return;
        }
        g_trialScore[0] = g_trialScore[1] = g_trialScore[2] = 0;
        g_trialsInCycle = 0;
    }
    g_orientationOffset.store(kOrientationCandidates[(idx + 1) % 3]);
    g_orientationTrialStartMs.store(now);
}

static std::string jsonEscape(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        if (c == '"' || c == '\\') out += '\\';
        if (c == '\n') { out += "\\n"; continue; }
        out += c;
    }
    return out;
}

// 偵測診斷的 JSON 欄位 (遙測使用)
static void appendDiagnosticJson(std::ostringstream& oss, const efd::PresenceDiagnostic& d) {
    oss << "\"failStage\":\"" << jsonEscape(d.failStage) << "\","
        << "\"reason\":\"" << jsonEscape(d.unconfirmedReason) << "\","
        << "\"tracking\":" << (d.presenceByTracking ? "true" : "false") << ","
        << "\"skinBox\":[" << d.skinBoxX << "," << d.skinBoxY << "," << d.skinBoxW << "," << d.skinBoxH << "],"
        << "\"faceBox\":[" << d.faceBoxX << "," << d.faceBoxY << "," << d.faceBoxW << "," << d.faceBoxH << "],"
        << "\"avgSkinLum\":" << d.avgSkinLum << ","
        << "\"eyeDark\":[" << d.eyeDarkFraction[0] << "," << d.eyeDarkFraction[1] << "],"
        << "\"eyeContrast\":[" << d.eyeContrast[0] << "," << d.eyeContrast[1] << "],"
        << "\"eyeGradient\":[" << d.eyeGradient[0] << "," << d.eyeGradient[1] << "],"
        << "\"symmetry\":" << d.symmetryRatio << ","
        << "\"openDark\":" << d.eyeOpenDarkRatio << ","
        << "\"openBase\":" << d.eyeOpenBaseline << ","
        << "\"openness\":" << d.eyeOpenness << ","
        << "\"cameraRotation\":" << g_lastReportedRotation.load() << ","
        << "\"extraRotation\":" << g_orientationOffset.load() << ","
        << "\"orientationLocked\":" << (g_orientationLocked.load() ? "true" : "false") << ","
        << "\"sensorSize\":[" << g_lastSensorWidth.load() << "," << g_lastSensorHeight.load() << "]";
}

static std::shared_ptr<efd::AsyncPipelineEngine> currentEngine() {
    std::lock_guard<std::mutex> lock(g_engineMutex);
    return g_engine;
}

extern "C" {

JNIEXPORT jboolean JNICALL
Java_com_efd_eyefatiguedetection_EfdNativeBridge_nativeInitEngineWithPath(JNIEnv* env, jclass /*clazz*/, jstring storageDir);

JNIEXPORT jboolean JNICALL
Java_com_efd_eyefatiguedetection_EfdNativeBridge_nativeInitEngine(JNIEnv* env, jclass clazz) {
    return Java_com_efd_eyefatiguedetection_EfdNativeBridge_nativeInitEngineWithPath(env, clazz, nullptr);
}

JNIEXPORT jboolean JNICALL
Java_com_efd_eyefatiguedetection_EfdNativeBridge_nativeInitEngineWithPath(JNIEnv* env, jclass /*clazz*/, jstring storageDir) {
    std::lock_guard<std::mutex> lock(g_engineMutex);
    if (g_engine && g_engine->isRunning()) {
        LOGI("EFD AsyncPipelineEngine 已經初始化並常駐運行中。");
        return JNI_TRUE;
    }
    std::string pathStr;
    if (storageDir != nullptr) {
        const char* str = env->GetStringUTFChars(storageDir, nullptr);
        if (str) {
            pathStr = str;
            env->ReleaseStringUTFChars(storageDir, str);
        }
    }
    try {
        auto engine = std::make_shared<efd::AsyncPipelineEngine>(efd::PlatformType::Android);

        if (!pathStr.empty()) {
            // 沿用既有檔名 test_study_config.json，避免進行中的受試者 UUID 與天數被重置
            engine->setDataDirectory(pathStr, "test_study_config.json");
            LOGI("配置 C++ NDK 內部持久化儲存路徑: %s", pathStr.c_str());
        }
        loadOrientationLock(pathStr);
        g_presenceFilterResetRequested.store(true);
        // Android 的每日上傳由 Java EfdSyncManager 統一負責，關閉 C++ 端避免重複上傳
        engine->setAutoNightlySyncEnabled(false);
        // 冷卻與快篩非採樣時段關閉相機 (前景服務依 cameraSuspended 開關 CameraX，引擎不自行開啟模擬相機)
        engine->setCameraPowerSaving(true);
        // 持續沒看到臉時相機輪流關開探測 (A71 實測沒臉時相機系統服務仍佔約 86% 核心)
        engine->setAbsenceProbing(true);
        engine->setExternalCameraControl(true);

        engine->setAlertCallback([](efd::FatigueLevel level, float score, const std::string& msg) {
            std::lock_guard<std::mutex> alertLock(g_alertMutex);
            g_lastAlertLevel = static_cast<int>(level);
            g_lastAlertScore = score;
            g_lastAlertMessage = msg;
            g_lastAlertTimeMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            LOGI("Alert triggered: Level=%d, Score=%.1f, Msg=%s", static_cast<int>(level), score, msg.c_str());
        });

        engine->setTelemetryCallback([](const efd::EngineTelemetry& telemetry) {
            // 相機關閉期間沒有畫面，不可當成「這個方向找不到臉」；
            // MediaPipe 依兩眼連線自行轉正，不需要方向搜尋 (畫面也不再旋轉)
            if (!telemetry.latestFrame.isSynthetic && !telemetry.cameraSuspended && !g_externalDetector.load()) {
                const auto& pd = telemetry.presenceDiagnostic;
                updateOrientationSearch(pd.rawPersonInFrame && pd.rawFacingCamera && !pd.presenceByTracking,
                                        telemetry.detection.hasFace);
            }

#ifndef NDEBUG
            // debug 版逐幀紀錄 (adb logcat -s EFD_Frame)：分析眨眼、閉眼與低頭時的 EAR 分布；正式版不輸出
            if (telemetry.detection.landmarksMeasured && !telemetry.cameraSuspended) {
                // 頭部俯仰角：額頭 (10) 到下巴 (152) 的深度差 / 平面距離，與畫面旋轉無關；正值代表低頭
                float pitchDeg = 0.0f;
                const auto& lm = telemetry.detection.landmarks;
                if (lm.size() > 152) {
                    const float planar = std::hypot(lm[152].x - lm[10].x, lm[152].y - lm[10].y);
                    pitchDeg = std::atan2(lm[152].z - lm[10].z, planar) * 57.2958f;
                }
                __android_log_print(ANDROID_LOG_DEBUG, "EFD_Frame",
                                    "face=%d ear=%.4f thr=%.4f bthr=%.4f closed=%d blink=%.3f,%.3f look=%.3f pitch=%.1f perclos=%.3f",
                                    telemetry.detection.hasFace ? 1 : 0, telemetry.eyeMetrics.earAvg, telemetry.currentThreshold,
                                    telemetry.blinkThreshold, telemetry.eyeMetrics.isEyeClosed ? 1 : 0,
                                    telemetry.detection.eyeBlinkLeft, telemetry.detection.eyeBlinkRight,
                                    telemetry.detection.eyeLookDown, pitchDeg, telemetry.eyeMetrics.perclos);
            }
#endif

            efd::EngineTelemetry slim;
            slim.latestFrame.timestampMs = telemetry.latestFrame.timestampMs;
            slim.latestFrame.width = telemetry.latestFrame.width;
            slim.latestFrame.height = telemetry.latestFrame.height;
            slim.latestFrame.isSynthetic = telemetry.latestFrame.isSynthetic;
            slim.presenceDiagnostic = telemetry.presenceDiagnostic;
            slim.detection.hasFace = telemetry.detection.hasFace;
            slim.detection.landmarksMeasured = telemetry.detection.landmarksMeasured;
            slim.detection.eyeBlinkLeft = telemetry.detection.eyeBlinkLeft;
            slim.detection.eyeBlinkRight = telemetry.detection.eyeBlinkRight;
            slim.cameraSuspended = telemetry.cameraSuspended;
            slim.eyeMetrics = telemetry.eyeMetrics;
            slim.complexityMetrics = telemetry.complexityMetrics;
            slim.systemState = telemetry.systemState;
            slim.currentThreshold = telemetry.currentThreshold;
            slim.measuredFps = telemetry.measuredFps;
            slim.totalFramesProcessed = telemetry.totalFramesProcessed;
            std::lock_guard<std::mutex> telemetryLock(g_telemetryMutex);
            g_lastTelemetry = std::move(slim);
        });

        engine->start();
        g_engine = engine;
        LOGI("EFD AsyncPipelineEngine 成功於 Android 啟動且資料持久化就緒。");
        return JNI_TRUE;
    } catch (const std::exception& e) {
        LOGE("Failed to init EFD Engine with path: %s", e.what());
        return JNI_FALSE;
    }
}

JNIEXPORT jboolean JNICALL
Java_com_efd_eyefatiguedetection_EfdNativeBridge_nativeIsEngineRunning(JNIEnv* /*env*/, jclass /*clazz*/) {
    auto engine = currentEngine();
    return (engine && engine->isRunning()) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL
Java_com_efd_eyefatiguedetection_EfdNativeBridge_nativeBeginCalibration(JNIEnv* /*env*/, jclass /*clazz*/) {
    if (auto engine = currentEngine()) {
        engine->beginCalibration();
    }
}

JNIEXPORT jboolean JNICALL
Java_com_efd_eyefatiguedetection_EfdNativeBridge_nativeFinishCalibration(JNIEnv* /*env*/, jclass /*clazz*/) {
    auto engine = currentEngine();
    if (!engine) return JNI_FALSE;
    bool ok = engine->finishCalibration();
    LOGI("Calibration %s: threshold=%.3f validRatio=%.2f", ok ? "OK" : "FAILED",
         engine->getAdaptiveBaseline().getCurrentThreshold(), engine->getLastCalibrationValidRatio());
    return ok ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL
Java_com_efd_eyefatiguedetection_EfdNativeBridge_nativeHasSavedCalibration(JNIEnv* /*env*/, jclass /*clazz*/) {
    auto engine = currentEngine();
    return (engine && engine->hasSavedCalibration()) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL
Java_com_efd_eyefatiguedetection_EfdNativeBridge_nativeSetSimulatedEyeOpenness(JNIEnv* /*env*/, jobject /*thiz*/, jfloat openness) {
    if (auto engine = currentEngine()) {
        engine->setSimulatedEyeOpenness(openness);
    }
}

JNIEXPORT void JNICALL
Java_com_efd_eyefatiguedetection_EfdNativeBridge_nativeSendFrame(JNIEnv* env, jobject /*thiz*/, jbyteArray frameData, jint width, jint height, jint rotation) {
    auto engine = currentEngine();
    if (!engine || !frameData || width <= 0 || height <= 0) return;

    jsize len = env->GetArrayLength(frameData);
    if (len <= 0) return;

    jbyte* bytes = env->GetByteArrayElements(frameData, nullptr);
    if (!bytes) return;

    // 接收實體相機影格，關閉背景 Synthetic 虛擬驅動
    engine->getCameraService().stopSyntheticIfRunning();

    const int reported = (rotation % 360 + 360) % 360;
    g_lastReportedRotation.store(reported);
    g_lastSensorWidth.store(width);
    g_lastSensorHeight.store(height);
    // 實際旋轉 = CameraX 回報角度 + 自動方向搜尋的額外角度
    int rot = (reported + g_orientationOffset.load()) % 360;
    // 轉正後的完整尺寸
    const int fullW = (rot == 90 || rot == 270) ? height : width;
    const int fullH = (rot == 90 || rot == 270) ? width : height;
    // 偵測參數以約 480 寬 (iPhone 480x640、Windows 640) 調校；部分手機 (例如三星) 會給 1088x1088 等
    // 高解析畫面，等比例縮到寬 480 以內，避免臉部尺寸、步進與膚色網格的比例失準
    constexpr int kTargetWidth = 480;
    const int outW = (fullW > kTargetWidth) ? kTargetWidth : fullW;
    const int outH = (fullW > kTargetWidth) ? std::max(1, fullH * kTargetWidth / fullW) : fullH;

    efd::RawFrame frame;
    frame.width = outW;
    frame.height = outH;
    frame.channels = 3;
    frame.format = efd::PixelFormat::RGB888;
    frame.isSynthetic = false;
    frame.timestampMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();

    size_t requiredSize = static_cast<size_t>(outW * outH * 3);
    frame.data.resize(requiredSize, 255);

    const uint8_t* nv21 = reinterpret_cast<const uint8_t*>(bytes);
    int frameSize = width * height;

    if (static_cast<size_t>(len) >= static_cast<size_t>(frameSize * 3 / 2)) {
        const uint8_t* yPlane = nv21;
        const uint8_t* vuPlane = nv21 + frameSize;

        // 逐一計算輸出像素：先換算到轉正後的完整座標 (縮放)，再反推回感測器座標 (旋轉)
        for (int oy = 0; oy < outH; ++oy) {
            const int ry = static_cast<int>(static_cast<long long>(oy) * fullH / outH);
            for (int ox = 0; ox < outW; ++ox) {
                const int rx = static_cast<int>(static_cast<long long>(ox) * fullW / outW);
                int x = rx, y = ry;
                if (rot == 90) {
                    x = ry;
                    y = height - 1 - rx;
                } else if (rot == 180) {
                    x = width - 1 - rx;
                    y = height - 1 - ry;
                } else if (rot == 270) {
                    x = width - 1 - ry;
                    y = rx;
                }
                if (x < 0 || x >= width || y < 0 || y >= height) continue;

                int yVal = yPlane[y * width + x] & 0xFF;
                const int uvIndex = (y / 2) * width + (x & ~1);
                int vVal = (vuPlane[uvIndex] & 0xFF) - 128;
                int uVal = (vuPlane[uvIndex + 1] & 0xFF) - 128;

                int r = static_cast<int>(static_cast<float>(yVal) + 1.402f * static_cast<float>(vVal));
                int g = static_cast<int>(static_cast<float>(yVal) - 0.344136f * static_cast<float>(uVal) - 0.714136f * static_cast<float>(vVal));
                int b = static_cast<int>(static_cast<float>(yVal) + 1.772f * static_cast<float>(uVal));

                size_t outIdx = (static_cast<size_t>(oy) * outW + ox) * 3;
                frame.data[outIdx + 0] = static_cast<uint8_t>(std::clamp(r, 0, 255));
                frame.data[outIdx + 1] = static_cast<uint8_t>(std::clamp(g, 0, 255));
                frame.data[outIdx + 2] = static_cast<uint8_t>(std::clamp(b, 0, 255));
            }
        }
    }

    env->ReleaseByteArrayElements(frameData, bytes, JNI_ABORT);
    engine->getCameraService().onDriverFrame(frame);
}

// Java 端 MediaPipe 已完成偵測：送入 0~1 正規化特徵點 (沒有臉時為 null)，由 C++ 換算成像素並交給訊號執行緒
// width / height 為 MediaPipe 實際分析的畫面尺寸；影像本身不送進 C++
JNIEXPORT void JNICALL
Java_com_efd_eyefatiguedetection_EfdNativeBridge_nativeSendLandmarks(JNIEnv* env, jclass /*clazz*/, jfloatArray normalizedXyz,
                                                                    jint width, jint height, jint rotation,
                                                                    jfloat blinkLeft, jfloat blinkRight, jfloat lookDown) {
    auto engine = currentEngine();
    if (!engine || width <= 0 || height <= 0) return;

    g_externalDetector.store(true);
    engine->getCameraService().stopSyntheticIfRunning();
    g_lastReportedRotation.store((rotation % 360 + 360) % 360);
    g_lastSensorWidth.store(width);
    g_lastSensorHeight.store(height);

    std::vector<float> xyz;
    if (normalizedXyz) {
        const jsize len = env->GetArrayLength(normalizedXyz);
        xyz.resize(static_cast<size_t>(len));
        env->GetFloatArrayRegion(normalizedXyz, 0, len, xyz.data());
    }
    efd::LandmarkDetectionResult detection = efd::buildExternalDetection(
        xyz.empty() ? nullptr : xyz.data(), xyz.size() / 3, width, height, blinkLeft, blinkRight);
    detection.eyeLookDown = lookDown;

    const int64_t now = steadyNowMsJni();
    if (g_presenceFilterResetRequested.exchange(false)) g_presenceFilter.reset();
    if (!g_presenceFilter.shouldSubmit(detection.hasFace, now)) return;

    efd::PresenceDiagnostic diagnostic = efd::buildExternalDiagnostic(detection, width, height);
    efd::RawFrame frameInfo;
    frameInfo.width = width;
    frameInfo.height = height;
    frameInfo.channels = 3;
    frameInfo.format = efd::PixelFormat::RGB888;
    frameInfo.isSynthetic = false;
    frameInfo.timestampMs = now;
    engine->submitExternalDetection(std::move(frameInfo), std::move(detection), std::move(diagnostic));
}

// 自動畫面方向搜尋目前要額外旋轉的角度 (0 / 90 / 270)，MediaPipe 路徑在 Java 端旋轉畫面時使用
JNIEXPORT jint JNICALL
Java_com_efd_eyefatiguedetection_EfdNativeBridge_nativeGetExtraRotation(JNIEnv* /*env*/, jclass /*clazz*/) {
    return static_cast<jint>(g_orientationOffset.load());
}

// 螢幕關閉：暫停引擎 (期間視同離座，滿 5 分鐘清除冷卻)；螢幕開啟後恢復
JNIEXPORT void JNICALL
Java_com_efd_eyefatiguedetection_EfdNativeBridge_nativePauseEngine(JNIEnv* /*env*/, jclass /*clazz*/) {
    if (auto engine = currentEngine()) {
        engine->pause();
        LOGI("螢幕關閉，暫停偵測引擎");
    }
}

JNIEXPORT void JNICALL
Java_com_efd_eyefatiguedetection_EfdNativeBridge_nativeResumeEngine(JNIEnv* /*env*/, jclass /*clazz*/) {
    if (auto engine = currentEngine()) {
        g_presenceFilterResetRequested.store(true);
        engine->resume();
        LOGI("螢幕開啟，恢復偵測引擎");
    }
}

// 交叉設計組別 ("A" / "B")；選定後鎖定，回傳 false 代表無效或已選了另一組
JNIEXPORT jboolean JNICALL
Java_com_efd_eyefatiguedetection_EfdNativeBridge_nativeSetStudyGroup(JNIEnv* env, jclass /*clazz*/, jstring group) {
    auto engine = currentEngine();
    if (!engine || !group) return JNI_FALSE;
    const char* str = env->GetStringUTFChars(group, nullptr);
    if (!str) return JNI_FALSE;
    const bool ok = engine->getStudyTracker().setStudyGroup(str);
    env->ReleaseStringUTFChars(group, str);
    return ok ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jstring JNICALL
Java_com_efd_eyefatiguedetection_EfdNativeBridge_nativeGetStudyGroup(JNIEnv* env, jclass /*clazz*/) {
    auto engine = currentEngine();
    return env->NewStringUTF(engine ? engine->getStudyTracker().getStudyGroup().c_str() : "");
}

// 提醒出現時的眼睛疲勞自評 (0~10；-1 = 略過或逾時)
JNIEXPORT void JNICALL
Java_com_efd_eyefatiguedetection_EfdNativeBridge_nativeRecordEyeFatigueResponse(JNIEnv* /*env*/, jclass /*clazz*/,
                                                                                jlong alertTimeMs, jint eyeFatigue,
                                                                                jfloat responseSec) {
    if (auto engine = currentEngine()) {
        engine->recordEyeFatigueResponse(static_cast<int64_t>(alertTimeMs), eyeFatigue, responseSec);
    }
}

// 相機是否因冷卻或快篩非採樣時段而應關閉
JNIEXPORT jboolean JNICALL
Java_com_efd_eyefatiguedetection_EfdNativeBridge_nativeIsCameraSuspended(JNIEnv* /*env*/, jclass /*clazz*/) {
    auto engine = currentEngine();
    return (engine && engine->isCameraSuspended()) ? JNI_TRUE : JNI_FALSE;
}

// 讀取最新狀態 (不會清除警報，Activity 與 Service 可同時呼叫)
JNIEXPORT jstring JNICALL
Java_com_efd_eyefatiguedetection_EfdNativeBridge_nativeGetTelemetryJson(JNIEnv* env, jobject /*thiz*/) {
    auto engine = currentEngine();
    if (!engine || !engine->isRunning()) {
        return env->NewStringUTF("{\"engineRunning\":false}");
    }

    efd::EngineTelemetry telemetry;
    {
        std::lock_guard<std::mutex> tLock(g_telemetryMutex);
        telemetry = g_lastTelemetry;
    }
    const auto& state = telemetry.systemState;
    const auto& eyeMetrics = telemetry.eyeMetrics;

    // 影格超時檢查：若超過 1.2 秒未收到新影格，強制視為離座
    auto nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    bool isStale = (telemetry.latestFrame.timestampMs > 0) &&
                   ((nowMs - telemetry.latestFrame.timestampMs) > 1200);
    bool hasFace = telemetry.detection.hasFace && !isStale;

    std::ostringstream oss;
    oss << std::fixed << std::setprecision(3);
    oss << "{"
        << "\"engineRunning\":true,"
        << "\"fatigueScore\":" << state.smoothedFatigueScore << ","
        << "\"fatigueLevel\":" << static_cast<int>(state.fatigueLevel) << ","
        << "\"cooldownState\":" << static_cast<int>(state.cooldownState) << ","
        << "\"cooldownRemaining\":" << state.cooldownRemainingSeconds << ","
        << "\"screeningRemaining\":" << state.screeningRemainingSeconds << ","
        << "\"threshold\":" << telemetry.currentThreshold << ","
        << "\"hasFace\":" << (hasFace ? "true" : "false") << ","
        << "\"currentEar\":" << eyeMetrics.earAvg << ","
        << "\"perclos\":" << (eyeMetrics.perclos * 100.0f) << ","
        << "\"blinkRate\":" << eyeMetrics.blinkRatePerMin << ","
        << "\"complexityCI\":" << telemetry.complexityMetrics.complexityIndex << ","
        << "\"measuredFps\":" << telemetry.measuredFps << ","
        << "\"studyDay\":" << state.studyDay << ","
        << "\"isStudyLocked\":" << (state.isStudyLocked ? "true" : "false") << ","
        << "\"stale\":" << (isStale ? "true" : "false") << ","
        << "\"synthetic\":" << (telemetry.latestFrame.isSynthetic ? "true" : "false") << ","
        << "\"frameW\":" << telemetry.latestFrame.width << ","
        << "\"frameH\":" << telemetry.latestFrame.height << ","
        << "\"cameraSuspended\":" << (telemetry.cameraSuspended ? "true" : "false") << ","
        << "\"studyGroup\":\"" << engine->getStudyTracker().getStudyGroup() << "\","
        << "\"alertsEnabled\":" << (engine->getStudyTracker().areAlertsEnabledToday() ? "true" : "false") << ","
        << "\"detector\":\"" << (g_externalDetector.load() ? "mediapipe" : "heuristic") << "\","
        << "\"eyeBlink\":[" << telemetry.detection.eyeBlinkLeft << "," << telemetry.detection.eyeBlinkRight << "],";
    size_t calibSamples = 0;
    size_t calibFrames = 0;
    engine->getCalibrationProgress(calibSamples, calibFrames);
    // 最新一幀距今的毫秒數：stale 時可分辨是相機停止送畫面還是分析過慢
    oss << "\"frameAgeMs\":" << (telemetry.latestFrame.timestampMs > 0 ? nowMs - telemetry.latestFrame.timestampMs : -1) << ","
        << "\"calibSamples\":" << calibSamples << ","
        << "\"calibFrames\":" << calibFrames << ",";
    appendDiagnosticJson(oss, telemetry.presenceDiagnostic);
    oss << "}";

    return env->NewStringUTF(oss.str().c_str());
}

// 取出並清除待發送的警報 (只由前景服務呼叫，確保每次警報只通知一次)
JNIEXPORT jstring JNICALL
Java_com_efd_eyefatiguedetection_EfdNativeBridge_nativeConsumeAlertJson(JNIEnv* env, jclass /*clazz*/) {
    int level = 0;
    float score = 0.0f;
    int64_t timeMs = 0;
    std::string msg;
    {
        std::lock_guard<std::mutex> lock(g_alertMutex);
        level = g_lastAlertLevel;
        score = g_lastAlertScore;
        msg = g_lastAlertMessage;
        timeMs = g_lastAlertTimeMs;
        g_lastAlertTimeMs = 0;
        g_lastAlertLevel = 0;
        g_lastAlertScore = 0.0f;
        g_lastAlertMessage.clear();
    }
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(1)
        << "{\"level\":" << level << ",\"score\":" << score << ",\"timeMs\":" << timeMs
        << ",\"message\":\"" << msg << "\"}";
    return env->NewStringUTF(oss.str().c_str());
}

JNIEXPORT jstring JNICALL
Java_com_efd_eyefatiguedetection_EfdNativeBridge_nativeGetSubjectUuid(JNIEnv* env, jobject /*thiz*/) {
    auto engine = currentEngine();
    if (!engine) {
        return env->NewStringUTF("");
    }
    std::string uuid = engine->getStudyTracker().getSubjectUuid();
    return env->NewStringUTF(uuid.c_str());
}

JNIEXPORT jint JNICALL
Java_com_efd_eyefatiguedetection_EfdNativeBridge_nativeGetCurrentDay(JNIEnv* /*env*/, jobject /*thiz*/) {
    auto engine = currentEngine();
    if (!engine) {
        return 0;
    }
    return static_cast<jint>(engine->getStudyTracker().getCurrentDay());
}

JNIEXPORT jstring JNICALL
Java_com_efd_eyefatiguedetection_EfdNativeBridge_nativeGetCloudSyncPayloadJson(JNIEnv* env, jobject /*thiz*/) {
    auto engine = currentEngine();
    if (!engine) {
        return env->NewStringUTF("");
    }

    // 與 Windows / iOS / macOS 共用同一份 JSON 產生邏輯：指標、眨眼率、校驗碼都由實際紀錄計算，
    // 無紀錄時指標為 null；憑證只在填寫後測問卷後才有，否則為空字串
    auto& tracker = engine->getStudyTracker();
    efd::PayloadHeader header;
    header.subjectUuid = tracker.getSubjectUuid();
    header.studyDay = tracker.getCurrentDay();
    header.studyStatus = efd::StudyWorkflowTracker::statusLabel(tracker.getStatus());
    header.unlockToken = tracker.getUnlockToken();

    auto& db = engine->getDatabaseService();
    db.flush();
    auto records = db.getLatestRecords(db.getRecordCount());
    header.extraJsonFields = engine->buildStudyLogJson(header.subjectUuid);
    std::string json = efd::NetworkSyncWorker::buildPayloadJson(header, records, false);
    return env->NewStringUTF(json.c_str());
}

JNIEXPORT void JNICALL
Java_com_efd_eyefatiguedetection_EfdNativeBridge_nativeSetSensitivity(JNIEnv* /*env*/, jclass /*clazz*/, jint level) {
    auto engine = currentEngine();
    if (engine) {
        engine->setSensitivity(static_cast<int>(level));
    }
}

JNIEXPORT jint JNICALL
Java_com_efd_eyefatiguedetection_EfdNativeBridge_nativeGetSensitivity(JNIEnv* /*env*/, jclass /*clazz*/) {
    auto engine = currentEngine();
    if (!engine) {
        return -1;
    }
    return static_cast<jint>(engine->getSensitivity());
}

JNIEXPORT jstring JNICALL
Java_com_efd_eyefatiguedetection_EfdNativeBridge_nativeGetUnlockToken(JNIEnv* env, jclass /*clazz*/) {
    auto engine = currentEngine();
    std::string token = engine ? engine->getStudyTracker().getUnlockToken() : std::string();
    return env->NewStringUTF(token.c_str());
}

JNIEXPORT jstring JNICALL
Java_com_efd_eyefatiguedetection_EfdNativeBridge_nativeSubmitQuestionnaire(JNIEnv* env, jobject /*thiz*/, jstring surveyData) {
    auto engine = currentEngine();
    if (!engine) {
        return env->NewStringUTF("ERROR");
    }

    const char* str = env->GetStringUTFChars(surveyData, nullptr);
    std::string data(str ? str : "");
    if (str) env->ReleaseStringUTFChars(surveyData, str);

    engine->getStudyTracker().submitQuestionnaire(data);
    std::string token = engine->getStudyTracker().getUnlockToken();
    return env->NewStringUTF(token.c_str());
}

JNIEXPORT void JNICALL
Java_com_efd_eyefatiguedetection_EfdNativeBridge_nativeTriggerTestAlert(JNIEnv* /*env*/, jobject /*thiz*/) {
    std::lock_guard<std::mutex> lock(g_alertMutex);
    g_lastAlertLevel = 2; // SevereWarning
    g_lastAlertScore = 0.0f;
    // 與真實疲勞提醒文字區隔，避免使用者誤以為未就坐時仍跳出疲勞警報
    g_lastAlertMessage = "【測試通知】背景通知運作正常，這不是疲勞警報";
}

JNIEXPORT void JNICALL
Java_com_efd_eyefatiguedetection_EfdNativeBridge_nativeStopEngine(JNIEnv* /*env*/, jobject /*thiz*/) {
    std::shared_ptr<efd::AsyncPipelineEngine> engine;
    {
        std::lock_guard<std::mutex> lock(g_engineMutex);
        engine.swap(g_engine);
    }
    if (engine) {
        // 其他執行緒若仍持有 shared_ptr，引擎會在最後一個使用者釋放後才解構
        engine->stop();
        LOGI("EFD AsyncPipelineEngine stopped.");
    }
}

} // extern "C"
