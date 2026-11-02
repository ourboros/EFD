#include "storage/StudyDataLog.hpp"

#include <cctype>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace efd {

namespace {

// UTF-8 路徑 (Windows 使用者名稱可能含中文)
std::filesystem::path toPath(const std::string& utf8) {
    return std::filesystem::path(std::u8string(utf8.begin(), utf8.end()));
}

std::vector<std::string> splitCsv(const std::string& line) {
    std::vector<std::string> out;
    std::stringstream ss(line);
    std::string cell;
    while (std::getline(ss, cell, ',')) out.push_back(cell);
    // getline 不會產生最後一個空欄位 (例如版本號為空)
    if (!line.empty() && line.back() == ',') out.emplace_back();
    return out;
}

// 只接受英數與少數符號，避免 CSV 與 JSON 被欄位內容破壞
std::string safeToken(const std::string& s) {
    std::string out;
    for (char c : s) {
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '.' || c == '-' || c == '_') out += c;
    }
    return out;
}

void appendLine(const std::string& path, const std::string& header, const std::string& line) {
    const bool exists = std::filesystem::exists(toPath(path));
    std::ofstream out(toPath(path), std::ios::app);
    if (!out.is_open()) return;
    if (!exists) out << header << "\n";
    out << line << "\n";
}

} // namespace

void StudyDataLog::setDirectory(const std::string& directory) {
    std::lock_guard<std::mutex> lock(m_mutex);
    const std::string prefix = directory.empty() ? "" : directory + "/";
    m_minutePath = prefix + kMinuteFile;
    m_surveyPath = prefix + kSurveyFile;
}

void StudyDataLog::appendMinute(const MinuteSummary& m) {
    std::ostringstream line;
    line << std::fixed << m.minuteMs << "," << m.studyDay << "," << safeToken(m.group) << ","
         << (m.alertsEnabled ? 1 : 0) << ","
         << std::setprecision(1) << m.presentSec << "," << m.cameraOnSec << ","
         << std::setprecision(4) << m.earMean << "," << m.earP10 << "," << m.perclos << ","
         << m.blinkCount << "," << std::setprecision(0) << m.blinkDurMs << ","
         << std::setprecision(3) << m.ci << "," << std::setprecision(1) << m.score << ","
         << m.cooldownState << "," << safeToken(m.alert) << "," << safeToken(m.detector) << ","
         << safeToken(m.platform) << "," << safeToken(m.appVersion);
    std::lock_guard<std::mutex> lock(m_mutex);
    appendLine(m_minutePath,
               "minute,study_day,group,alerts_enabled,present_sec,camera_on_sec,ear_mean,ear_p10,perclos,"
               "blink_count,blink_dur_ms,ci,score,cooldown_state,alert,detector,platform,app_version",
               line.str());
}

void StudyDataLog::appendSurvey(const SurveyResponse& s) {
    std::ostringstream line;
    line << s.timeMs << "," << s.studyDay << "," << safeToken(s.group) << "," << (s.alertsEnabled ? 1 : 0) << ","
         << s.kss << "," << s.eyeFatigue << "," << std::fixed << std::setprecision(1) << s.responseSec << ","
         << (s.skipped ? 1 : 0);
    std::lock_guard<std::mutex> lock(m_mutex);
    appendLine(m_surveyPath, "time,study_day,group,alerts_enabled,kss,eye_fatigue,response_sec,skipped", line.str());
}

std::vector<MinuteSummary> StudyDataLog::readMinutesSince(int64_t sinceMs) const {
    std::vector<MinuteSummary> out;
    std::lock_guard<std::mutex> lock(m_mutex);
    std::ifstream in(toPath(m_minutePath));
    std::string line;
    std::getline(in, line);   // 標題列
    while (std::getline(in, line)) {
        const auto c = splitCsv(line);
        if (c.size() < 18) continue;
        try {
            MinuteSummary m;
            m.minuteMs = std::stoll(c[0]);
            if (m.minuteMs < sinceMs) continue;
            m.studyDay = std::stoi(c[1]);
            m.group = c[2];
            m.alertsEnabled = c[3] == "1";
            m.presentSec = std::stof(c[4]);
            m.cameraOnSec = std::stof(c[5]);
            m.earMean = std::stof(c[6]);
            m.earP10 = std::stof(c[7]);
            m.perclos = std::stof(c[8]);
            m.blinkCount = std::stoi(c[9]);
            m.blinkDurMs = std::stof(c[10]);
            m.ci = std::stof(c[11]);
            m.score = std::stof(c[12]);
            m.cooldownState = std::stoi(c[13]);
            m.alert = c[14];
            m.detector = c[15];
            m.platform = c[16];
            m.appVersion = c[17];
            out.push_back(m);
        } catch (const std::exception&) {
            // 損毀的列略過
        }
    }
    return out;
}

