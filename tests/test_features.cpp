// 測試一律啟用 assert：Release 建置定義 NDEBUG 會整行移除 assert 內的呼叫 (例如讀圖、偵測、餵資料)，測試會空轉通過
#ifdef NDEBUG
#undef NDEBUG
#endif

#include <iostream>
#include <vector>
#include <cmath>
#include <random>
#include <cassert>
#include <chrono>
#include <thread>
#include <atomic>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <tuple>
#include <limits>

#include "efd/types.hpp"
#include "analysis/FeatureExtractor.hpp"
#include "analysis/EmdCalculator.hpp"
#include "analysis/MseCalculator.hpp"
#include "analysis/AdaptiveBaseline.hpp"
#include "state/FatigueStateMachine.hpp"
#include "vision/RawFrame.hpp"
#include "vision/FaceLandmarker.hpp"
#include "vision/ExternalLandmarks.hpp"
#include "vision/OnnxFaceLandmarker.hpp"
#include "analysis/RollingOpenBaseline.hpp"
#include "storage/StudyDataLog.hpp"
#include "vision/CameraService.hpp"
#include "vision/VisionPipeline.hpp"
#include "vision/drivers/SyntheticCameraDriver.hpp"
#include "platform/PlatformLifecycleAdapter.hpp"
#include "storage/DatabaseService.hpp"
#include "study/StudyWorkflowTracker.hpp"
#include "network/NetworkSyncWorker.hpp"
#include "platform/windows/SystemTrayManager.hpp"
#include "ui/FatigueFloatingIndicator.hpp"
#include "engine/AsyncPipelineEngine.hpp"
#include "vision/drivers/PixelConverters.hpp"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

void testEarCalculation() {
    std::cout << "[TEST] 1. 測試 EAR 幾何特徵計算...";
    std::array<efd::Point3D, 6> eyePoints = {{
        {-10.0f,  0.0f, 0.0f}, // p1 (outer)
        { -5.0f,  3.0f, 0.0f}, // p2 (top-outer)
        {  5.0f,  3.0f, 0.0f}, // p3 (top-inner)
        { 10.0f,  0.0f, 0.0f}, // p4 (inner)
        {  5.0f, -3.0f, 0.0f}, // p5 (bottom-inner)
        { -5.0f, -3.0f, 0.0f}  // p6 (bottom-outer)
    }};

    float ear = efd::FeatureExtractor::calculateSingleEar(eyePoints);
    assert(std::fabs(ear - 0.30f) < 0.01f);
    std::cout << " [PASS] (EAR = " << ear << ")\n";
}

void testAdaptiveBaseline() {
    std::cout << "[TEST] 2. 測試動態滑動窗口基準自適應與選擇性更新...";
    efd::AdaptiveBaseline baseline(0.30f, 0.7f, 1.5f, 100);
    
    // 正常睜眼樣本更新
    for (int i = 0; i < 50; ++i) {
        baseline.update(0.29f, false);
    }
    float thNormal = baseline.getCurrentThreshold();
    assert(thNormal >= 0.15f && thNormal <= 0.30f);

    // 模擬微睡眠 (連續閉眼 EAR = 0.100) -> 改良驗證: 閾值不應被閉眼樣本崩塌拉低
    for (int i = 0; i < 50; ++i) {
        baseline.update(0.10f, true); // 標記為閉眼
    }
    float thAfterClosed = baseline.getCurrentThreshold();
    // 驗證選擇性更新生效: 閾值依然穩固在清醒基準附近，未崩塌至 0.12
    assert(std::fabs(thAfterClosed - thNormal) < 0.03f);

    // 測試熱重啟快速再校準 (Fast Recalibrate)
    std::vector<float> resumeSamples(30, 0.33f);
    baseline.fastRecalibrate(resumeSamples);
    assert(baseline.getCalibrationData().baselineEar > 0.30f);

    std::cout << " [PASS] (微睡眠抗污染正常, 閾值=" << thAfterClosed << ")\n";
}

void testMseComplexity() {
    std::cout << "[TEST] 3. 測試 MSE 多尺度熵與 CI 複雜度計算...";
    efd::MseCalculator mse(3, 2, 0.15f);

    std::vector<float> signal(100);
    for (size_t i = 0; i < 100; ++i) {
        signal[i] = 0.30f + 0.03f * std::sin(i * 0.2f);
    }

    efd::ComplexityMetrics metrics = mse.calculateComplexity(signal);
    assert(metrics.sampleEntropyScales.size() == 3);
    assert(metrics.complexityIndex >= 0.0f);
    std::cout << " [PASS] (CI = " << metrics.complexityIndex << ")\n";
}

void testEmdDecomposition() {
    std::cout << "[TEST] 4. 測試 EMD (經驗模態分解)...";
    efd::EmdCalculator emd(3, 10, 0.05f);

    std::vector<float> signal(120);
    for (size_t i = 0; i < 120; ++i) {
        signal[i] = std::sin(i * 0.05f) + 0.5f * std::sin(i * 0.4f);
    }

    efd::EmdResult result = emd.decompose(signal);
    assert(!result.imfs.empty());
    std::cout << " [PASS] (成功分解出 " << result.imfs.size() << " 個 IMF)\n";
}

// 以 1 秒步進模擬 seconds 秒的輸入，回傳期間觸發的警報次數
struct FsmInput { bool present; float perclos; float blink; float ci; };
static const FsmInput kRelaxedInput { true, 0.02f, 16.0f, 4.5f };  // 約 4 分
static const FsmInput kMidInput     { true, 0.10f, 6.0f, 1.5f };   // 約 47 分 (注意區間)
static const FsmInput kSevereInput  { true, 0.30f, 4.0f, 1.0f };   // 約 84 分
static const FsmInput kAwayInput    { false, 0.0f, 0.0f, 0.0f };

static int feedFsm(efd::FatigueStateMachine& fsm, const FsmInput& in, int seconds, int& alertCount, efd::SystemState* last = nullptr) {
    int before = alertCount;
    efd::SystemState s;
    for (int i = 0; i < seconds; ++i) {
        s = fsm.update(in.present, in.perclos, in.blink, in.ci, 1.0f);
    }
    if (last) *last = s;
    return alertCount - before;
}

void testFatigueStateMachineFullCycle() {
    std::cout << "[TEST] 5. 測試 20/5/5 狀態機 (20 秒平滑、持續 5 秒觸發、20 分冷卻、5 分快篩、離座清零)...";
    efd::FatigueStateMachine fsm(1200, 300, 300);

    int alertCount = 0;
    std::string lastAlertMsg;
    fsm.setAlertCallback([&](efd::FatigueLevel level, float, const std::string& msg) {
        assert(level == efd::FatigueLevel::SevereWarning);
        alertCount++;
        lastAlertMsg = msg;
    });
    efd::SystemState s;

    // 1. 正常清醒 60 秒：不提醒
    assert(feedFsm(fsm, kRelaxedInput, 60, alertCount, &s) == 0);
    assert(s.fatigueLevel == efd::FatigueLevel::Relaxed);
    assert(s.cooldownState == efd::CooldownState::NormalTracking);

    // 2. 短暫高分 (例如揉眼 5 秒) 不應觸發
    assert(feedFsm(fsm, kSevereInput, 5, alertCount) == 0);
    assert(feedFsm(fsm, kRelaxedInput, 60, alertCount) == 0);

    // 3. 持續高分：平滑分數需爬升到 65 並維持 5 秒才提醒 (約 30 多秒，不會在前 20 秒就跳出)
    assert(feedFsm(fsm, kSevereInput, 20, alertCount) == 0);
    assert(feedFsm(fsm, kSevereInput, 40, alertCount, &s) == 1);
    assert(lastAlertMsg == "你的眼睛處於疲勞狀態，請適當休息");
    assert(s.cooldownState == efd::CooldownState::InCooldown);

    // 4. 20 分鐘冷卻內即使持續疲勞也完全靜默
    assert(feedFsm(fsm, kSevereInput, 1000, alertCount, &s) == 0);
    assert(s.cooldownState == efd::CooldownState::InCooldown);
    assert(s.fatigueLevel == efd::FatigueLevel::SevereWarning); // 等級仍反映真實狀態

    // 5. 冷卻期滿後進入快篩，快篩要滿 5 分鐘才判定
    int remaining = s.cooldownRemainingSeconds;
    assert(feedFsm(fsm, kSevereInput, remaining, alertCount, &s) == 0);
    assert(s.cooldownState == efd::CooldownState::FastScreening);
    assert(feedFsm(fsm, kSevereInput, 298, alertCount, &s) == 0);
    assert(s.cooldownState == efd::CooldownState::FastScreening);

    // 6. 快篩窗口平均 >= 65：再提醒一次並重新冷卻 20 分鐘
    assert(feedFsm(fsm, kSevereInput, 2, alertCount, &s) == 1);
    assert(s.cooldownState == efd::CooldownState::InCooldown);
    assert(s.cooldownRemainingSeconds >= 1199);

    // 7. 離座未滿 5 分鐘：冷卻照常倒數，不清零
    assert(feedFsm(fsm, kAwayInput, 200, alertCount, &s) == 0);
    assert(s.fatigueLevel == efd::FatigueLevel::UserAway);
    assert(s.cooldownState == efd::CooldownState::InCooldown);
    assert(s.cooldownRemainingSeconds <= 1001 && s.cooldownRemainingSeconds >= 999);

    // 8. 回座後再離座滿 5 分鐘：視為已休息，清除冷卻與平滑分數
    feedFsm(fsm, kRelaxedInput, 1, alertCount);
    assert(feedFsm(fsm, kAwayInput, 300, alertCount, &s) == 0);
    assert(s.cooldownState == efd::CooldownState::NormalTracking);
    assert(s.smoothedFatigueScore == 0.0f);

    // 9. 快篩平均落在 45~65：不提醒，繼續下一輪快篩；之後 5 分鐘平均 < 45 才回到常態
    efd::FatigueStateMachine fsm2(1200, 300, 300);
    int alert2 = 0;
    fsm2.setAlertCallback([&](efd::FatigueLevel, float, const std::string&) { alert2++; });
    assert(feedFsm(fsm2, kSevereInput, 200, alert2, &s) == 1);
    // 剛好餵到冷卻結束，讓快篩窗口從頭開始
    assert(feedFsm(fsm2, kRelaxedInput, s.cooldownRemainingSeconds, alert2, &s) == 0);
    assert(s.cooldownState == efd::CooldownState::FastScreening);
    assert(feedFsm(fsm2, kMidInput, 300, alert2, &s) == 0);
    assert(s.cooldownState == efd::CooldownState::FastScreening);
    assert(feedFsm(fsm2, kRelaxedInput, 100, alert2, &s) == 0);
    assert(s.cooldownState == efd::CooldownState::FastScreening); // 窗口未滿不提前判定
    assert(feedFsm(fsm2, kRelaxedInput, 200, alert2, &s) == 0);
    assert(s.cooldownState == efd::CooldownState::NormalTracking);

    std::cout << " [PASS] (共 " << alertCount << " 次提醒：持續觸發 1 次 + 快篩升級 1 次)\n";
}

void testFatigueStateMachinePersistence() {
    std::cout << "[TEST] 13. 測試冷卻狀態持久化 (App 重啟延續冷卻、關閉滿 5 分鐘清零)...";
    efd::FatigueStateMachine fsm(1200, 300, 300);
    int alerts = 0;
    fsm.setAlertCallback([&](efd::FatigueLevel, float, const std::string&) { alerts++; });
    efd::SystemState s;
    feedFsm(fsm, kSevereInput, 200, alerts, &s);
    assert(alerts == 1 && s.cooldownState == efd::CooldownState::InCooldown);
    int remainingBefore = s.cooldownRemainingSeconds;

    const int64_t savedAt = 1800000000;
    std::string data = fsm.serialize(savedAt);

    // 關閉 100 秒後重啟：冷卻延續並扣掉關閉期間
    efd::FatigueStateMachine restored(1200, 300, 300);
    assert(restored.restore(data, savedAt + 100));
    efd::SystemState r = restored.getState();
    assert(r.cooldownState == efd::CooldownState::InCooldown);
    assert(std::abs(r.cooldownRemainingSeconds - (remainingBefore - 100)) <= 1);

    // 關閉 400 秒 (>= 5 分鐘) 後重啟：比照離座清零
    efd::FatigueStateMachine restoredLate(1200, 300, 300);
    assert(restoredLate.restore(data, savedAt + 400));
    assert(restoredLate.getState().cooldownState == efd::CooldownState::NormalTracking);

    // 損毀資料不應崩潰
    efd::FatigueStateMachine broken(1200, 300, 300);
    assert(!broken.restore("savedAt=abc\nstate=9\n", savedAt));
    std::cout << " [PASS] (重啟後剩餘 " << r.cooldownRemainingSeconds << " 秒)\n";
}

