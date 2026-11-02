#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace efd {

// 每分鐘摘要：欄位與 Apps Script 的 MinuteSummary 工作表一致 (subject_id 於上傳時加入)
struct MinuteSummary {
    int64_t minuteMs = 0;          // 該分鐘開始的 epoch 毫秒
    int studyDay = 1;
    std::string group;             // "A" / "B" / "" (未分組)
    bool alertsEnabled = true;
    float presentSec = 0.0f;       // 偵測到臉的秒數
    float cameraOnSec = 0.0f;      // 相機開啟 (有在分析) 的秒數
    float earMean = 0.0f;
    float earP10 = 0.0f;
    float perclos = 0.0f;
    int blinkCount = 0;
    float blinkDurMs = 0.0f;       // 本分鐘完成的眨眼平均時長
    float ci = 0.0f;
    float score = 0.0f;            // 平滑疲勞分數 (分鐘結束時)
    int cooldownState = 0;         // 0 常態 / 1 冷卻 / 2 快篩
    std::string alert = "none";    // none / shown / suppressed
    std::string detector;          // mediapipe / onnx / heuristic
    std::string platform;          // Android / Windows
    std::string appVersion;
};

// 提醒時的眼睛疲勞自評 (0 完全不累 ~ 10 非常疲勞)；kss 保留欄位，目前不詢問 (-1)
struct SurveyResponse {
    int64_t timeMs = 0;            // 提醒出現的時間
    int studyDay = 1;
    std::string group;
    bool alertsEnabled = true;
    int kss = -1;
    int eyeFatigue = -1;           // -1 = 略過或逾時
    float responseSec = 0.0f;
    bool skipped = false;
};

// 研究資料的本機紀錄 (只有數值，不含影像或特徵點座標)：每分鐘摘要與自評各存一個 CSV 檔，
// 每日上傳時把最近幾天的資料轉成 JSON 陣列一併送出 (Apps Script 會略過已寫入的時間點)
class StudyDataLog {
public:
    static constexpr const char* kMinuteFile = "efd_minute_summary.csv";
    static constexpr const char* kSurveyFile = "efd_eye_fatigue.csv";

    void setDirectory(const std::string& directory);

    void appendMinute(const MinuteSummary& m);
    void appendSurvey(const SurveyResponse& s);

    std::vector<MinuteSummary> readMinutesSince(int64_t sinceMs) const;
    std::vector<SurveyResponse> readSurveysSince(int64_t sinceMs) const;

    // "minute_summaries": [...], "kss_responses": [...]  (不含外層大括號，供上傳 JSON 直接嵌入)
    std::string buildUploadJsonFields(const std::string& subjectId, int64_t sinceMs) const;

private:
    std::string m_minutePath = kMinuteFile;
    std::string m_surveyPath = kSurveyFile;
    mutable std::mutex m_mutex;
};

} // namespace efd