std::vector<SurveyResponse> StudyDataLog::readSurveysSince(int64_t sinceMs) const {
    std::vector<SurveyResponse> out;
    std::lock_guard<std::mutex> lock(m_mutex);
    std::ifstream in(toPath(m_surveyPath));
    std::string line;
    std::getline(in, line);
    while (std::getline(in, line)) {
        const auto c = splitCsv(line);
        if (c.size() < 8) continue;
        try {
            SurveyResponse s;
            s.timeMs = std::stoll(c[0]);
            if (s.timeMs < sinceMs) continue;
            s.studyDay = std::stoi(c[1]);
            s.group = c[2];
            s.alertsEnabled = c[3] == "1";
            s.kss = std::stoi(c[4]);
            s.eyeFatigue = std::stoi(c[5]);
            s.responseSec = std::stof(c[6]);
            s.skipped = c[7] == "1";
            out.push_back(s);
        } catch (const std::exception&) {
        }
    }
    return out;
}

std::string StudyDataLog::buildUploadJsonFields(const std::string& subjectId, int64_t sinceMs) const {
    const std::string subject = safeToken(subjectId);
    std::ostringstream json;
    json << std::fixed;
    json << "\"minute_summaries\": [";
    bool first = true;
    for (const auto& m : readMinutesSince(sinceMs)) {
        json << (first ? "\n" : ",\n") << "    {\"minute\": " << m.minuteMs
             << ", \"subject_id\": \"" << subject << "\", \"study_day\": " << m.studyDay
             << ", \"group\": \"" << safeToken(m.group) << "\", \"alerts_enabled\": " << (m.alertsEnabled ? "true" : "false")
             << std::setprecision(1) << ", \"present_sec\": " << m.presentSec << ", \"camera_on_sec\": " << m.cameraOnSec
             << std::setprecision(4) << ", \"ear_mean\": " << m.earMean << ", \"ear_p10\": " << m.earP10
             << ", \"perclos\": " << m.perclos << ", \"blink_count\": " << m.blinkCount
             << std::setprecision(0) << ", \"blink_dur_ms\": " << m.blinkDurMs
             << std::setprecision(3) << ", \"ci\": " << m.ci << std::setprecision(1) << ", \"score\": " << m.score
             << ", \"cooldown_state\": " << m.cooldownState << ", \"alert\": \"" << safeToken(m.alert)
             << "\", \"detector\": \"" << safeToken(m.detector) << "\", \"platform\": \"" << safeToken(m.platform)
             << "\", \"app_version\": \"" << safeToken(m.appVersion) << "\"}";
        first = false;
    }
    json << (first ? "]" : "\n  ]") << ",\n  \"kss_responses\": [";
    first = true;
    for (const auto& s : readSurveysSince(sinceMs)) {
        json << (first ? "\n" : ",\n") << "    {\"time\": " << s.timeMs << ", \"subject_id\": \"" << subject
             << "\", \"study_day\": " << s.studyDay << ", \"group\": \"" << safeToken(s.group)
             << "\", \"alerts_enabled\": " << (s.alertsEnabled ? "true" : "false") << ", \"kss\": ";
        if (s.kss >= 0) json << s.kss; else json << "null";
        json << ", \"eye_fatigue\": ";
        if (s.eyeFatigue >= 0) json << s.eyeFatigue; else json << "null";
        json << std::setprecision(1) << ", \"response_sec\": " << s.responseSec
             << ", \"skipped\": " << (s.skipped ? "true" : "false") << "}";
        first = false;
    }
    json << (first ? "]" : "\n  ]");
    return json.str();
}

} // namespace efd