void testTimeBasedFeatureWindow() {
    std::cout << "[TEST] 14. 測試 60 秒時間窗口 PERCLOS 與眨眼率 (不同幀率結果一致)...";
    // 每 4 秒眨眼一次 (閉眼 200ms) = 15 次/分，分別以 30 FPS 與 10 FPS 模擬 90 秒
    auto simulate = [](float fps) {
        efd::FeatureExtractor fx(60.0f, 0.20f);
        efd::EyeMetrics m;
        const int total = static_cast<int>(90.0f * fps);
        const float stepMs = 1000.0f / fps;
        for (int i = 0; i < total; ++i) {
            float tMs = i * stepMs;
            bool closed = std::fmod(tMs, 4000.0f) < 200.0f;
            float ear = closed ? 0.10f : 0.30f;
            m = fx.processEar(ear, ear, fps);
        }
        assert(std::fabs(fx.getWindowDurationSeconds() - 60.0f) < 1.0f);
        return m;
    };
    efd::EyeMetrics m30 = simulate(30.0f);
    efd::EyeMetrics m10 = simulate(10.0f);
    assert(std::fabs(m30.perclos - 0.05f) < 0.01f);
    assert(std::fabs(m10.perclos - 0.05f) < 0.01f);
    assert(std::fabs(m30.blinkRatePerMin - 15.0f) <= 1.5f);
    assert(std::fabs(m10.blinkRatePerMin - 15.0f) <= 1.5f);

    // 窗口未滿 30 秒時不輸出眨眼率 (不計分)
    efd::FeatureExtractor shortFx(60.0f, 0.20f);
    efd::EyeMetrics early;
    for (int i = 0; i < 20 * 30; ++i) early = shortFx.processEar(0.30f, 0.30f, 30.0f);
    assert(early.blinkRatePerMin == 0.0f);

    // 一次 1 秒的揉眼在 60 秒窗口內只佔約 1.7%
    efd::FeatureExtractor rubFx(60.0f, 0.20f);
    efd::EyeMetrics rub;
    for (int i = 0; i < 60 * 30; ++i) {
        float ear = (i >= 900 && i < 930) ? 0.10f : 0.30f;
        rub = rubFx.processEar(ear, ear, 30.0f);
    }
    assert(rub.perclos < 0.02f);

    std::vector<float> resampled = rubFx.getEarHistoryResampled(10.0f);
    assert(resampled.size() >= 595 && resampled.size() <= 601);
    std::cout << " [PASS] (30FPS: PERCLOS=" << m30.perclos << " 眨眼=" << m30.blinkRatePerMin
              << "/分; 10FPS: PERCLOS=" << m10.perclos << " 眨眼=" << m10.blinkRatePerMin << "/分)\n";
}

void testCalibrationBoundsAndRestore() {
    std::cout << "[TEST] 15. 測試校準閾值不被自動調整覆蓋、校準結果可還原...";
    efd::AdaptiveBaseline baseline(0.31f, 0.7f, 1.5f, 300);
    std::vector<float> awake(90);
    for (size_t i = 0; i < awake.size(); ++i) awake[i] = 0.28f + 0.004f * std::sin(static_cast<float>(i));
    baseline.calibrate(awake);
    efd::CalibrationData cal = baseline.getCalibrationData();
    assert(cal.isCalibrated);
    // 閾值落在基準的 65%~85%
    assert(cal.thresholdEar >= cal.baselineEar * 0.649f && cal.thresholdEar <= cal.baselineEar * 0.851f);

    // 長時間餵入偏高的睜眼樣本，閾值最多漂移 15%
    for (int i = 0; i < 600; ++i) baseline.update(0.40f, false);
    float drifted = baseline.getCurrentThreshold();
    assert(drifted <= cal.thresholdEar * 1.151f);

    efd::AdaptiveBaseline restored(0.31f, 0.7f, 1.5f, 300);
    restored.restoreCalibration(cal);
    assert(restored.getCalibrationData().isCalibrated);
    assert(std::fabs(restored.getCurrentThreshold() - cal.thresholdEar) < 1e-6f);
    std::cout << " [PASS] (校準閾值=" << cal.thresholdEar << ", 漂移後=" << drifted << ")\n";
}

static std::string makeTestDir(const std::string& name) {
    std::filesystem::path dir = std::filesystem::temp_directory_path() / name;
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    return dir.string();
}

void testEngineCalibrationAndDataDirectory() {
    std::cout << "[TEST] 16. 測試資料目錄持久化與兩段式校準 (無臉時校準失敗、成功後重啟免重測)...";
    std::string dir = makeTestDir("efd_test_engine_calib");
    std::string uuid;
    {
        efd::AsyncPipelineEngine engine(efd::PlatformType::Windows);
        engine.setAutoNightlySyncEnabled(false);
        engine.setDataDirectory(dir);
        uuid = engine.getStudyTracker().getSubjectUuid();
        assert(!uuid.empty());
        assert(!engine.hasSavedCalibration());

        engine.getCameraService().setForceSynthetic(true); // 不使用開發機的實體鏡頭
        engine.getCameraService().startSynthetic();
        assert(engine.start());

        // 模擬使用者不在鏡頭前：校準必須失敗，不得寫入基準
        engine.setSimulatedEyeOpenness(-1.0f);
        std::this_thread::sleep_for(std::chrono::milliseconds(1300)); // 超過 1 秒離座判定
        engine.beginCalibration();
        std::this_thread::sleep_for(std::chrono::milliseconds(1200));
        assert(!engine.finishCalibration());
        assert(!engine.hasSavedCalibration());

        // 使用者在鏡頭前：校準成功並保存
        engine.setSimulatedEyeOpenness(1.0f);
        std::this_thread::sleep_for(std::chrono::milliseconds(1500));
        engine.beginCalibration();
        std::this_thread::sleep_for(std::chrono::milliseconds(1500));
        assert(engine.finishCalibration());
        assert(engine.hasSavedCalibration());
        engine.stop();
    }
    assert(std::filesystem::exists(std::filesystem::path(dir) / "efd_study_config.json"));
    assert(std::filesystem::exists(std::filesystem::path(dir) / "efd_fsm_state.txt"));

    // 重新開啟：同一位受試者、沿用校準
    {
        efd::AsyncPipelineEngine engine(efd::PlatformType::Windows);
        engine.setAutoNightlySyncEnabled(false);
        engine.setDataDirectory(dir);
        assert(engine.getStudyTracker().getSubjectUuid() == uuid);
        assert(engine.hasSavedCalibration());
        assert(engine.getAdaptiveBaseline().getCalibrationData().isCalibrated);
    }
    std::filesystem::remove_all(dir);
    std::cout << " [PASS] (受試者 " << uuid << " 重啟後資料與校準皆保留)\n";
}

void testCorruptedConfigDoesNotCrash() {
    std::cout << "[TEST] 17. 測試設定檔與資料庫損毀時不閃退...";
    std::string dir = makeTestDir("efd_test_corrupt");
    std::string cfg = (std::filesystem::path(dir) / "cfg.json").string();
    {
        std::ofstream out(cfg);
        out << "{\n  \"subjectUuid\": \"SUBJ-KEEP\",\n  \"currentDay\": abc,\n  \"status\": 99,\n"
               "  \"studyStartTimestamp\": ,\n  \"lastSyncedDate\": 2026x\n}\n";
    }
    efd::StudyWorkflowTracker tracker(cfg);
    assert(tracker.initialize());
    assert(tracker.getSubjectUuid() == "SUBJ-KEEP");
    assert(tracker.getStatus() == efd::StudyStatus::ActiveMonitoring);

    std::string db = (std::filesystem::path(dir) / "db.dat").string();
    {
        std::ofstream out(db);
        out << "TimestampMs,SubjectUUID,EAR,PERCLOS,ComplexityIndex,FatigueScore,AlertLevel\n"
            << "1700000000000,S,0.3,0.01,4.5,5,0\n"
            << "17000000000\n"          // 斷電留下的半行
            << "garbage,line,x,y\n";
    }
    efd::DatabaseService dbs(db);
    assert(dbs.initialize());
    assert(dbs.getRecordCount() == 1);
    assert(dbs.getOldestRecordTimestampMs() == 1700000000000LL);
    std::filesystem::remove_all(dir);
    std::cout << " [PASS]\n";
}

static int dateIntDaysAgo(int days) {
    std::time_t t = std::time(nullptr) - static_cast<std::time_t>(days) * 86400;
    std::tm tmv{};
#ifdef _WIN32
    localtime_s(&tmv, &t);
#else
    localtime_r(&t, &tmv);
#endif
    return (tmv.tm_year + 1900) * 10000 + (tmv.tm_mon + 1) * 100 + tmv.tm_mday;
}

void testNightlySyncMarksOnlyOnSuccess() {
    std::cout << "[TEST] 18. 測試補傳：上傳失敗不標記已同步、成功才標記...";
    const int64_t twoDaysAgoMs = (static_cast<int64_t>(std::time(nullptr)) - 2 * 86400) * 1000;

    auto runCase = [&](const std::string& dirName, bool uploadOk) {
        std::string dir = makeTestDir(dirName);
        efd::AsyncPipelineEngine engine(efd::PlatformType::Windows);
        engine.setDataDirectory(dir);
        std::atomic<int> uploads{0};
        engine.getSyncWorker().setUploader([&](const std::string&, const std::string&, std::string& resp) {
            uploads++;
            resp = uploadOk ? "{\"status\":\"success\"}" : "GAS_ERR: offline";
            return uploadOk;
        });
        efd::FatigueRecord r;
        r.timestampMs = twoDaysAgoMs; // 前天的資料尚未上傳
        r.subjectUuid = engine.getStudyTracker().getSubjectUuid();
        engine.getDatabaseService().logRecord(r);
        assert(engine.getStudyTracker().getLastSyncedDate() == 0);

        // 不可把有副作用的呼叫寫在 assert 內：Release (NDEBUG) 會整行移除，上傳不會被觸發
        const bool triggered = engine.checkAndTriggerNightlySync();
        assert(triggered);
        (void)triggered;
        for (int i = 0; i < 300 && engine.getSyncWorker().isSyncing(); ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        int lastSynced = engine.getStudyTracker().getLastSyncedDate();
        bool retriggered = engine.checkAndTriggerNightlySync();
        std::filesystem::remove_all(dir);
        return std::make_tuple(uploads.load(), lastSynced, retriggered);
    };

    auto [failUploads, failMarked, failRetry] = runCase("efd_test_sync_fail", false);
    assert(failUploads == 1);
    assert(failMarked == 0);   // 失敗不標記
    assert(!failRetry);        // 10 分鐘內不重複打伺服器

    auto [okUploads, okMarked, okRetry] = runCase("efd_test_sync_ok", true);
    assert(okUploads == 1);
    assert(okMarked >= dateIntDaysAgo(1)); // 補傳標記昨天 (21:00 後則為今天)
    assert(!okRetry);
    std::cout << " [PASS] (失敗: 標記=" << failMarked << "; 成功: 標記=" << okMarked << ")\n";
}

void testPresenceRejectsUniformSkinColoredObject() {
    std::cout << "[TEST] 19. 測試膚色但無眼睛特徵的物體 (牆面/木頭) 不被判定為人臉...";
    efd::FaceLandmarker landmarker;
    landmarker.initialize();
    const int w = 320, h = 240;
    std::vector<uint8_t> pixels(static_cast<size_t>(w * h * 3));
    // 灰藍色背景
    for (size_t i = 0; i < pixels.size(); i += 3) {
        pixels[i] = 90; pixels[i + 1] = 100; pixels[i + 2] = 120;
    }
    // 中央一塊帶輕微紋理的膚色物體 (輪廓像頭部，例如木頭椅背或紙箱，但沒有眼睛暗點)
    for (int y = 60; y < 200; ++y) {
        for (int x = 110; x < 210; ++x) {
            size_t idx = static_cast<size_t>((y * w + x) * 3);
            int n = ((x * 7 + y * 13) % 9) - 4;
            pixels[idx] = static_cast<uint8_t>(205 + n);
            pixels[idx + 1] = static_cast<uint8_t>(155 + n);
            pixels[idx + 2] = static_cast<uint8_t>(125 + n);
        }
    }
    bool everDetected = false;
    for (int i = 0; i < 10; ++i) {
        efd::LandmarkDetectionResult r = landmarker.detect(pixels.data(), w, h, efd::PixelFormat::RGB888, false);
        everDetected = everDetected || r.hasFace;
    }
    assert(!everDetected);
    std::cout << " [PASS] (原因: " << landmarker.getLatestPresenceDiagnostic().unconfirmedReason << ")\n";
}

void testFaceDetectedAboveSkinColoredShirt() {
    std::cout << "[TEST] 23. 測試穿膚色衣服時，臉與脖子、衣服連成一塊仍能偵測 (iPhone 直向 480x640)...";
    const int w = 480, h = 640;
    std::vector<uint8_t> px(static_cast<size_t>(w * h * 3));
    auto fill = [&](int x, int y, uint8_t r, uint8_t g, uint8_t b) {
        if (x < 0 || x >= w || y < 0 || y >= h) return;
        size_t i = static_cast<size_t>((y * w + x) * 3);
        px[i] = r; px[i + 1] = g; px[i + 2] = b;
    };
    auto inEllipse = [](int x, int y, int cx, int cy, int rx, int ry) {
        float dx = static_cast<float>(x - cx) / rx, dy = static_cast<float>(y - cy) / ry;
        return dx * dx + dy * dy <= 1.0f;
    };
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            int n = ((x * 7 + y * 13) % 7) - 3;  // 輕微紋理
            uint8_t r = 120, g = 125, b = 130;   // 灰色牆面
            if (y >= 470) { r = 240; g = 190; b = 150; }                       // 淺橘色 T 恤 (整排膚色)
            else if (x >= 180 && x < 300 && y >= 370) { r = 220; g = 170; b = 140; } // 脖子
            if (inEllipse(x, y, 240, 240, 120, 160)) {
                r = 220; g = 170; b = 140;                                    // 臉
                if (y < 110) { r = 30; g = 25; b = 25; }                      // 瀏海
                if (inEllipse(x, y, 187, 220, 22, 6) || inEllipse(x, y, 293, 220, 22, 6)) {
                    r = 40; g = 30; b = 30;                                   // 眼睛
                }
                if (inEllipse(x, y, 240, 330, 30, 8)) { r = 170; g = 80; b = 85; } // 嘴唇
            }
            fill(x, y, static_cast<uint8_t>(std::clamp(r + n, 0, 255)),
                       static_cast<uint8_t>(std::clamp(g + n, 0, 255)),
                       static_cast<uint8_t>(std::clamp(b + n, 0, 255)));
        }
    }

    efd::FaceLandmarker landmarker;
    landmarker.initialize();
    landmarker.detect(px.data(), w, h, efd::PixelFormat::RGB888, false);
    const auto d = landmarker.getLatestPresenceDiagnostic();
    assert(d.skinBoxW > w * 0.9f);          // 未裁切前，膚色區域橫跨整個畫面 (舊版會判定為佔滿畫面而失敗)
    assert(d.rawPersonInFrame && d.rawFacingCamera);
    assert(d.faceBoxW < w * 0.6f);          // 裁切後只剩臉的寬度
    assert(d.faceBoxY + d.faceBoxH < 470);  // 不包含衣服
    assert(std::abs(d.eyeCenterY - 220) < 25);
    std::cout << " [PASS] (膚色區域寬 " << d.skinBoxW << " -> 臉框 " << d.faceBoxW << "x" << d.faceBoxH
              << ", 眼睛 y=" << d.eyeCenterY << ")\n";
}

