#include "StudyWorkflowTracker.hpp"
#include <fstream>
#include <sstream>
#include <iomanip>
#include <random>
#include <iostream>
#include <algorithm>

namespace efd {

StudyWorkflowTracker::StudyWorkflowTracker(const std::string& configPath)
    : m_configPath(configPath) {
}

int StudyWorkflowTracker::toJdn(int y, int m, int d) {
    int a = (14 - m) / 12;
    int y_adj = y + 4800 - a;
    int m_adj = m + 12 * a - 3;
    return d + (153 * m_adj + 2) / 5 + 365 * y_adj + y_adj / 4 - y_adj / 100 + y_adj / 400 - 32045;
}

int StudyWorkflowTracker::fromJdn(int jdn) {
    int l = jdn + 68569;
    int n = (4 * l) / 146097;
    l = l - (146097 * n + 3) / 4;
    int i = (4000 * (l + 1)) / 1461001;
    l = l - (1461 * i) / 4 + 31;
    int j = (80 * l) / 2447;
    int d = l - (2447 * j) / 80;
    l = j / 11;
    int m = j + 2 - (12 * l);
    int y = 100 * (n - 49) + i + l;
    return y * 10000 + m * 100 + d;
}

int StudyWorkflowTracker::getTodayDateInt() {
    auto nowTime = std::chrono::system_clock::now();
    std::time_t nowT = std::chrono::system_clock::to_time_t(nowTime);
    std::tm tmNow{};
#ifdef _WIN32
    localtime_s(&tmNow, &nowT);
#else
    localtime_r(&nowT, &tmNow);
#endif
    return (tmNow.tm_year + 1900) * 10000 + (tmNow.tm_mon + 1) * 100 + tmNow.tm_mday;
}

int StudyWorkflowTracker::refreshStudyProgressUnlocked() {
    int todayDate = getTodayDateInt();
    if (m_startDate <= 0) {
        if (m_studyStartTimestamp > 0) {
            std::time_t startT = static_cast<std::time_t>(m_studyStartTimestamp);
            std::tm tmStart{};
#ifdef _WIN32
            localtime_s(&tmStart, &startT);
#else
            localtime_r(&startT, &tmStart);
#endif
            m_startDate = (tmStart.tm_year + 1900) * 10000 + (tmStart.tm_mon + 1) * 100 + tmStart.tm_mday;
        } else {
            m_startDate = todayDate;
            m_studyStartTimestamp = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
        }
    }

    int startY = m_startDate / 10000;
    int startM = (m_startDate % 10000) / 100;
    int startD = m_startDate % 100;

    int todayY = todayDate / 10000;
    int todayM = (todayDate % 10000) / 100;
    int todayD = todayDate % 100;

    int jdnStart = toJdn(startY, startM, startD);
    int jdnToday = toJdn(todayY, todayM, todayD);
    int elapsedDays = jdnToday - jdnStart;
    if (elapsedDays < 0) elapsedDays = 0;

    int calculatedDay = std::clamp(elapsedDays + 1, 1, 14);

    bool changed = false;
    if (m_status == StudyStatus::ActiveMonitoring) {
        if (m_currentDay != calculatedDay) {
            m_currentDay = calculatedDay;
            changed = true;
        }
        if (m_currentDay >= 14) {
            m_status = StudyStatus::LockedForPostTest;
            changed = true;
        }
    } else if (m_status == StudyStatus::LockedForPostTest) {
        if (m_currentDay != 14) {
            m_currentDay = 14;
            changed = true;
        }
    }

    if (changed) {
        saveConfigUnlocked();
    }
    return m_currentDay;
}

int StudyWorkflowTracker::refreshStudyProgress() {
    std::lock_guard<std::mutex> lock(m_mutex);
    return refreshStudyProgressUnlocked();
}

void StudyWorkflowTracker::setConfigPath(const std::string& path) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!path.empty()) {
        // 切換到新路徑時清掉舊路徑載入的內容，避免沿用預設路徑產生的 UUID
        resetInMemoryUnlocked();
        m_configPath = path;
        initializeUnlocked();
    }
}

