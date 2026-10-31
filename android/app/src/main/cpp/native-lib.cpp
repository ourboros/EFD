#include <jni.h>
#include <algorithm>
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
static std::mutex g_alertMutex;

// 最新遙測 (不含影像像素，避免每幀複製整張影格)
static efd::EngineTelemetry g_lastTelemetry;
static std::mutex g_telemetryMutex;

// 診斷擷取：使用者按下「匯出診斷畫面」時，下一幀 C++ 實際分析的 RGB 影像與偵測數值
static std::atomic<bool> g_debugCaptureRequested{false};
static std::vector<uint8_t> g_debugRgb;
static std::string g_debugInfoJson;
static std::mutex g_debugMutex;

// -----------------------------------------------------------------------------
// 自動畫面方向：部分手機 (例如三星 A71) 由背景服務開相機時，CameraX 回報的旋轉角與實際不符，
// 送進偵測程式的臉會橫躺 90°。尚未鎖定方向時，連續一段時間找不到臉就輪流額外旋轉
// 0° / 90° / 270°；在某個方向第一次偵測到臉 (需通過眼睛檢查) 即鎖定並存檔，之後不再嘗試。
// -----------------------------------------------------------------------------
static std::atomic<int> g_orientationOffset{0};      // 額外旋轉角 (0 / 90 / 270)
static std::atomic<bool> g_orientationLocked{false};
static std::atomic<int> g_lastReportedRotation{0};   // CameraX 回報的旋轉角 (診斷用)
static std::atomic<int> g_lastSensorWidth{0};
static std::atomic<int> g_lastSensorHeight{0};
static std::atomic<int64_t> g_orientationTrialStartMs{0};
static std::string g_orientationFilePath;
static constexpr int kOrientationCandidates[3] = {0, 90, 270};
static constexpr int64_t kOrientationTrialMs = 1500;  // 每個方向嘗試 1.5 秒

static int64_t steadyNowMsJni() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

static void loadOrientationLock(const std::string& dir) {
    g_orientationFilePath = dir.empty() ? std::string() : dir + "/efd_orientation.txt";
    g_orientationOffset.store(0);
    g_orientationLocked.store(false);
    if (g_orientationFilePath.empty()) return;
    FILE* f = std::fopen(g_orientationFilePath.c_str(), "r");
    if (!f) return;
    int offset = -1;
    if (std::fscanf(f, "%d", &offset) == 1 && (offset == 0 || offset == 90 || offset == 270)) {
        g_orientationOffset.store(offset);
        g_orientationLocked.store(true);
        LOGI("沿用已鎖定的畫面方向：額外旋轉 %d 度", offset);
    }
    std::fclose(f);
}