void testFaceSeparatedFromLargerSkinColoredShirt() {
    std::cout << "[TEST] 24. 測試臉被下巴陰影與更大的膚色衣服分開時，仍檢查到臉 (不只看最大的一塊)...";
    const int w = 480, h = 640;
    std::vector<uint8_t> px(static_cast<size_t>(w * h * 3));
    auto inEllipse = [](int x, int y, int cx, int cy, int rx, int ry) {
        float dx = static_cast<float>(x - cx) / rx, dy = static_cast<float>(y - cy) / ry;
        return dx * dx + dy * dy <= 1.0f;
    };
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            int n = ((x * 7 + y * 13) % 7) - 3;
            int r = 120, g = 125, b = 130;                          // 灰色牆面
            if (y >= 330) { r = 240; g = 190; b = 150; }             // 佔畫面下半部的淺橘色 T 恤 (面積大於臉)
            if (inEllipse(x, y, 240, 180, 95, 125)) {
                r = 220; g = 170; b = 140;                          // 臉
                if (y < 80) { r = 30; g = 25; b = 25; }             // 瀏海
                if (inEllipse(x, y, 198, 165, 18, 5) || inEllipse(x, y, 282, 165, 18, 5)) {
                    r = 40; g = 30; b = 30;                         // 眼睛
                }
            }
            if (y >= 300 && y < 340) { r = 60; g = 50; b = 45; }     // 下巴陰影 / 深色領口：臉與衣服分成兩塊
            px[static_cast<size_t>((y * w + x) * 3)]     = static_cast<uint8_t>(std::clamp(r + n, 0, 255));
            px[static_cast<size_t>((y * w + x) * 3) + 1] = static_cast<uint8_t>(std::clamp(g + n, 0, 255));
            px[static_cast<size_t>((y * w + x) * 3) + 2] = static_cast<uint8_t>(std::clamp(b + n, 0, 255));
        }
    }

    efd::FaceLandmarker landmarker;
    landmarker.initialize();
    landmarker.detect(px.data(), w, h, efd::PixelFormat::RGB888, false);
    const auto d = landmarker.getLatestPresenceDiagnostic();
    assert(d.rawPersonInFrame && d.rawFacingCamera);
    assert(d.faceBoxY + d.faceBoxH < 330);   // 通過的是臉，不是衣服
    std::cout << " [PASS] (臉框 " << d.faceBoxX << "," << d.faceBoxY << " " << d.faceBoxW << "x" << d.faceBoxH << ")\n";
}

void testWarmLitWallIsNotSkin() {
    std::cout << "[TEST] 25. 測試暖光下的米白牆面不被當成膚色 (未穿上衣、肩膀與臉相連)...";
    const int w = 480, h = 640;
    std::vector<uint8_t> px(static_cast<size_t>(w * h * 3));
    auto inEllipse = [](int x, int y, int cx, int cy, int rx, int ry) {
        float dx = static_cast<float>(x - cx) / rx, dy = static_cast<float>(y - cy) / ry;
        return dx * dx + dy * dy <= 1.0f;
    };
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            int n = ((x * 7 + y * 13) % 7) - 3;
            int r = 172, g = 165, b = 137;                          // 暖光頂燈下的米白牆面 (iPhone 實測值)
            if (y < 180) { r = 200; g = 191; b = 179; }              // 天花板
            if (y >= 500 || (x >= 190 && x < 290 && y >= 440)) { r = 137; g = 101; b = 70; } // 肩膀、胸口與脖子
            if (inEllipse(x, y, 240, 350, 85, 115)) {
                r = 147; g = 105; b = 81;                           // 臉
                if (y < 265) { r = 30; g = 25; b = 25; }            // 瀏海
                if (inEllipse(x, y, 200, 320, 16, 5) || inEllipse(x, y, 280, 320, 16, 5)) {
                    r = 35; g = 28; b = 28;                         // 眼睛
                }
            }
            px[static_cast<size_t>((y * w + x) * 3)]     = static_cast<uint8_t>(std::clamp(r + n, 0, 255));
            px[static_cast<size_t>((y * w + x) * 3) + 1] = static_cast<uint8_t>(std::clamp(g + n, 0, 255));
            px[static_cast<size_t>((y * w + x) * 3) + 2] = static_cast<uint8_t>(std::clamp(b + n, 0, 255));
        }
    }

    efd::FaceLandmarker landmarker;
    landmarker.initialize();
    landmarker.detect(px.data(), w, h, efd::PixelFormat::RGB888, false);
    const auto d = landmarker.getLatestPresenceDiagnostic();
    assert(d.skinBoxY > 180);                // 牆面與天花板不屬於膚色區域
    assert(d.rawPersonInFrame && d.rawFacingCamera);
    assert(std::abs(d.eyeCenterY - 320) < 30);
    std::cout << " [PASS] (膚色區域 y=" << d.skinBoxY << ", 臉框 " << d.faceBoxW << "x" << d.faceBoxH
              << ", 眼睛 y=" << d.eyeCenterY << ")\n";
}

// 灰牆前的臉 (480x640)；eyesOpen=false 時眼睛位置填成膚色 (模擬閉眼、低頭)，personPresent=false 時只有牆
static std::vector<uint8_t> makePresenceFrame(bool personPresent, bool eyesOpen) {
    const int w = 480, h = 640;
    std::vector<uint8_t> px(static_cast<size_t>(w * h * 3));
    auto inEllipse = [](int x, int y, int cx, int cy, int rx, int ry) {
        float dx = static_cast<float>(x - cx) / rx, dy = static_cast<float>(y - cy) / ry;
        return dx * dx + dy * dy <= 1.0f;
    };
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            int n = ((x * 7 + y * 13) % 7) - 3;
            int r = 120, g = 125, b = 130;
            if (personPresent && inEllipse(x, y, 240, 260, 100, 135)) {
                r = 215; g = 165; b = 135;
                if (y < 150) { r = 30; g = 25; b = 25; }
                if (eyesOpen && (inEllipse(x, y, 196, 245, 18, 6) || inEllipse(x, y, 284, 245, 18, 6))) {
                    r = 40; g = 30; b = 30;
                }
            }
            size_t i = static_cast<size_t>((y * w + x) * 3);
            px[i] = static_cast<uint8_t>(std::clamp(r + n, 0, 255));
            px[i + 1] = static_cast<uint8_t>(std::clamp(g + n, 0, 255));
            px[i + 2] = static_cast<uint8_t>(std::clamp(b + n, 0, 255));
        }
    }
    return px;
}

void testFaceFillingFrameNeedsBothEyes() {
    std::cout << "[TEST] 28. 測試手機拿近、臉佔滿畫面時仍可偵測 (需雙眼成立)，無眼睛或只有單眼時不判定為人臉...";
    const int w = 320, h = 240;
    // eyes: 0 = 無眼睛 (整面膚色)、1 = 只有左眼、2 = 雙眼
    auto makeCloseUp = [&](int eyes) {
        std::vector<uint8_t> px(static_cast<size_t>(w * h * 3));
        auto inEllipse = [](int x, int y, int cx, int cy, int rx, int ry) {
            float dx = static_cast<float>(x - cx) / rx, dy = static_cast<float>(y - cy) / ry;
            return dx * dx + dy * dy <= 1.0f;
        };
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                int n = ((x * 7 + y * 13) % 7) - 3;
                int r = 215, g = 165, b = 135;  // 整個畫面都是臉的膚色
                if ((eyes >= 1 && inEllipse(x, y, 90, 91, 18, 6)) || (eyes >= 2 && inEllipse(x, y, 230, 91, 18, 6))) {
                    r = 40; g = 30; b = 30;
                }
                size_t i = static_cast<size_t>((y * w + x) * 3);
                px[i] = static_cast<uint8_t>(std::clamp(r + n, 0, 255));
                px[i + 1] = static_cast<uint8_t>(std::clamp(g + n, 0, 255));
                px[i + 2] = static_cast<uint8_t>(std::clamp(b + n, 0, 255));
            }
        }
        return px;
    };
    auto detectedAny = [&](const std::vector<uint8_t>& px) {
        efd::FaceLandmarker lm;
        lm.initialize();
        bool any = false;
        for (int i = 0; i < 5; ++i) any = lm.detect(px.data(), w, h, efd::PixelFormat::RGB888, false).hasFace || any;
        return any;
    };
    assert(!detectedAny(makeCloseUp(0)));
    assert(!detectedAny(makeCloseUp(1)));
    assert(detectedAny(makeCloseUp(2)));
    std::cout << " [PASS]\n";
}

void testOpennessRobustToMeasurementNoise() {
    std::cout << "[TEST] 29. 測試睜眼時暗點比例上下跳動不會被誤判為閉眼，閉眼時開合度確實下降...";
    // 眼睛暗區高度 eyeRy：5 為一般睜眼，8 為暗點偏多 (眉毛、陰影進入量測框)，0 為閉眼
    auto makeFrame = [](int eyeRy) {
        const int w = 480, h = 640;
        std::vector<uint8_t> px(static_cast<size_t>(w * h * 3));
        auto inEllipse = [](int x, int y, int cx, int cy, int rx, int ry) {
            float dx = static_cast<float>(x - cx) / rx, dy = static_cast<float>(y - cy) / ry;
            return dx * dx + dy * dy <= 1.0f;
        };
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                int n = ((x * 7 + y * 13) % 7) - 3;
                int r = 120, g = 125, b = 130;
                if (inEllipse(x, y, 240, 260, 100, 135)) {
                    r = 215; g = 165; b = 135;
                    if (y < 150) { r = 30; g = 25; b = 25; }
                    if (eyeRy > 0 && (inEllipse(x, y, 196, 245, 18, eyeRy) || inEllipse(x, y, 284, 245, 18, eyeRy))) {
                        r = 40; g = 30; b = 30;
                    }
                }
                size_t i = static_cast<size_t>((y * w + x) * 3);
                px[i] = static_cast<uint8_t>(std::clamp(r + n, 0, 255));
                px[i + 1] = static_cast<uint8_t>(std::clamp(g + n, 0, 255));
                px[i + 2] = static_cast<uint8_t>(std::clamp(b + n, 0, 255));
            }
        }
        return px;
    };
    const auto normal = makeFrame(5);
    const auto larger = makeFrame(8);
    const auto closed = makeFrame(0);

    efd::FaceLandmarker lm;
    lm.initialize();
    // 睜眼 120 幀，其中每 3 幀有 1 幀暗點偏多：舊版以第 90 百分位為睜眼基準時，
    // 基準被這些幀墊高，一般睜眼的幀反而低於閉眼門檻 (PERCLOS 被誇大)
    int lowOpenness = 0;
    int counted = 0;
    for (int i = 0; i < 120; ++i) {
        const auto& f = (i % 3 == 2) ? larger : normal;
        auto r = lm.detect(f.data(), 480, 640, efd::PixelFormat::RGB888, false);
        if (i >= 30 && r.hasFace) {
            counted++;
            // EAR = 0.10 + 0.22 x 開合度；預設閉眼門檻約 0.215 對應開合度 0.52
            if (lm.getLatestPresenceDiagnostic().eyeOpenness < 0.52f) lowOpenness++;
        }
    }
    assert(counted > 60);
    assert(lowOpenness * 10 < counted);  // 誤判為閉眼的幀數低於 10%
    // 閉眼：連續幾幀後開合度降到閉眼範圍
    float closedOpenness = 1.0f;
    for (int i = 0; i < 6; ++i) {
        lm.detect(closed.data(), 480, 640, efd::PixelFormat::RGB888, false);
        closedOpenness = lm.getLatestPresenceDiagnostic().eyeOpenness;
    }
    assert(closedOpenness < 0.3f);
    std::cout << " [PASS] (睜眼誤判 " << lowOpenness << "/" << counted << "，閉眼開合度 " << closedOpenness << ")\n";

    std::cout << "[TEST] 30. 測試長時間閉眼 (閉眼幀多於睜眼幀) 時開合度維持閉眼、仍判定在座，睜眼後恢復...";
    // 接續上面的狀態再閉眼 250 幀：若睜眼基準納入閉眼幀，中位數會變成閉眼的暗點比例，閉眼反被判為睜眼。
    // 真實閉眼仍留有睫毛線與陰影 (三星 A71 實測閉眼暗點比例約 0.05，高於基準下限 0.02)，以高度 2 的暗線模擬
    const auto lashLine = makeFrame(2);
    bool presentThroughout = true;
    int openedDuringClosure = 0;
    for (int i = 0; i < 250; ++i) {
        auto r = lm.detect(lashLine.data(), 480, 640, efd::PixelFormat::RGB888, false);
        presentThroughout = presentThroughout && r.hasFace;
        if (lm.getLatestPresenceDiagnostic().eyeOpenness >= 0.52f) openedDuringClosure++;
    }
    const auto closureDiag = lm.getLatestPresenceDiagnostic();
    std::cout << " (閉眼暗點 " << closureDiag.eyeOpenDarkRatio << " / 睜眼基準 " << closureDiag.eyeOpenBaseline << ")";
    assert(presentThroughout);
    assert(openedDuringClosure == 0);
    float reopened = 0.0f;
    for (int i = 0; i < 6; ++i) {
        lm.detect(normal.data(), 480, 640, efd::PixelFormat::RGB888, false);
        reopened = lm.getLatestPresenceDiagnostic().eyeOpenness;
    }
    assert(reopened > 0.8f);
    std::cout << " [PASS] (閉眼 250 幀皆判定閉眼，睜眼後開合度 " << reopened << ")\n";
}