void StudyWorkflowTracker::resetInMemoryUnlocked() {
    m_subjectUuid.clear();
    m_currentDay = 1;
    m_status = StudyStatus::ActiveMonitoring;
    m_unlockToken.clear();
    m_studyStartTimestamp = 0;
    m_startDate = 0;
    m_launchCount = 0;
    m_hasCompletedInitialSetup = false;
    m_savedBaselineEar = 0.0f;
    m_savedEarStdDev = 0.0f;
    m_savedThresholdEar = 0.0f;
    m_cloudSyncEndpoint.clear();
    m_lastSyncedDate = 0;
    m_sensitivityLevel = 1;
}

bool StudyWorkflowTracker::initialize() {
    std::lock_guard<std::mutex> lock(m_mutex);
    return initializeUnlocked();
}

bool StudyWorkflowTracker::initializeUnlocked() {
    loadConfigUnlocked();
    int64_t nowSec = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    if (m_subjectUuid.empty()) {
        m_subjectUuid = generateRandomUuid();
        m_studyStartTimestamp = nowSec;
        m_startDate = getTodayDateInt();
        m_currentDay = 1;
        m_status = StudyStatus::ActiveMonitoring;
    } else if (m_studyStartTimestamp <= 0) {
        m_studyStartTimestamp = nowSec;
        m_startDate = getTodayDateInt();
        m_currentDay = 1;
        m_status = StudyStatus::ActiveMonitoring;
    } else {
        refreshStudyProgressUnlocked();
    }
    if (m_cloudSyncEndpoint.empty() || m_cloudSyncEndpoint.find("AKfycb...") != std::string::npos || m_cloudSyncEndpoint.find("AKfycbyGXGJz") != std::string::npos || m_cloudSyncEndpoint.find("AKfycbxWOj") != std::string::npos) {
        m_cloudSyncEndpoint = kDefaultCloudSyncEndpoint;
    }
    m_launchCount++;
    saveConfigUnlocked();
    return true;
}

std::string StudyWorkflowTracker::getSubjectUuid() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_subjectUuid;
}

StudyStatus StudyWorkflowTracker::getStatus() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_status;
}

int StudyWorkflowTracker::getCurrentDay() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    const_cast<StudyWorkflowTracker*>(this)->refreshStudyProgressUnlocked();
    return m_currentDay;
}

float StudyWorkflowTracker::getStudyProgressPercent() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return std::clamp(static_cast<float>(m_currentDay) / 14.0f * 100.0f, 0.0f, 100.0f);
}

std::string StudyWorkflowTracker::getUnlockToken() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_unlockToken;
}

int StudyWorkflowTracker::getLaunchCount() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_launchCount;
}

bool StudyWorkflowTracker::isSecondOrSubsequentLaunch() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_launchCount >= 2;
}

bool StudyWorkflowTracker::isInitialSetupCompleted() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_hasCompletedInitialSetup;
}

void StudyWorkflowTracker::setInitialSetupCompleted(bool completed) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_hasCompletedInitialSetup = completed;
    saveConfigUnlocked();
}

void StudyWorkflowTracker::markInitialSetupCompleted(const CalibrationData& calibration) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_hasCompletedInitialSetup = true;
    m_savedBaselineEar = calibration.baselineEar;
    m_savedEarStdDev = calibration.earStdDev;
    m_savedThresholdEar = calibration.thresholdEar;
    saveConfigUnlocked();
}

CalibrationData StudyWorkflowTracker::getSavedCalibration() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    CalibrationData data;
    data.baselineEar = m_savedBaselineEar;
    data.earStdDev = m_savedEarStdDev;
    data.thresholdEar = m_savedThresholdEar;
    data.isCalibrated = m_hasCompletedInitialSetup && m_savedBaselineEar > 0.0f && m_savedThresholdEar > 0.0f;
    return data;
}

std::string StudyWorkflowTracker::getCloudSyncEndpoint() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_cloudSyncEndpoint;
}

void StudyWorkflowTracker::setCloudSyncEndpoint(const std::string& endpoint) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_cloudSyncEndpoint = endpoint;
    saveConfigUnlocked();
}