// 每幀偵測結果 (遙測回呼，於分析執行緒)：推進或鎖定方向
static void updateOrientationSearch(bool rawFaceFound) {
    if (g_orientationLocked.load()) return;
    // 剛切換方向的 400 ms 內，管線中可能仍是舊方向的畫面，結果不採用，避免鎖錯方向
    const int64_t trialStart = g_orientationTrialStartMs.load();
    if (rawFaceFound && trialStart != 0 && steadyNowMsJni() - trialStart < 400) return;
    if (rawFaceFound) {
        g_orientationLocked.store(true);
        const int offset = g_orientationOffset.load();
        LOGI("偵測到臉部，鎖定畫面方向：額外旋轉 %d 度 (CameraX 回報 %d 度)", offset, g_lastReportedRotation.load());
        if (!g_orientationFilePath.empty()) {
            if (FILE* f = std::fopen(g_orientationFilePath.c_str(), "w")) {
                std::fprintf(f, "%d\n", offset);
                std::fclose(f);
            }
        }
        return;
    }
    const int64_t now = steadyNowMsJni();
    int64_t start = g_orientationTrialStartMs.load();
    if (start == 0) {
        g_orientationTrialStartMs.store(now);
        return;
    }
    if (now - start >= kOrientationTrialMs) {
        int idx = 0;
        for (int i = 0; i < 3; ++i) {
            if (kOrientationCandidates[i] == g_orientationOffset.load()) idx = i;
        }
        g_orientationOffset.store(kOrientationCandidates[(idx + 1) % 3]);
        g_orientationTrialStartMs.store(now);
    }
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

// 偵測診斷的 JSON 欄位 (遙測與診斷擷取共用)
static void appendDiagnosticJson(std::ostringstream& oss, const efd::FaceLandmarker::PresenceDiagnostic& d) {
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
        // Android 的每日上傳由 Java EfdSyncManager 統一負責，關閉 C++ 端避免重複上傳
        engine->setAutoNightlySyncEnabled(false);

        engine->setAlertCallback([](efd::FatigueLevel level, float score, const std::string& msg) {
            std::lock_guard<std::mutex> alertLock(g_alertMutex);
            g_lastAlertLevel = static_cast<int>(level);
            g_lastAlertScore = score;
            g_lastAlertMessage = msg;
            LOGI("Alert triggered: Level=%d, Score=%.1f, Msg=%s", static_cast<int>(level), score, msg.c_str());
        });

        engine->setTelemetryCallback([](const efd::EngineTelemetry& telemetry) {
            if (!telemetry.latestFrame.isSynthetic) {
                updateOrientationSearch(telemetry.presenceDiagnostic.rawPersonInFrame &&
                                        telemetry.presenceDiagnostic.rawFacingCamera);
            }

            // 診斷擷取：複製 C++ 實際分析的這一幀 (RGB888) 與偵測數值
            if (g_debugCaptureRequested.exchange(false) && telemetry.latestFrame.isValid()) {
                std::ostringstream info;
                info << std::fixed << std::setprecision(3)
                     << "{\"width\":" << telemetry.latestFrame.width
                     << ",\"height\":" << telemetry.latestFrame.height
                     << ",\"synthetic\":" << (telemetry.latestFrame.isSynthetic ? "true" : "false")
                     << ",\"hasFace\":" << (telemetry.detection.hasFace ? "true" : "false")
                     << ",\"rawFace\":" << (telemetry.presenceDiagnostic.rawPersonInFrame && telemetry.presenceDiagnostic.rawFacingCamera ? "true" : "false")
                     << ",\"fps\":" << telemetry.measuredFps << ",";
                appendDiagnosticJson(info, telemetry.presenceDiagnostic);
                info << "}";
                std::lock_guard<std::mutex> debugLock(g_debugMutex);
                const size_t bytes = static_cast<size_t>(telemetry.latestFrame.width) * telemetry.latestFrame.height * 3;
                g_debugRgb.assign(telemetry.latestFrame.data.begin(), telemetry.latestFrame.data.begin() + bytes);
                g_debugInfoJson = info.str();
            }

            efd::EngineTelemetry slim;
            slim.latestFrame.timestampMs = telemetry.latestFrame.timestampMs;
            slim.latestFrame.width = telemetry.latestFrame.width;
            slim.latestFrame.height = telemetry.latestFrame.height;
            slim.latestFrame.isSynthetic = telemetry.latestFrame.isSynthetic;
            slim.presenceDiagnostic = telemetry.presenceDiagnostic;
            slim.detection.hasFace = telemetry.detection.hasFace;
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
        << "\"frameH\":" << telemetry.latestFrame.height << ",";
    appendDiagnosticJson(oss, telemetry.presenceDiagnostic);
    oss << "}";

    return env->NewStringUTF(oss.str().c_str());
}

// 取出並清除待發送的警報 (只由前景服務呼叫，確保每次警報只通知一次)
JNIEXPORT jstring JNICALL
Java_com_efd_eyefatiguedetection_EfdNativeBridge_nativeConsumeAlertJson(JNIEnv* env, jclass /*clazz*/) {
    int level = 0;
    float score = 0.0f;
    std::string msg;
    {
        std::lock_guard<std::mutex> lock(g_alertMutex);
        level = g_lastAlertLevel;
        score = g_lastAlertScore;
        msg = g_lastAlertMessage;
        g_lastAlertLevel = 0;
        g_lastAlertScore = 0.0f;
        g_lastAlertMessage.clear();
    }
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(1)
        << "{\"level\":" << level << ",\"score\":" << score << ",\"message\":\"" << msg << "\"}";
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
Java_com_efd_eyefatiguedetection_EfdNativeBridge_nativeRequestDebugCapture(JNIEnv* /*env*/, jclass /*clazz*/) {
    {
        std::lock_guard<std::mutex> lock(g_debugMutex);
        g_debugRgb.clear();
        g_debugInfoJson.clear();
    }
    g_debugCaptureRequested.store(true);
}

// 取出診斷擷取的偵測數值 (JSON)；尚未擷取到時回傳空字串
JNIEXPORT jstring JNICALL
Java_com_efd_eyefatiguedetection_EfdNativeBridge_nativeTakeDebugCaptureInfo(JNIEnv* env, jclass /*clazz*/) {
    std::lock_guard<std::mutex> lock(g_debugMutex);
    return env->NewStringUTF(g_debugInfoJson.c_str());
}

// 取出診斷擷取的 RGB888 影像並清除；尚未擷取到時回傳 null
JNIEXPORT jbyteArray JNICALL
Java_com_efd_eyefatiguedetection_EfdNativeBridge_nativeTakeDebugCaptureRgb(JNIEnv* env, jclass /*clazz*/) {
    std::lock_guard<std::mutex> lock(g_debugMutex);
    if (g_debugRgb.empty()) return nullptr;
    jbyteArray array = env->NewByteArray(static_cast<jsize>(g_debugRgb.size()));
    if (array) {
        env->SetByteArrayRegion(array, 0, static_cast<jsize>(g_debugRgb.size()),
                                reinterpret_cast<const jbyte*>(g_debugRgb.data()));
    }
    g_debugRgb.clear();
    g_debugInfoJson.clear();
    return array;
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