void testClosedEyesStillPresentAfterLock() {
    std::cout << "[TEST] 26. 測試鎖定臉部後閉眼/低頭仍判定在座，人離開後 1 秒判定離座...";
    const auto open = makePresenceFrame(true, true);
    const auto closed = makePresenceFrame(true, false);
    const auto empty = makePresenceFrame(false, false);

    // 沒有鎖定過的臉：只有膚色、看不到眼睛時不可判定為人臉 (避免把膚色物體當成人)
    {
        efd::FaceLandmarker fresh;
        fresh.initialize();
        bool any = false;
        for (int i = 0; i < 5; ++i) any = any || fresh.detect(closed.data(), 480, 640, efd::PixelFormat::RGB888, false).hasFace;
        assert(!any);
    }

    efd::FaceLandmarker lm;
    lm.initialize();
    for (int i = 0; i < 3; ++i) lm.detect(open.data(), 480, 640, efd::PixelFormat::RGB888, false);
    assert(lm.detect(open.data(), 480, 640, efd::PixelFormat::RGB888, false).hasFace);

    // 閉眼 2 秒 (以 10 fps 餵入)：仍在座
    for (int i = 0; i < 20; ++i) {
        auto r = lm.detect(closed.data(), 480, 640, efd::PixelFormat::RGB888, false);
        assert(r.hasFace);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    assert(lm.getLatestPresenceDiagnostic().presenceByTracking);

    // 人離開：超過 1 秒後判定離座
    bool stillPresent = true;
    for (int i = 0; i < 15; ++i) {
        stillPresent = lm.detect(empty.data(), 480, 640, efd::PixelFormat::RGB888, false).hasFace;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    assert(!stillPresent);
    std::cout << " [PASS]\n";
}

void testCameraPixelFormatConversion() {
    std::cout << "[TEST] 27. 測試 Windows 相機原生格式轉換 (NV12 黑白條紋、UYVY 偏色、stride 補齊、由下而上)...";
    using namespace efd::pixel;
    const int w = 640, h = 480, stride = 704;   // 每行補齊到 704 位元組 (常見於筆電鏡頭)
    auto within3 = [](int a, int b) { return std::abs(a - b) <= 3; };

    // 1. NV12：膚色 Y=160 U=110 V=145 → 約 (184, 154, 128)
    std::vector<uint8_t> nv12(static_cast<size_t>(stride) * h * 3 / 2, 0);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) nv12[static_cast<size_t>(y) * stride + x] = 160;
    }
    for (int y = 0; y < h / 2; ++y) {
        uint8_t* uv = nv12.data() + static_cast<size_t>(stride) * h + static_cast<size_t>(y) * stride;
        for (int x = 0; x < w; x += 2) { uv[x] = 110; uv[x + 1] = 145; }
    }
    assert(requiredBytes(RawFormat::NV12, stride, h) == nv12.size());
    std::vector<uint8_t> rgb(static_cast<size_t>(w) * h * 3);
    nv12ToRgb888(nv12.data(), stride, w, h, rgb.data());
    for (size_t p : {size_t(0), size_t(w * 3 * 240 + 320 * 3), rgb.size() - 3}) {   // 左上、中央、右下皆一致 (無斜切條紋)
        assert(within3(rgb[p], 184) && within3(rgb[p + 1], 154) && within3(rgb[p + 2], 128));
    }
    // 舊版行為：NV12 直接當 RGB 讀取 → R≈G≈B (灰階)，這就是黑白條紋的來源
    assert(std::abs(int(nv12[0]) - int(nv12[1])) < 5 && std::abs(int(nv12[1]) - int(nv12[2])) < 5);

    // 2. UYVY 與 YUY2：同一個膚色，兩種排列都要解出相同顏色
    std::vector<uint8_t> uyvy(static_cast<size_t>(w) * 2 * h), yuy2(static_cast<size_t>(w) * 2 * h);
    for (size_t i = 0; i < uyvy.size(); i += 4) {
        uyvy[i] = 110; uyvy[i + 1] = 160; uyvy[i + 2] = 145; uyvy[i + 3] = 160;
        yuy2[i] = 160; yuy2[i + 1] = 110; yuy2[i + 2] = 160; yuy2[i + 3] = 145;
    }
    std::vector<uint8_t> rgbU(rgb.size()), rgbY(rgb.size()), rgbWrong(rgb.size());
    uyvyToRgb888(uyvy.data(), w * 2, w, h, rgbU.data());
    yuy2ToRgb888(yuy2.data(), w * 2, w, h, rgbY.data());
    assert(within3(rgbU[0], 184) && within3(rgbU[1], 154) && within3(rgbU[2], 128));
    assert(rgbU == rgbY);
    // 舊版行為：UYVY 誤用 YUY2 解碼，顏色嚴重偏移
    yuy2ToRgb888(uyvy.data(), w * 2, w, h, rgbWrong.data());
    assert(std::abs(int(rgbWrong[0]) - 184) + std::abs(int(rgbWrong[1]) - 154) + std::abs(int(rgbWrong[2]) - 128) > 60);

    // 3. 由下而上的 RGB32 (負 stride)：記憶體第一行是畫面最下方
    const int bw = 4, bh = 2;
    std::vector<uint8_t> bgra(static_cast<size_t>(bw) * 4 * bh);
    for (int x = 0; x < bw; ++x) {
        uint8_t* bottom = bgra.data() + x * 4;            // 記憶體第 0 行 = 畫面下方：藍色
        uint8_t* top = bgra.data() + bw * 4 + x * 4;      // 記憶體第 1 行 = 畫面上方：紅色
        bottom[0] = 255; bottom[1] = 0; bottom[2] = 0;
        top[0] = 0; top[1] = 0; top[2] = 255;
    }
    std::vector<uint8_t> rgbFlip(static_cast<size_t>(bw) * bh * 3);
    bgraToRgb888(bgra.data() + bw * 4 * (bh - 1), -(bw * 4), bw, bh, rgbFlip.data());
    assert(rgbFlip[0] == 255 && rgbFlip[2] == 0);                   // 輸出第一行為紅色 (畫面上方)
    assert(rgbFlip[bw * 3] == 0 && rgbFlip[bw * 3 + 2] == 255);     // 輸出第二行為藍色

    // 4. 長度不足時應被拒絕 (例如相機實際是 1280x720，卻當成 640x480)
    assert(requiredBytes(RawFormat::YUY2, 1280 * 2, 720) > static_cast<size_t>(640 * 480 * 2));

    // 5. 720p 整數倍縮小：1280x720 -> 640x360，維持長寬比，2x2 區塊取平均
    std::vector<uint8_t> big(static_cast<size_t>(1280) * 720 * 3), halfImg(static_cast<size_t>(640) * 360 * 3);
    for (size_t i = 0; i < big.size(); i += 3) {
        const size_t px = i / 3;
        const bool odd = ((px % 1280) % 2) == 1;
        big[i] = odd ? 200 : 100; big[i + 1] = 50; big[i + 2] = 10;
    }
    downscaleRgb888(big.data(), 1280, 720, 2, halfImg.data());
    assert(halfImg[0] == 150 && halfImg[1] == 50 && halfImg[2] == 10);
    assert(halfImg[halfImg.size() - 3] == 150);

    std::cout << " [PASS] (NV12 膚色 " << int(rgb[0]) << "," << int(rgb[1]) << "," << int(rgb[2])
              << "；UYVY 誤用 YUY2 時為 " << int(rgbWrong[0]) << "," << int(rgbWrong[1]) << "," << int(rgbWrong[2]) << ")\n";
}

void testPlatformLifecycleAdapter() {
    std::cout << "[TEST] 6. 測試跨平台生命週期適配器 (PlatformLifecycleAdapter)...";
    efd::PlatformLifecycleAdapter adapter(efd::PlatformType::Windows);
    assert(adapter.getPlatformType() == efd::PlatformType::Windows);
    assert(adapter.supportsSystemTray());

    bool lifecycleCallbackFired = false;
    efd::AppLifecycleState targetState = efd::AppLifecycleState::Active;
    adapter.setLifecycleCallback([&](const efd::LifecycleEvent& event) {
        lifecycleCallbackFired = true;
        targetState = event.currentState;
    });

    // 模擬休眠掛起
    adapter.notifySystemSleep();
    assert(adapter.getCurrentState() == efd::AppLifecycleState::Suspended);
    assert(lifecycleCallbackFired && targetState == efd::AppLifecycleState::Suspended);

    // 模擬休眠喚醒 (Hot Resume)
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    adapter.notifySystemWake();
    assert(adapter.getCurrentState() == efd::AppLifecycleState::Resumed);
    assert(targetState == efd::AppLifecycleState::Resumed);

    std::cout << " [PASS] (" << adapter.getStatusSummary() << ")\n";
}

void testAsyncPipelineEngine() {
    std::cout << "[TEST] 7. 測試五執行緒非同步管線引擎 (AsyncPipelineEngine)...";
    efd::AsyncPipelineEngine engine(efd::PlatformType::Windows);
    engine.setAutoNightlySyncEnabled(false); // 測試不可觸發真實雲端上傳
    std::filesystem::path pipelineDir = std::filesystem::temp_directory_path() / "efd_test_pipeline";
    std::filesystem::create_directories(pipelineDir);
    engine.setDataDirectory(pipelineDir.string());

    std::atomic<int> telemetryCount{0};
    engine.setTelemetryCallback([&](const efd::EngineTelemetry& t) {
        if (t.detection.hasFace) {
            telemetryCount++;
        }
    });

    // 啟動五執行緒管線 (在單元測試環境中使用 Synthetic 測試相機)
    engine.getCameraService().setForceSynthetic(true); // 不使用開發機的實體鏡頭
    engine.getCameraService().startSynthetic();
    assert(engine.start());
    assert(engine.isRunning());
    assert(!engine.isPaused());

    // 執行 250ms
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
    int countBeforePause = telemetryCount.load();
    assert(countBeforePause > 0);

    // 測試暫停 (Pause)
    engine.pause();
    assert(engine.isPaused());
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // 測試恢復 (Resume)
    engine.resume();
    assert(!engine.isPaused());
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    assert(telemetryCount.load() > countBeforePause);

    // 正常停止
    engine.stop();
    assert(!engine.isRunning());

    std::cout << " [PASS] (共非同步處理 " << telemetryCount.load() << " 幀，Pause/Resume 正常)\n";
}

// 測試用偵測器：永遠回報畫面中沒有臉，用來確認引擎改用注入的偵測器
class NoFaceTestDetector : public efd::ILandmarkDetector {
public:
    bool initialize(const std::string& /*modelPath*/) override { m_ready = true; return true; }
    bool isReady() const override { return m_ready; }
    const char* name() const override { return "test-no-face"; }
    efd::LandmarkDetectionResult detect(const efd::RawFrame& /*frame*/) override { return {}; }
    efd::PresenceDiagnostic getLatestPresenceDiagnostic() const override { return {}; }
private:
    bool m_ready = false;
};