void StudyWorkflowTracker::setTimeWarpDay(int day) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_currentDay = std::clamp(day, 1, 14);
    int todayDate = getTodayDateInt();
    int todayY = todayDate / 10000;
    int todayM = (todayDate % 10000) / 100;
    int todayD = todayDate % 100;
    int jdnToday = toJdn(todayY, todayM, todayD);
    int targetStartJdn = jdnToday - (m_currentDay - 1);
    m_startDate = fromJdn(targetStartJdn);

    int64_t nowSec = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    m_studyStartTimestamp = nowSec - static_cast<int64_t>(m_currentDay - 1) * 86400;
    if (m_currentDay >= 14 && m_status == StudyStatus::ActiveMonitoring) {
        m_status = StudyStatus::LockedForPostTest;
    }
    saveConfigUnlocked();
}

void StudyWorkflowTracker::advanceDay() {
    std::lock_guard<std::mutex> lock(m_mutex);
    int targetDay = m_currentDay < 14 ? m_currentDay + 1 : 14;
    m_currentDay = targetDay;
    int todayDate = getTodayDateInt();
    int todayY = todayDate / 10000;
    int todayM = (todayDate % 10000) / 100;
    int todayD = todayDate % 100;
    int jdnToday = toJdn(todayY, todayM, todayD);
    int targetStartJdn = jdnToday - (m_currentDay - 1);
    m_startDate = fromJdn(targetStartJdn);

    int64_t nowSec = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    m_studyStartTimestamp = nowSec - static_cast<int64_t>(m_currentDay - 1) * 86400;
    if (m_currentDay >= 14 && m_status == StudyStatus::ActiveMonitoring) {
        m_status = StudyStatus::LockedForPostTest;
    }
    saveConfigUnlocked();
}

void StudyWorkflowTracker::triggerPostStudyLock() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_currentDay = 14;
    m_status = StudyStatus::LockedForPostTest;
    saveConfigUnlocked();
}

std::string StudyWorkflowTracker::submitQuestionnaire(const std::string& questionnairePayload) {
    (void)questionnairePayload;
    std::lock_guard<std::mutex> lock(m_mutex);
    
    // 生成經模擬簽署的科研解鎖 Token (格式: EFD-STUDY-14D-XXXX-XXXX)
    std::mt19937 rng(1337 + static_cast<unsigned int>(std::chrono::system_clock::now().time_since_epoch().count()));
    std::uniform_int_distribution<int> dist(1000, 9999);
    
    std::ostringstream oss;
    oss << "EFD-14D-" << dist(rng) << "-" << dist(rng);
    m_unlockToken = oss.str();
    m_status = StudyStatus::CompletedUnlocked;
    saveConfigUnlocked();

    return m_unlockToken;
}

int StudyWorkflowTracker::getLastSyncedDate() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_lastSyncedDate;
}

void StudyWorkflowTracker::setLastSyncedDate(int dateInt) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_lastSyncedDate = dateInt;
    saveConfigUnlocked();
}

int64_t StudyWorkflowTracker::getStudyStartTimestamp() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_studyStartTimestamp;
}

int StudyWorkflowTracker::getStartDate() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_startDate;
}

const char* StudyWorkflowTracker::statusLabel(StudyStatus status) {
    switch (status) {
    case StudyStatus::LockedForPostTest: return "Locked";
    case StudyStatus::CompletedUnlocked: return "Completed";
    default:                             return "Active";
    }
}

int StudyWorkflowTracker::getSensitivityLevel() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_sensitivityLevel;
}

void StudyWorkflowTracker::setSensitivityLevel(int level) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_sensitivityLevel = std::clamp(level, 0, 2);
    saveConfigUnlocked();
}

void StudyWorkflowTracker::setStartDate(int dateInt) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_startDate = dateInt;
    refreshStudyProgressUnlocked();
    saveConfigUnlocked();
}

void StudyWorkflowTracker::resetStudy(const std::string& newUuid) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_subjectUuid = newUuid.empty() ? generateRandomUuid() : newUuid;
    m_studyStartTimestamp = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    m_startDate = getTodayDateInt();
    m_currentDay = 1;
    m_status = StudyStatus::ActiveMonitoring;
    m_unlockToken.clear();
    m_lastSyncedDate = 0;
    saveConfigUnlocked();
}

void StudyWorkflowTracker::saveConfigUnlocked() {
    std::ofstream out(m_configPath, std::ios::trunc);
    if (!out.is_open()) return;

    out << "{\n"
        << "  \"subjectUuid\": \"" << m_subjectUuid << "\",\n"
        << "  \"currentDay\": " << m_currentDay << ",\n"
        << "  \"status\": " << static_cast<int>(m_status) << ",\n"
        << "  \"unlockToken\": \"" << m_unlockToken << "\",\n"
        << "  \"studyStartTimestamp\": " << m_studyStartTimestamp << ",\n"
        << "  \"startDate\": " << m_startDate << ",\n"
        << "  \"launchCount\": " << m_launchCount << ",\n"
        << "  \"hasCompletedInitialSetup\": " << (m_hasCompletedInitialSetup ? "true" : "false") << ",\n"
        << "  \"calibBaselineEar\": " << m_savedBaselineEar << ",\n"
        << "  \"calibEarStdDev\": " << m_savedEarStdDev << ",\n"
        << "  \"calibThresholdEar\": " << m_savedThresholdEar << ",\n"
        << "  \"cloudSyncEndpoint\": \"" << m_cloudSyncEndpoint << "\",\n"
        << "  \"lastSyncedDate\": " << m_lastSyncedDate << ",\n"
        << "  \"sensitivityLevel\": " << m_sensitivityLevel << "\n"
        << "}\n";
}

void StudyWorkflowTracker::loadConfigUnlocked() {
    std::ifstream in(m_configPath);
    if (!in.is_open()) return;

    std::string line;
    while (std::getline(in, line)) {
        size_t colon = line.find(':');
        if (colon == std::string::npos) continue;

        std::string key = line.substr(0, colon);
        std::string val = line.substr(colon + 1);

        auto trim = [](std::string& s) {
            s.erase(0, s.find_first_not_of(" \t\r\n\","));
            size_t last = s.find_last_not_of(" \t\r\n\",");
            if (last != std::string::npos) s.erase(last + 1);
            else s.clear();
        };

        trim(key);
        trim(val);

        // 單一欄位損毀時略過該欄位 (保留預設值)，不讓整個 App 因 stoi 例外而閃退
        try {
            if (key == "subjectUuid") m_subjectUuid = val;
            else if (key == "currentDay") m_currentDay = std::clamp(std::stoi(val), 1, 14);
            else if (key == "status") {
                int st = std::stoi(val);
                if (st >= 0 && st <= static_cast<int>(StudyStatus::CompletedUnlocked)) {
                    m_status = static_cast<StudyStatus>(st);
                }
            }
            else if (key == "unlockToken") m_unlockToken = val;
            else if (key == "studyStartTimestamp") m_studyStartTimestamp = std::stoll(val);
            else if (key == "startDate") m_startDate = std::stoi(val);
            else if (key == "launchCount") m_launchCount = std::stoi(val);
            else if (key == "hasCompletedInitialSetup") m_hasCompletedInitialSetup = (val == "true" || val == "1");
            else if (key == "calibBaselineEar") m_savedBaselineEar = std::stof(val);
            else if (key == "calibEarStdDev") m_savedEarStdDev = std::stof(val);
            else if (key == "calibThresholdEar") m_savedThresholdEar = std::stof(val);
            else if (key == "cloudSyncEndpoint") m_cloudSyncEndpoint = val;
            else if (key == "lastSyncedDate") m_lastSyncedDate = std::stoi(val);
            else if (key == "sensitivityLevel") m_sensitivityLevel = std::clamp(std::stoi(val), 0, 2);
        } catch (const std::exception&) {
            std::cerr << "[StudyWorkflowTracker] 設定欄位損毀，已略過: " << key << "\n";
        }
    }
}

std::string StudyWorkflowTracker::generateRandomUuid() {
    std::random_device rd;
    std::mt19937 rng(rd());
    std::uniform_int_distribution<uint32_t> dist(0x1000, 0xFFFF);

    std::ostringstream oss;
    oss << "SUBJ-" << std::hex << std::uppercase << dist(rng) << "-" << dist(rng);
    return oss.str();
}

} // namespace efd