void testLandmarkDetectorInjectionAndExternalDetection() {
    std::cout << "[TEST] 31. 臉部偵測器介面：更換偵測器與平台層遞交偵測結果...";
    efd::AsyncPipelineEngine engine(efd::PlatformType::Windows);
    engine.setAutoNightlySyncEnabled(false);
    std::filesystem::path dir = std::filesystem::temp_directory_path() / "efd_test_detector_injection";
    std::filesystem::create_directories(dir);
    engine.setDataDirectory(dir.string());

    // 預設為啟發式偵測器
    assert(std::string(engine.getFaceLandmarker().name()) == "heuristic");

    // 空指標與未初始化的偵測器不可取代原偵測器
    assert(!engine.setLandmarkDetector(nullptr));
    assert(!engine.setLandmarkDetector(std::make_unique<NoFaceTestDetector>()));
    assert(std::string(engine.getFaceLandmarker().name()) == "heuristic");

    auto detector = std::make_unique<NoFaceTestDetector>();
    assert(detector->initialize(""));
    assert(engine.setLandmarkDetector(std::move(detector)));
    assert(std::string(engine.getFaceLandmarker().name()) == "test-no-face");

    std::atomic<int> cameraFaceFrames{0};
    std::atomic<int> externalFrames{0};
    engine.setTelemetryCallback([&](const efd::EngineTelemetry& t) {
        if (t.detection.landmarksMeasured) {
            if (t.detection.hasFace && t.latestFrame.width == 480 && t.latestFrame.data.empty()) {
                externalFrames++;
            }
        } else if (t.detection.hasFace) {
            cameraFaceFrames++;
        }
    });

    // 未啟動時遞交的結果應被忽略
    efd::LandmarkDetectionResult external;
    external.hasFace = true;
    external.landmarksMeasured = true;
    external.landmarks.assign(468, efd::Point3D{});
    efd::RawFrame info;
    info.width = 480;
    info.height = 640;
    engine.submitExternalDetection(info, external);

    engine.getCameraService().setForceSynthetic(true);
    engine.getCameraService().startSynthetic();
    assert(engine.start());

    // 執行中不可更換偵測器
    auto late = std::make_unique<NoFaceTestDetector>();
    late->initialize("");
    assert(!engine.setLandmarkDetector(std::move(late)));

    for (int i = 0; i < 5; ++i) {
        info.timestampMs = i;
        engine.submitExternalDetection(info, external);
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    const int externalWhileRunning = externalFrames.load();
    assert(externalWhileRunning > 0);

    // 暫停期間遞交的結果應被忽略
    engine.pause();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    const int beforePausedSubmit = externalFrames.load();
    engine.submitExternalDetection(info, external);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    assert(externalFrames.load() == beforePausedSubmit);

    engine.stop();

    // 相機影格交給注入的偵測器 (永遠無臉)，不應出現有臉的相機影格
    assert(cameraFaceFrames.load() == 0);

    std::cout << " [PASS] (外部遞交 " << externalWhileRunning << " 幀，相機影格改由注入的偵測器處理)\n";
}

// 讀取二進位 PPM (P6, maxval 255) 測試影像
static bool readPpmFixture(const std::filesystem::path& path, efd::RawFrame& frame) {
    std::ifstream in(path, std::ios::binary);
    std::string magic;
    int w = 0, h = 0, maxval = 0;
    if (!(in >> magic >> w >> h >> maxval) || magic != "P6" || maxval != 255) return false;
    in.get();
    frame.width = w;
    frame.height = h;
    frame.channels = 3;
    frame.format = efd::PixelFormat::RGB888;
    frame.data.resize(static_cast<size_t>(w) * h * 3);
    in.read(reinterpret_cast<char*>(frame.data.data()), static_cast<std::streamsize>(frame.data.size()));
    return static_cast<bool>(in);
}

void testOnnxFaceLandmarkerMatchesMediaPipe() {
    std::cout << "[TEST] 35. ONNX 臉部模型 (Windows) 與 MediaPipe 官方結果一致...";
#ifdef EFD_HAS_ONNX
    auto detector = efd::createModelLandmarkDetector(efd::defaultModelDirectory());
    assert(detector && std::string(detector->name()) == "onnx");

    efd::RawFrame frame;
    assert(readPpmFixture(std::filesystem::path(EFD_TEST_FIXTURE_DIR) / "portrait_480.ppm", frame));

    // MediaPipe 官方 FaceLandmarker (Python, IMAGE 模式) 在同一張圖的結果
    const float refEarLeft = 0.2060f, refEarRight = 0.2000f;
    const efd::Point3D ref33{203.8f, 103.9f, 0.0f}, ref263{272.1f, 102.9f, 0.0f}, ref1{237.7f, 135.7f, 0.0f};
    auto dist = [](const efd::Point3D& a, const efd::Point3D& b) { return std::hypot(a.x - b.x, a.y - b.y); };

    // 第一次：臉部偵測 + 特徵點；第二次：以上一幀特徵點追蹤 (不重新偵測)
    float earL = 0.0f, earR = 0.0f;
    for (int pass = 0; pass < 2; ++pass) {
        efd::LandmarkDetectionResult r = detector->detect(frame);
        assert(r.hasFace && r.landmarksMeasured);
        assert(r.landmarks.size() == efd::kMediaPipeLandmarkCount);
        assert(dist(r.landmarks[33], ref33) < 4.0f);
        assert(dist(r.landmarks[263], ref263) < 4.0f);
        assert(dist(r.landmarks[1], ref1) < 4.0f);
        efd::FeatureExtractor extractor(60.0f, 0.21f);
        efd::EyeMetrics m = extractor.processFrame(r.landmarks, 15.0f);
        assert(std::abs(m.earLeft - refEarLeft) < 0.03f && std::abs(m.earRight - refEarRight) < 0.03f);
        earL = m.earLeft;
        earR = m.earRight;
        assert(detector->getLatestPresenceDiagnostic().failStage.empty());
    }

    // 全黑畫面：沒有臉，追蹤也應放掉
    efd::RawFrame black = frame;
    std::fill(black.data.begin(), black.data.end(), static_cast<uint8_t>(0));
    efd::LandmarkDetectionResult none = detector->detect(black);
    assert(!none.hasFace && none.landmarksMeasured);
    assert(detector->getLatestPresenceDiagnostic().failStage == "model");
    assert(detector->detect(frame).hasFace);

    // 追蹤狀態下的單幀耗時 (前處理 + 特徵點推論)，供 Windows 耗電評估
    const auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < 10; ++i) assert(detector->detect(frame).hasFace);
    const double perFrameMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() / 10.0;
    std::cout << " [PASS] (EAR 左 " << earL << " 右 " << earR << "，官方 " << refEarLeft << " / " << refEarRight
              << "；追蹤每幀 " << std::setprecision(1) << std::fixed << perFrameMs << " ms)\n";
#else
    std::cout << " [SKIP] (未以 ONNX Runtime 建置)\n";
#endif
}

void testRollingOpenBaselineP80() {
    std::cout << "[TEST] 36. 模型偵測器閉眼門檻：PERCLOS 用 P80、眨眼用 P50，睜眼基準跟隨最近 2 分鐘...";
    efd::RollingOpenBaseline baseline(0.26f);
    // 累積不足前沿用校準值：P80 = 0.05 + 0.2 x 0.21，P50 = 0.05 + 0.5 x 0.21
    assert(std::abs(baseline.perclosThreshold() - 0.092f) < 1e-3f);
    assert(std::abs(baseline.blinkThreshold() - 0.155f) < 1e-3f);

    // 60 秒一般看螢幕 (EAR 0.25，5% 為眨眼谷底 0.06)：睜眼基準 0.25，眨眼不拉低基準
    for (int i = 0; i < 600; ++i) baseline.add((i % 20 == 0) ? 0.06f : 0.25f, 0.1f);
    assert(std::abs(baseline.openEar() - 0.25f) < 0.005f);
    const float normalPerclosThr = baseline.perclosThreshold();
    assert(normalPerclosThr > 0.085f && normalPerclosThr < 0.095f);

    // 持續低頭 2.5 分鐘 (EAR 0.17)：基準跟著下降，低頭時的眼瞼位置不再被當成閉眼
    for (int i = 0; i < 1500; ++i) baseline.add(0.17f, 0.1f);
    assert(std::abs(baseline.openEar() - 0.17f) < 0.005f);
    assert(baseline.perclosThreshold() < 0.17f * 0.5f);

    // 眨眼與 PERCLOS 分開判斷：EAR 0.12 持續 200 ms 算一次眨眼，但未達 P80 不計入閉眼時間
    efd::FeatureExtractor extractor(60.0f, 0.092f);
    extractor.setBlinkThreshold(0.155f);
    efd::EyeMetrics m;
    for (int i = 0; i < 10; ++i) m = extractor.processEar(0.25f, 0.25f, 10.0f);
    for (int i = 0; i < 2; ++i) m = extractor.processEar(0.12f, 0.12f, 10.0f);
    assert(!m.isEyeClosed);
    for (int i = 0; i < 3; ++i) m = extractor.processEar(0.25f, 0.25f, 10.0f);
    assert(m.blinkCount == 1);
    assert(m.perclos == 0.0f);
    // 真正閉眼 (EAR 0.06) 才計入 PERCLOS
    for (int i = 0; i < 5; ++i) m = extractor.processEar(0.06f, 0.06f, 10.0f);
    assert(m.isEyeClosed && m.perclos > 0.0f);
    // 未設定眨眼門檻時與閉眼門檻相同 (啟發式偵測器的舊行為)
    extractor.setBlinkThreshold(-1.0f);
    assert(extractor.getBlinkThreshold() == extractor.getEyeClosedThreshold());

    // A71 實測：連續閉眼 2 分鐘 (EAR 0.04) 不可拉低睜眼基準，否則門檻會掉到閉眼 EAR 以下
    efd::RollingOpenBaseline closing(0.26f);
    for (int i = 0; i < 300; ++i) closing.add(0.26f, 0.1f);
    for (int i = 0; i < 1200; ++i) closing.add(0.04f, 0.1f);
    assert(std::abs(closing.perclosThreshold() - 0.092f) < 2e-3f);

    // PERCLOS 單項即可達到提醒門檻 (約 12.5% 以上)：實測 PERCLOS 69% 時最早的公式只有 62 分
    using Fsm = efd::FatigueStateMachine;
    assert(Fsm::computeFatigueScore(0.69f, 15.0f, 3.0f) >= Fsm::kSevereThreshold);
    assert(Fsm::computeFatigueScore(0.13f, 15.0f, 3.0f) >= Fsm::kSevereThreshold);
    assert(Fsm::computeFatigueScore(0.11f, 15.0f, 3.0f) < Fsm::kSevereThreshold);
    assert(Fsm::computeFatigueScore(0.03f, 15.0f, 3.0f) < Fsm::kAttentionThreshold);
    // 各項累加：PERCLOS 中等 (10%) 單獨只到注意區間，加上眨眼變慢與長閉眼即達提醒門檻
    // (舊的加權平均要各項都很嚴重才到 65，A71 實測除了持續閉眼幾乎不會提醒)
    assert(Fsm::computeFatigueScore(0.10f, 15.0f, 2.0f) < Fsm::kSevereThreshold);
    assert(Fsm::computeFatigueScore(0.10f, 15.0f, 2.0f, 350.0f, 3.0f) >= Fsm::kSevereThreshold);
    // A71 正常使用：眨眼率偏低 (約 5 次/分，每秒只分析 10 張) 與偶發長閉眼不可達到注意區間
    assert(Fsm::computeFatigueScore(0.01f, 5.0f, 2.0f, 160.0f, 2.0f) < Fsm::kAttentionThreshold);

    // 閉眼變久時眨眼分數跟著上升：長閉眼 (> 800 ms) 與眨眼時長都計入
    efd::FeatureExtractor slow(60.0f, 0.092f);
    slow.setBlinkThreshold(0.155f);
    efd::EyeMetrics sm;
    for (int sec = 0; sec < 40; ++sec) {
        for (int f = 0; f < 10; ++f) {
            const bool shut = (sec % 10 == 0) || (sec % 5 == 0 && f < 4);   // 每 10 秒閉 1 秒、其餘每 5 秒眨 400 ms
            sm = slow.processEar(shut ? 0.04f : 0.25f, shut ? 0.04f : 0.25f, 10.0f);
        }
    }
    assert(sm.longClosuresPerMin > 5.0f && sm.windowBlinkDurationMs > 350.0f);
    assert(Fsm::computeFatigueScore(0.0f, 15.0f, 2.0f, sm.windowBlinkDurationMs, sm.longClosuresPerMin) > 20.0f);
    assert(Fsm::computeFatigueScore(0.0f, 15.0f, 2.0f, 120.0f, 0.0f) == 0.0f);

    // 複雜度用固定容許誤差：持續閉眼 (平線＋雜訊) 的 CI 低於正常睜眼 (有眨眼起伏)
    efd::MseCalculator mse(5, 2, 0.15f);
    std::vector<float> openSig, shutSig;
    std::mt19937 rng(7);
    std::normal_distribution<float> noise(0.0f, 0.004f);
    for (int i = 0; i < 600; ++i) {
        openSig.push_back(0.25f + 0.02f * std::sin(i * 0.37f) + noise(rng) - ((i % 47) < 2 ? 0.18f : 0.0f));
        shutSig.push_back(0.04f + noise(rng));
    }
    const float r = 0.15f * (0.26f - 0.05f);
    assert(mse.calculateComplexity(shutSig, r).complexityIndex < mse.calculateComplexity(openSig, r).complexityIndex);
    // 舊做法 (r 隨窗口標準差縮放) 會把閉眼的雜訊算成高複雜度
    assert(mse.calculateComplexity(shutSig).complexityIndex > mse.calculateComplexity(shutSig, r).complexityIndex);
    std::cout << std::setprecision(3) << " [PASS] (一般看螢幕 P80 門檻 " << normalPerclosThr << "，低頭 2.5 分鐘後 " << baseline.perclosThreshold() << ")\n";
}

void testCrossoverGroupsAndStudyLog() {
    std::cout << "[TEST] 37. 交叉設計分組、不提醒週照常冷卻、每分鐘摘要與提醒自評上傳...";
    // 組別規則：A 組第 8 天起提醒，B 組第 1-7 天提醒，未分組全程提醒
    assert(!efd::StudyWorkflowTracker::alertsEnabledFor("A", 1));
    assert(!efd::StudyWorkflowTracker::alertsEnabledFor("A", 7));
    assert(efd::StudyWorkflowTracker::alertsEnabledFor("A", 8));
    assert(efd::StudyWorkflowTracker::alertsEnabledFor("B", 7));
    assert(!efd::StudyWorkflowTracker::alertsEnabledFor("B", 8));
    assert(efd::StudyWorkflowTracker::alertsEnabledFor("", 3));

    std::filesystem::path dir = std::filesystem::temp_directory_path() / "efd_test_crossover";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);

    // 組別存檔並鎖定：選定後不可改成另一組
    {
        efd::StudyWorkflowTracker tracker((dir / "cfg.json").string());
        tracker.initialize();
        assert(tracker.getStudyGroup().empty());
        assert(!tracker.setStudyGroup("C"));
        assert(tracker.setStudyGroup("A"));
        assert(!tracker.setStudyGroup("B"));
        assert(tracker.getStudyGroup() == "A");
    }
    {
        efd::StudyWorkflowTracker reloaded((dir / "cfg.json").string());
        reloaded.initialize();
        assert(reloaded.getStudyGroup() == "A");
        assert(!reloaded.areAlertsEnabledToday());   // 第 1 天，A 組不提醒
    }

    // 引擎：A 組第 1 天觸發疲勞判定 -> 照常冷卻，但不交給介面
    efd::AsyncPipelineEngine engine(efd::PlatformType::Windows);
    engine.setAutoNightlySyncEnabled(false);
    engine.setDataDirectory(dir.string(), "cfg.json");
    std::atomic<int> shownAlerts{0};
    engine.setAlertCallback([&](efd::FatigueLevel, float, const std::string&) { shownAlerts++; });
    int fsmAlerts = 0;
    efd::FatigueStateMachine& fsm = engine.getStateMachine();
    for (int i = 0; i < 150; ++i) fsm.update(true, kSevereInput.perclos, kSevereInput.blink, kSevereInput.ci, 1.0f);
    fsmAlerts = fsm.getState().cooldownState == efd::CooldownState::InCooldown ? 1 : 0;
    assert(fsmAlerts == 1);
    assert(shownAlerts.load() == 0);

    // 提醒自評與每分鐘摘要寫入本機紀錄，並出現在上傳 JSON
    engine.recordEyeFatigueResponse(1793600000000LL, 7, 2.5f);
    engine.recordEyeFatigueResponse(1793600060000LL, -1, 30.0f);
    efd::StudyDataLog log;
    log.setDirectory(dir.string());
    efd::MinuteSummary m;
    m.minuteMs = 1793600040000LL;
    m.group = "A";
    m.alertsEnabled = false;
    m.presentSec = 58.0f;
    m.cameraOnSec = 60.0f;
    m.earMean = 0.25f;
    m.blinkCount = 14;
    m.alert = "suppressed";
    m.detector = "onnx";
    m.platform = "Windows";
    m.longClosures = 2.5f;
    log.appendMinute(m);
    const auto minutes = log.readMinutesSince(0);
    assert(minutes.size() == 1 && minutes[0].blinkCount == 14 && minutes[0].alert == "suppressed");
    assert(std::fabs(minutes[0].longClosures - 2.5f) < 0.01f);
    const auto surveys = log.readSurveysSince(0);
    assert(surveys.size() == 2 && surveys[0].eyeFatigue == 7 && !surveys[0].skipped && surveys[1].skipped);
    assert(surveys[0].group == "A" && !surveys[0].alertsEnabled);

    efd::PayloadHeader header;
    header.subjectUuid = "SUBJ-TEST";
    header.extraJsonFields = log.buildUploadJsonFields("SUBJ-TEST", 0);
    const std::string json = efd::NetworkSyncWorker::buildPayloadJson(header, {}, false);
    assert(json.find("\"minute_summaries\": [") != std::string::npos);
    assert(json.find("\"alert\": \"suppressed\"") != std::string::npos);
    assert(json.find("\"long_closures\": 2.5") != std::string::npos);
    assert(json.find("\"eye_fatigue\": 7") != std::string::npos);
    assert(json.find("\"eye_fatigue\": null") != std::string::npos);
    assert(json.find("\"subject_id\": \"SUBJ-TEST\"") != std::string::npos);
    // 時間範圍外的資料不上傳
    assert(log.buildUploadJsonFields("SUBJ-TEST", 1793700000000LL).find("\"minute\"") == std::string::npos);

    // 每日摘要附上研究組別 (試算表每日摘要的 study_group 欄)；未選組時為空字串
    {
        std::string groupDir = makeTestDir("efd_test_study_group");
        efd::AsyncPipelineEngine engine(efd::PlatformType::Android);
        engine.setDataDirectory(groupDir);
        assert(engine.buildStudyLogJson("SUBJ-TEST").find("\"study_group\": \"\"") != std::string::npos);
        assert(engine.getStudyTracker().setStudyGroup("B"));
        efd::PayloadHeader groupHeader;
        groupHeader.subjectUuid = "SUBJ-TEST";
        groupHeader.extraJsonFields = engine.buildStudyLogJson("SUBJ-TEST");
        const std::string groupJson = efd::NetworkSyncWorker::buildPayloadJson(groupHeader, {}, false);
        assert(groupJson.find("\"study_group\": \"B\"") != std::string::npos);
        assert(groupJson.find("\"minute_summaries\": [") != std::string::npos);
        std::filesystem::remove_all(groupDir);
    }
    std::cout << " [PASS]\n";
}

void testAdvanceWithoutObservationKeepsCooldown() {
    std::cout << "[TEST] 32. 相機關閉時只推進倒數：冷卻 20 分鐘與快篩 4+1 分鐘排程、不算離座...";
    efd::FatigueStateMachine fsm(1200, 300, 300);
    int alertCount = 0;
    fsm.setAlertCallback([&](efd::FatigueLevel, float, const std::string&) { alertCount++; });
    efd::SystemState s;
    assert(feedFsm(fsm, kSevereInput, 150, alertCount, &s) == 1);
    assert(s.cooldownState == efd::CooldownState::InCooldown);
    const float scoreAtAlert = s.smoothedFatigueScore;

    // 10 分鐘看不到使用者：若當成離座，5 分鐘就會清除冷卻並把分數歸零
    for (int i = 0; i < 1200; ++i) s = fsm.advanceWithoutObservation(0.5f);
    assert(s.cooldownState == efd::CooldownState::InCooldown);
    assert(s.awaySeconds == 0);
    assert(s.smoothedFatigueScore == scoreAtAlert);
    assert(s.cooldownRemainingSeconds > 0 && s.cooldownRemainingSeconds <= 600);

    assert(!s.shouldCameraRun);

    // 冷卻期滿進入快篩 (不是回到常態追蹤)
    while (s.cooldownState == efd::CooldownState::InCooldown) s = fsm.advanceWithoutObservation(0.5f);
    assert(s.cooldownState == efd::CooldownState::FastScreening);
    assert(s.screeningRemainingSeconds == 300);

    // 快篩 5 分鐘：前 4 分鐘相機關閉，最後 1 分鐘開啟採樣
    for (int i = 0; i < 470; ++i) s = fsm.advanceWithoutObservation(0.5f);   // 經過 235 秒
    assert(!s.shouldCameraRun && !fsm.shouldCameraRun());
    for (int i = 0; i < 20; ++i) s = fsm.advanceWithoutObservation(0.5f);    // 經過 245 秒
    assert(s.shouldCameraRun && fsm.shouldCameraRun());

    // 採樣時段完全沒有樣本 (例如相機沒開成)：不提醒、不清除，再快篩一輪
    for (int i = 0; i < 120; ++i) s = fsm.advanceWithoutObservation(0.5f);
    assert(s.cooldownState == efd::CooldownState::FastScreening);
    assert(!s.shouldCameraRun);
    assert(alertCount == 1);

    // 採樣時段持續疲勞 -> 再次提醒並重新冷卻；採樣時段清醒 -> 回到常態追蹤
    while (!s.shouldCameraRun) s = fsm.advanceWithoutObservation(0.5f);
    assert(feedFsm(fsm, kSevereInput, 60, alertCount, &s) == 1);
    assert(s.cooldownState == efd::CooldownState::InCooldown);
    while (s.cooldownState == efd::CooldownState::InCooldown) s = fsm.advanceWithoutObservation(0.5f);
    while (!s.shouldCameraRun) s = fsm.advanceWithoutObservation(0.5f);
    feedFsm(fsm, kRelaxedInput, 60, alertCount, &s);
    assert(s.cooldownState == efd::CooldownState::NormalTracking);
    assert(s.shouldCameraRun);

    // 常態追蹤中呼叫不改變狀態，相機持續開啟
    efd::FatigueStateMachine idle(1200, 300, 300);
    efd::SystemState idleState = idle.advanceWithoutObservation(5.0f);
    assert(idleState.cooldownState == efd::CooldownState::NormalTracking && idleState.shouldCameraRun);
    std::cout << " [PASS] (冷卻 20 分關閉、快篩前 4 分關閉 / 最後 1 分採樣)\n";
}

void testExternalLandmarksConversion() {
    std::cout << "[TEST] 33. MediaPipe 正規化座標換算成像素後 EAR 不失真、臉部短暫遺失 1 秒內不判定離座...";
    // 直向 480x640 畫面中，一隻寬 60、高 18 像素的眼睛：EAR = (18 + 18) / (2 x 60) = 0.30
    const int w = 480, h = 640;
    std::vector<float> xyz(efd::kMediaPipeLandmarkCount * 3, 0.5f);
    auto setPoint = [&](int idx, float px, float py) {
        xyz[idx * 3 + 0] = px / w;
        xyz[idx * 3 + 1] = py / h;
        xyz[idx * 3 + 2] = 0.0f;
    };
    for (const auto* eye : {&efd::EyeLandmarkIndices::LEFT_EYE, &efd::EyeLandmarkIndices::RIGHT_EYE}) {
        const float cx = (eye == &efd::EyeLandmarkIndices::LEFT_EYE) ? 180.0f : 300.0f, cy = 250.0f;
        setPoint((*eye)[0], cx - 30.0f, cy);        // p1 眼角
        setPoint((*eye)[1], cx - 15.0f, cy - 9.0f); // p2
        setPoint((*eye)[2], cx + 15.0f, cy - 9.0f); // p3
        setPoint((*eye)[3], cx + 30.0f, cy);        // p4 眼角
        setPoint((*eye)[4], cx + 15.0f, cy + 9.0f); // p5
        setPoint((*eye)[5], cx - 15.0f, cy + 9.0f); // p6
    }

    efd::LandmarkDetectionResult det = efd::buildExternalDetection(xyz.data(), efd::kMediaPipeLandmarkCount, w, h, 0.1f, 0.3f);
    assert(det.hasFace && det.landmarksMeasured);
    assert(det.landmarks.size() == efd::kMediaPipeLandmarkCount);
    efd::FeatureExtractor extractor(60.0f, 0.21f);
    efd::EyeMetrics m = extractor.processFrame(det.landmarks, 15.0f);
    assert(std::abs(m.earLeft - 0.30f) < 0.001f && std::abs(m.earRight - 0.30f) < 0.001f);

    // 若直接用正規化座標，同一隻眼睛的 EAR 會被算成 0.225
    efd::LandmarkDetectionResult raw = efd::buildExternalDetection(xyz.data(), efd::kMediaPipeLandmarkCount, 1, 1);
    efd::FeatureExtractor rawExtractor(60.0f, 0.21f);
    assert(std::abs(rawExtractor.processFrame(raw.landmarks, 15.0f).earLeft - 0.225f) < 0.001f);

    efd::PresenceDiagnostic diag = efd::buildExternalDiagnostic(det, w, h);
    assert(diag.failStage.empty() && diag.rawPersonInFrame && diag.isFaceConfirmed);
    assert(diag.faceBoxW > 0 && diag.faceBoxH > 0);
    assert(std::abs(diag.eyeOpenness - 0.8f) < 0.001f);

    // 沒有臉、點數不足、數值異常都視為沒有臉
    assert(!efd::buildExternalDetection(nullptr, 0, w, h).hasFace);
    assert(!efd::buildExternalDetection(xyz.data(), 400, w, h).hasFace);
    std::vector<float> bad = xyz;
    bad[10] = std::numeric_limits<float>::quiet_NaN();
    assert(!efd::buildExternalDetection(bad.data(), efd::kMediaPipeLandmarkCount, w, h).hasFace);
    efd::LandmarkDetectionResult none = efd::buildExternalDetection(nullptr, 0, w, h);
    assert(efd::buildExternalDiagnostic(none, w, h).failStage == "model");

    // 遺失緩衝：未曾看到臉直接回報；看到後遺失未滿 1 秒不遞交，滿 1 秒才回報離座
    efd::ExternalPresenceFilter filter;
    assert(filter.shouldSubmit(false, 0));
    assert(filter.shouldSubmit(true, 100));
    assert(!filter.shouldSubmit(false, 600));
    assert(!filter.shouldSubmit(false, 1099));
    assert(filter.shouldSubmit(false, 1100));
    assert(filter.shouldSubmit(true, 1200));
    filter.reset();
    assert(filter.shouldSubmit(false, 1300));
    std::cout << " [PASS] (EAR 0.300，未換算時為 0.225)\n";
}

void testEngineSuspendsCameraDuringCooldown() {
    std::cout << "[TEST] 34. 相機省電：冷卻期間引擎關閉相機、忽略外部偵測結果，關閉省電後重新開啟...";
    std::filesystem::path dir = std::filesystem::temp_directory_path() / "efd_test_cooldown_camera";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);

    // 以存檔還原「剛提醒、冷卻中」的狀態，不必等疲勞分數爬升
    {
        efd::FatigueStateMachine fsm(1200, 300, 300);
        int alerts = 0;
        fsm.setAlertCallback([&](efd::FatigueLevel, float, const std::string&) { alerts++; });
        assert(feedFsm(fsm, kSevereInput, 150, alerts) == 1);
        const int64_t nowSec = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        std::ofstream out(dir / "efd_fsm_state.txt", std::ios::trunc);
        out << fsm.serialize(nowSec);
    }

    efd::AsyncPipelineEngine engine(efd::PlatformType::Windows);
    engine.setAutoNightlySyncEnabled(false);
    engine.setDataDirectory(dir.string());
    assert(engine.getStateMachine().getState().cooldownState == efd::CooldownState::InCooldown);
    engine.setCameraPowerSaving(true);

    std::atomic<int> suspendedTelemetry{0};
    std::atomic<int> externalWhileSuspended{0};
    engine.setTelemetryCallback([&](const efd::EngineTelemetry& t) {
        if (t.cameraSuspended) {
            suspendedTelemetry++;
            if (t.detection.landmarksMeasured) externalWhileSuspended++;
        }
    });

    engine.getCameraService().setForceSynthetic(true);
    engine.getCameraService().startSynthetic();
    assert(engine.start());

    // 第一幀處理後即進入關閉狀態
    for (int i = 0; i < 40 && !engine.isCameraSuspended(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    assert(engine.isCameraSuspended());
    // 引擎先設定關閉旗標 (之後的幀不再分析) 再停止相機服務，稍等停止完成
    for (int i = 0; i < 40 && engine.getCameraService().isRunning(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    assert(!engine.getCameraService().isRunning());

    // 相機關閉期間：外部偵測結果被忽略，遙測仍每 0.5 秒更新冷卻倒數
    efd::LandmarkDetectionResult external;
    external.hasFace = true;
    external.landmarksMeasured = true;
    external.landmarks.assign(468, efd::Point3D{});
    efd::RawFrame info;
    info.width = 480;
    info.height = 640;
    engine.submitExternalDetection(info, external);
    std::this_thread::sleep_for(std::chrono::milliseconds(1200));
    assert(suspendedTelemetry.load() >= 2);
    assert(externalWhileSuspended.load() == 0);
    assert(engine.getStateMachine().getState().cooldownState == efd::CooldownState::InCooldown);

    // 冷卻中重新校準：需要畫面，相機重新開啟且校準期間不再關閉
    engine.beginCalibration();
    for (int i = 0; i < 40 && engine.isCameraSuspended(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    assert(!engine.isCameraSuspended());
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    assert(!engine.isCameraSuspended());
    engine.finishCalibration();
    for (int i = 0; i < 40 && !engine.isCameraSuspended(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    assert(engine.isCameraSuspended());

    // 關閉此功能：下一次倒數時重新開啟相機
    engine.setCameraPowerSaving(false);
    for (int i = 0; i < 40 && engine.isCameraSuspended(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    assert(!engine.isCameraSuspended());
    assert(engine.getCameraService().isRunning());   // 引擎擁有相機 (Windows)：由引擎重新開啟
    engine.stop();

    // 相機由平台層開關 (Android)：恢復時引擎不可自行開啟 CameraService (會改用模擬相機)
    {
        efd::FatigueStateMachine fsm(1200, 300, 300);
        int alerts = 0;
        fsm.setAlertCallback([&](efd::FatigueLevel, float, const std::string&) { alerts++; });
        feedFsm(fsm, kSevereInput, 150, alerts);
        const int64_t nowSec = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        std::ofstream out(dir / "efd_fsm_state.txt", std::ios::trunc);
        out << fsm.serialize(nowSec);
    }
    efd::AsyncPipelineEngine androidEngine(efd::PlatformType::Android);
    androidEngine.setAutoNightlySyncEnabled(false);
    androidEngine.setDataDirectory(dir.string());
    androidEngine.setCameraPowerSaving(true);
    androidEngine.setExternalCameraControl(true);
    androidEngine.getCameraService().setForceSynthetic(true);
    androidEngine.getCameraService().startSynthetic();
    assert(androidEngine.start());
    for (int i = 0; i < 40 && !androidEngine.isCameraSuspended(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    assert(androidEngine.isCameraSuspended());
    androidEngine.setCameraPowerSaving(false);
    for (int i = 0; i < 40 && androidEngine.isCameraSuspended(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    assert(!androidEngine.isCameraSuspended());
    assert(!androidEngine.getCameraService().isRunning());
    androidEngine.stop();
    std::cout << " [PASS] (關閉期間遙測 " << suspendedTelemetry.load() << " 次；平台層控制時引擎不開啟模擬相機)\n";
}

void testAbsenceProbing() {
    std::cout << "[TEST] 38. 離座探測：持續沒臉時相機輪流關開，關閉時段照常累計離座，偵測到臉恢復常態...";
    std::filesystem::path dir = std::filesystem::temp_directory_path() / "efd_test_absence_probe";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);

    // 縮短時間：沒臉 1 秒就關 1 秒、開 0.5 秒 (預設 60 / 15 / 4 秒)
    efd::AsyncPipelineEngine engine(efd::PlatformType::Android);
    engine.setAutoNightlySyncEnabled(false);
    engine.setDataDirectory(dir.string());
    engine.setCameraPowerSaving(true);
    engine.setAbsenceProbing(true, 1.0f, 1.0f, 0.5f);
    engine.setExternalCameraControl(true);
    assert(engine.start());

    efd::RawFrame info;
    info.width = 480;
    info.height = 640;
    efd::LandmarkDetectionResult noFace;
    noFace.hasFace = false;
    noFace.landmarksMeasured = true;
    efd::LandmarkDetectionResult face;
    face.hasFace = true;
    face.landmarksMeasured = true;
    face.landmarks.assign(468, efd::Point3D{});

    // 相機開著時每 100 ms 送一筆偵測結果 (相機關閉時平台不會送)，回傳等到指定狀態的毫秒數
    auto feedUntil = [&](const efd::LandmarkDetectionResult& d, bool wantSuspended, int maxMs) {
        for (int t = 0; t <= maxMs; t += 100) {
            if (engine.isCameraSuspended() == wantSuspended) return t;
            if (!engine.isCameraSuspended()) engine.submitExternalDetection(info, d);
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        return -1;
    };

    const int firstOff = feedUntil(noFace, true, 4000);
    assert(firstOff >= 800);
    const int awayAtOff = engine.getStateMachine().getState().awaySeconds;
    const int offLasted = feedUntil(noFace, false, 4000);
    assert(offLasted >= 500 && offLasted <= 3000);
    // 關閉時段視為不在座：離座時間照常累計 (滿 5 分鐘即算休息)
    assert(engine.getStateMachine().getState().awaySeconds >= awayAtOff + 1);
    // 重新開啟後只看 0.5 秒，仍沒有臉就再關閉
    const int secondOff = feedUntil(noFace, true, 3000);
    assert(secondOff >= 0 && secondOff < firstOff);

    // 探測時段看到臉：恢復常態，不再關閉
    assert(feedUntil(face, false, 4000) >= 0);
    for (int i = 0; i < 25; ++i) {
        engine.submitExternalDetection(info, face);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        assert(!engine.isCameraSuspended());
    }

    // 校準期間不探測
    engine.beginCalibration();
    for (int i = 0; i < 25; ++i) {
        engine.submitExternalDetection(info, noFace);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        assert(!engine.isCameraSuspended());
    }
    engine.stop();
    std::cout << " [PASS] (沒臉 " << firstOff << " ms 後關閉，關 " << offLasted << " ms，再開 "
              << secondOff << " ms 又關閉)\n";
}

void testCameraDriversAndHal() {
    std::cout << "[TEST] 8. 測試跨平台相機硬體抽象層 (Camera HAL & Drivers)...";
    efd::CameraService cameraService(640, 480, 30.0f);

    // 1. 測試設備列舉 (包含實體與虛擬鏡頭)
    auto devices = cameraService.enumerateDevices();
    assert(!devices.empty());

    // 2. 測試 Synthetic 驅動取幀
    efd::SyntheticCameraDriver synthetic;
    std::atomic<int> frameCount{0};
    synthetic.setFrameCallback([&frameCount](const efd::RawFrame& frame) {
        if (frame.isValid()) {
            frameCount++;
        }
    });

    efd::CameraConfig config;
    config.width = 640;
    config.height = 480;
    config.fps = 30.0f;
    assert(synthetic.open(config));
    assert(synthetic.isOpened());

    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    assert(frameCount.load() > 0);
    synthetic.close();
    assert(!synthetic.isOpened());

    // 3. 測試 CameraService 整合啟動 (自動偵測 Front / 智慧回退)
    assert(cameraService.start(0, efd::CameraFacing::Front));
    assert(cameraService.isRunning());
    std::string driverName = cameraService.getActiveDriverName();
    cameraService.stop();
    assert(!cameraService.isRunning());

    std::cout << " [PASS] (驅動: " << driverName << ", 列舉到 " << devices.size() << " 個設備)\n";
}

void testDatabaseServicePersistence() {
    std::cout << "[TEST] 9. 測試本地資料持久化服務 (DatabaseService)...";
    efd::DatabaseService db("test_study_data.dat");
    db.clearAllData();
    assert(db.initialize());

    // 寫入 5 筆時序紀錄
    for (int i = 0; i < 5; ++i) {
        efd::FatigueRecord r;
        r.timestampMs = 1700000000000LL + i * 1000;
        r.subjectUuid = "SUBJ-TEST-001";
        r.ear = 0.30f - i * 0.02f;
        r.perclos = i * 0.05f;
        r.complexityIndex = 4.5f - i * 0.2f;
        r.fatigueScore = 5.0f + i * 10.0f;
        r.alertLevel = (i >= 3) ? efd::FatigueLevel::Attention : efd::FatigueLevel::Relaxed;
        assert(db.logRecord(r));
    }

    assert(db.getRecordCount() == 5);
    auto latest = db.getLatestRecords(3);
    assert(latest.size() == 3);

    std::string jsonStr = db.exportRecordsAsJson();
    assert(jsonStr.find("\"recordCount\": 5") != std::string::npos);

    std::string csvStr = db.exportRecordsAsCsv();
    assert(csvStr.find("SUBJ-TEST-001") != std::string::npos);

    // 重新載入驗證資料完整性
    efd::DatabaseService dbReload("test_study_data.dat");
    assert(dbReload.initialize());
    assert(dbReload.getRecordCount() == 5);

    db.clearAllData();
    std::cout << " [PASS] (成功驗證 WAL 事務落盤、導出與重載, 記錄數=5)\n";
}

void testStudyWorkflowTrackerAndGatekeeper() {
    std::cout << "[TEST] 10. 測試 14 天科研週期追蹤與門禁事務解鎖 (StudyWorkflowTracker)...";
    efd::StudyWorkflowTracker tracker("test_study_config.json");
    tracker.resetStudy("SUBJ-TEST-999");
    assert(tracker.initialize());
    assert(tracker.getSubjectUuid() == "SUBJ-TEST-999");
    assert(tracker.getCurrentDay() == 1);
    assert(tracker.getStatus() == efd::StudyStatus::ActiveMonitoring);
    assert(tracker.getLastSyncedDate() == 0);
    tracker.setLastSyncedDate(20261011);
    assert(tracker.getLastSyncedDate() == 20261011);
    assert(tracker.getStartDate() > 0);

    // 模擬實驗推進至第 2 天與第 13 天
    tracker.setTimeWarpDay(2);
    assert(tracker.getCurrentDay() == 2);
    tracker.setTimeWarpDay(13);
    assert(tracker.getCurrentDay() == 13);
    assert(tracker.getStatus() == efd::StudyStatus::ActiveMonitoring);

    // 推進至第 14 天 -> 觸發期滿門禁 (資產 5.png)
    tracker.advanceDay();
    assert(tracker.getCurrentDay() == 14);
    assert(tracker.getStatus() == efd::StudyStatus::LockedForPostTest);

    // 提交後測問卷 -> 驗證事務型解鎖 (資產 6.png)
    std::string token = tracker.submitQuestionnaire("Q1:Yes,Q2:5,Q3:Safe");
    assert(!token.empty());
    assert(tracker.getStatus() == efd::StudyStatus::CompletedUnlocked);
    assert(tracker.getUnlockToken() == token);

    std::remove("test_study_config.json");
    std::cout << " [PASS] (門禁觸發正常, 解鎖 Token: " << token << ")\n";
}

void testNetworkSyncWorker() {
    std::cout << "[TEST] 11. 測試科研雲端非同步同步與門禁憑證生成 (NetworkSyncWorker)...";
    efd::DatabaseService db("test_sync_db.dat");
    db.clearAllData();
    assert(db.initialize());

    // 寫入 3 筆測試資料
    for (int i = 0; i < 3; ++i) {
        efd::FatigueRecord r;
        r.timestampMs = 1700000000000LL + i * 1000;
        r.subjectUuid = "SUBJ-SYNC-101";
        r.ear = 0.310f - i * 0.01f;
        r.fatigueScore = 12.0f;
        db.logRecord(r);
    }
    db.flush();

    // 測試 Checksum 計算
    std::string testData = "EFD-RESEARCH-DATA-PAYLOAD";
    std::string checksum1 = efd::NetworkSyncWorker::calculateChecksum(testData);
    std::string checksum2 = efd::NetworkSyncWorker::calculateChecksum(testData);
    assert(!checksum1.empty());
    assert(checksum1 == checksum2);

    efd::NetworkSyncWorker worker(db, "https://api.efd-research.org/v1/sync");
    assert(worker.getStatus() == efd::SyncStatus::Idle);
    assert(worker.getEndpointUrl() == "https://api.efd-research.org/v1/sync");
    // 單元測試不連真實網路：以假上傳函式模擬伺服器成功回應
    worker.setUploader([](const std::string&, const std::string& body, std::string& resp) {
        assert(body.find("SUBJ-SYNC-101") != std::string::npos);
        resp = "{\"status\":\"success\"}";
        return true;
    });
    worker.setDumpPath((std::filesystem::temp_directory_path() / "efd_test_sync_dump.json").string());

    std::atomic<bool> callbackFired{false};
    efd::SyncResult receivedResult;

    bool triggered = worker.triggerSync("SUBJ-SYNC-101", 14, "Q1:5,Q2:VerySatisfied", [&](const efd::SyncResult& res) {
        receivedResult = res;
        callbackFired = true;
    });

    assert(triggered);
    
    // 等待非同步背景任務完成 (上限 2 秒)
    for (int i = 0; i < 200 && !callbackFired.load(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    assert(callbackFired.load());
    assert(receivedResult.success);
    assert(receivedResult.httpStatusCode == 200);
    assert(receivedResult.unlockToken.empty()); // 未填寫後測問卷前不產生假憑證
    assert(worker.getStatus() == efd::SyncStatus::Success);
    assert(worker.getLatestUnlockToken() == receivedResult.unlockToken);

    db.clearAllData();
    std::remove("test_sync_db.dat");
    std::cout << " [PASS] (同步成功, Checksum=" << checksum1 << ")\n";
}

// 以 1 秒步進餵入持續高分，回傳第一次提醒發生在第幾秒 (未提醒回傳 -1)
static int secondsUntilFirstAlert(int sensitivity, int maxSeconds) {
    efd::FatigueStateMachine fsm(1200, 300, 300);
    fsm.setSensitivity(sensitivity);
    int alertAt = -1;
    int t = 0;
    fsm.setAlertCallback([&](efd::FatigueLevel, float, const std::string&) {
        if (alertAt < 0) alertAt = t;
    });
    for (t = 1; t <= maxSeconds && alertAt < 0; ++t) {
        fsm.update(kSevereInput.present, kSevereInput.perclos, kSevereInput.blink, kSevereInput.ci, 1.0f);
    }
    return alertAt;
}

void testSensitivityLevels() {
    std::cout << "[TEST] 20. 測試提示靈敏度 (只調整門檻與持續秒數，且會保存)...";
    int high = secondsUntilFirstAlert(efd::FatigueStateMachine::kSensitivityHigh, 600);
    int standard = secondsUntilFirstAlert(efd::FatigueStateMachine::kSensitivityStandard, 600);
    int low = secondsUntilFirstAlert(efd::FatigueStateMachine::kSensitivityLow, 600);
    assert(high > 0 && standard > 0 && low > 0);
    assert(high < standard && standard < low);

    // 超出範圍視為標準
    efd::FatigueStateMachine fsm;
    fsm.setSensitivity(7);
    assert(fsm.getSensitivity() == efd::FatigueStateMachine::kSensitivityStandard);
    assert(fsm.getSevereThreshold() == efd::FatigueStateMachine::kSevereThreshold);

    // 設定保存在設定檔，重新開啟引擎後自動套用
    std::string dir = makeTestDir("efd_test_sensitivity");
    {
        efd::AsyncPipelineEngine engine(efd::PlatformType::Windows);
        engine.setDataDirectory(dir);
        assert(engine.getSensitivity() == efd::FatigueStateMachine::kSensitivityStandard);
        engine.setSensitivity(efd::FatigueStateMachine::kSensitivityHigh);
    }
    {
        efd::AsyncPipelineEngine engine(efd::PlatformType::Windows);
        engine.setDataDirectory(dir);
        assert(engine.getSensitivity() == efd::FatigueStateMachine::kSensitivityHigh);
        assert(engine.getStateMachine().getSevereThreshold() == 58.0f);
    }
    std::filesystem::remove_all(dir);
    std::cout << " [PASS] (首次提醒: 高=" << high << "s, 標準=" << standard << "s, 低=" << low << "s)\n";
}

void testPayloadJsonUsesRealValues() {
    std::cout << "[TEST] 21. 測試上傳 JSON 不含寫死的假值...";
    efd::PayloadHeader header;
    header.subjectUuid = "SUBJ-JSON";
    header.studyDay = 3;
    header.studyStatus = efd::StudyWorkflowTracker::statusLabel(efd::StudyStatus::LockedForPostTest);

    // 無紀錄：指標為 null、筆數為 0、沒有憑證
    std::string emptyChecksum;
    std::string empty = efd::NetworkSyncWorker::buildPayloadJson(header, {}, false, &emptyChecksum);
    assert(empty.find("\"ear_mean\": null") != std::string::npos);
    assert(empty.find("\"blink_rate\": null") != std::string::npos);
    assert(empty.find("\"mse_ci\": null") != std::string::npos);
    assert(empty.find("\"fatigue_score\": null") != std::string::npos);
    assert(empty.find("\"record_count\": 0") != std::string::npos);
    assert(empty.find("\"token\": \"\"") != std::string::npos);
    assert(empty.find("\"status\": \"Locked\"") != std::string::npos);
    assert(empty.find("0.312") == std::string::npos);
    assert(empty.find("16.0") == std::string::npos);
    assert(empty.find("Normal") == std::string::npos);
    assert(empty.find("\"records\"") == std::string::npos);

    // 有紀錄：眨眼率取實際平均 (0 代表尚未累積，不納入)
    std::vector<efd::FatigueRecord> records(3);
    const float blinks[3] = {12.0f, 18.0f, 0.0f};
    for (int i = 0; i < 3; ++i) {
        records[i].timestampMs = 1700000000000LL + i * 1000;
        records[i].subjectUuid = "SUBJ-JSON";
        records[i].ear = 0.30f;
        records[i].perclos = 0.05f;
        records[i].complexityIndex = 3.0f;
        records[i].fatigueScore = 20.0f;
        records[i].blinkRatePerMin = blinks[i];
    }
    header.unlockToken = "EFD-14D-1234-5678";
    std::string checksum;
    std::string json = efd::NetworkSyncWorker::buildPayloadJson(header, records, true, &checksum);
    assert(json.find("\"blink_rate\": 15.00") != std::string::npos);
    assert(json.find("\"ear_mean\": 0.3000") != std::string::npos);
    assert(json.find("\"perclos\": 5.00") != std::string::npos);
    assert(json.find("\"record_count\": 3") != std::string::npos);
    assert(json.find("\"token\": \"EFD-14D-1234-5678\"") != std::string::npos);
    assert(json.find("\"records\": [") != std::string::npos);
    assert(json.find("\"checksum\": \"" + checksum + "\"") != std::string::npos);
    assert(checksum != emptyChecksum);
    std::cout << " [PASS]\n";
}

void testDatabaseBlinkColumnBackwardCompatible() {
    std::cout << "[TEST] 22. 測試資料庫新增眨眼率欄位且相容舊版 7 欄資料...";
    std::string dir = makeTestDir("efd_test_db_blink");
    std::string db = (std::filesystem::path(dir) / "db.dat").string();
    {
        std::ofstream out(db);
        out << "TimestampMs,SubjectUUID,EAR,PERCLOS,ComplexityIndex,FatigueScore,AlertLevel\n"
            << "1700000000000,S,0.3,0.01,4.5,5,0\n"          // 舊版 7 欄
            << "1700000001000,S,0.3,0.01,4.5,5,0,14.5\n";    // 新版 8 欄
    }
    {
        efd::DatabaseService dbs(db);
        assert(dbs.initialize());
        auto records = dbs.getLatestRecords(10);
        assert(records.size() == 2);
        assert(records[0].blinkRatePerMin == 0.0f);
        assert(std::fabs(records[1].blinkRatePerMin - 14.5f) < 0.001f);

        efd::FatigueRecord r;
        r.timestampMs = 1700000002000LL;
        r.subjectUuid = "S";
        r.blinkRatePerMin = 17.0f;
        dbs.logRecord(r); // 只寫入 WAL
    }
    {
        efd::DatabaseService dbs(db);
        assert(dbs.initialize());
        auto records = dbs.getLatestRecords(10);
        assert(records.size() == 3);
        assert(std::fabs(records[2].blinkRatePerMin - 17.0f) < 0.001f);
    }
    std::filesystem::remove_all(dir);
    std::cout << " [PASS]\n";
}

void testFloatingIndicatorAndSystemTray() {
    std::cout << "[TEST] 12. 測試桌面置頂懸浮指標 HUD 與 Windows 系統托盤 (UI & Tray)...";
#ifdef _WIN32
    // 1. 測試懸浮指標物件
    efd::FatigueFloatingIndicator indicator(168, 56);
    assert(!indicator.isVisible());

    bool restoreCalled = false;
    indicator.setRestoreCallback([&]() {
        restoreCalled = true;
    });

    // 模擬遙測數據更新 (綠色 Relaxed -> 黃色 Attention -> 紅色 SevereWarning)
    indicator.updateMetrics(0.312f, 8.5f, efd::FatigueLevel::Relaxed, "Optimal");
    indicator.updateMetrics(0.240f, 45.0f, efd::FatigueLevel::Attention, "Attention");
    indicator.updateMetrics(0.180f, 85.0f, efd::FatigueLevel::SevereWarning, "Severe Alert");

    // 2. 測試系統托盤管理器
    efd::SystemTrayManager tray;
    assert(!tray.isInitialized());

    bool actionDispatched = false;
    efd::TrayMenuAction receivedAction = efd::TrayMenuAction::ShowMainWindow;
    tray.setActionCallback([&](efd::TrayMenuAction action) {
        actionDispatched = true;
        receivedAction = action;
    });

    // 建立 message-only 測試視窗以驗證 Tray 初始化與訊息響應
    WNDCLASSEXW wc = { sizeof(WNDCLASSEXW) };
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.lpszClassName = L"EFD_TestTrayHostClass";
    RegisterClassExW(&wc);

    HWND testHwnd = CreateWindowExW(0, wc.lpszClassName, L"TestTrayHost", 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, wc.hInstance, NULL);
    if (testHwnd) {
        bool inited = tray.initialize(testHwnd, L"EFD 測試托盤");
        if (inited) {
            assert(tray.isInitialized());
            tray.updateStatus(efd::FatigueLevel::Attention, 45.0f, L"注意疲勞");
            tray.showBalloonNotification(L"EFD 警報", L"請適當休息", efd::FatigueLevel::Attention);
            
            // 模擬托盤雙擊訊息
            tray.handleTrayMessage(WM_LBUTTONDBLCLK);
            assert(actionDispatched);
            assert(receivedAction == efd::TrayMenuAction::ShowMainWindow);

            tray.removeIcon();
            assert(!tray.isInitialized());
        }
        DestroyWindow(testHwnd);
    }
    UnregisterClassW(wc.lpszClassName, wc.hInstance);

    std::cout << " [PASS] (HUD 數據流與 SystemTray 訊息分發正常)\n";
#else
    std::cout << " [PASS] (非 Windows 平台跳過 Win32 托盤驗證)\n";
#endif
}

int main() {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif

    std::cout << "========================================================\n";
    std::cout << "  EFD 跨平台核心與相機驅動 (Camera HAL) 整合單元測試\n";
    std::cout << "  - 5-Thread Pipeline Engine\n";
    std::cout << "  - Full 20/5/5 State Machine & Escalation\n";
    std::cout << "  - Platform Lifecycle (Sleep/Wake/Hot-Resume)\n";
    std::cout << "  - Camera HAL & Drivers (WMF / Camera2 / Synthetic)\n";
    std::cout << "  - Local SQLite/WAL Persistent Storage (Phase 3)\n";
    std::cout << "  - 14-Day Study Workflow & Gatekeeper Unlock (Phase 3)\n";
    std::cout << "  - Asynchronous Cloud Sync & Checksum (Phase 4)\n";
    std::cout << "  - Stay-On-Top Floating Indicator & Tray (Phase 4)\n";
    std::cout << "========================================================\n";

    testEarCalculation();
    testAdaptiveBaseline();
    testMseComplexity();
    testEmdDecomposition();
    testFatigueStateMachineFullCycle();
    testPlatformLifecycleAdapter();
    testAsyncPipelineEngine();
    testCameraDriversAndHal();
    testDatabaseServicePersistence();
    testStudyWorkflowTrackerAndGatekeeper();
    testNetworkSyncWorker();
    testFloatingIndicatorAndSystemTray();
    testFatigueStateMachinePersistence();
    testTimeBasedFeatureWindow();
    testCalibrationBoundsAndRestore();
    testEngineCalibrationAndDataDirectory();
    testCorruptedConfigDoesNotCrash();
    testNightlySyncMarksOnlyOnSuccess();
    testPresenceRejectsUniformSkinColoredObject();
    testSensitivityLevels();
    testPayloadJsonUsesRealValues();
    testDatabaseBlinkColumnBackwardCompatible();
    testFaceDetectedAboveSkinColoredShirt();
    testFaceSeparatedFromLargerSkinColoredShirt();
    testWarmLitWallIsNotSkin();
    testClosedEyesStillPresentAfterLock();
    testCameraPixelFormatConversion();
    testFaceFillingFrameNeedsBothEyes();
    testOpennessRobustToMeasurementNoise();
    testLandmarkDetectorInjectionAndExternalDetection();
    testAdvanceWithoutObservationKeepsCooldown();
    testExternalLandmarksConversion();
    testEngineSuspendsCameraDuringCooldown();
    testOnnxFaceLandmarkerMatchesMediaPipe();
    testRollingOpenBaselineP80();
    testCrossoverGroupsAndStudyLog();
    testAbsenceProbing();

    std::cout << "\n[ALL TESTS PASSED] 全部 38 項測試順利通過！\n";
    return 0;
}
