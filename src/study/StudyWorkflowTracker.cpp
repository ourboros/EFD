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

bool StudyWorkflowTracker::initialize() {
    std::lock_guard<std::mutex> lock(m_mutex);
    loadConfigUnlocked();
    if (m_subjectUuid.empty()) {
        m_subjectUuid = generateRandomUuid();
        m_studyStartTimestamp = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        m_currentDay = 1;
        m_status = StudyStatus::ActiveMonitoring;
        saveConfigUnlocked();
    }
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

void StudyWorkflowTracker::setTimeWarpDay(int day) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_currentDay = std::clamp(day, 1, 14);
    if (m_currentDay >= 14 && m_status == StudyStatus::ActiveMonitoring) {
        m_status = StudyStatus::LockedForPostTest;
    }
    saveConfigUnlocked();
}

void StudyWorkflowTracker::advanceDay() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_currentDay < 14) {
        m_currentDay++;
    }
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

void StudyWorkflowTracker::resetStudy(const std::string& newUuid) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_subjectUuid = newUuid.empty() ? generateRandomUuid() : newUuid;
    m_studyStartTimestamp = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    m_currentDay = 1;
    m_status = StudyStatus::ActiveMonitoring;
    m_unlockToken.clear();
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
        << "  \"studyStartTimestamp\": " << m_studyStartTimestamp << "\n"
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

        if (key == "subjectUuid") m_subjectUuid = val;
        else if (key == "currentDay") m_currentDay = std::stoi(val);
        else if (key == "status") m_status = static_cast<StudyStatus>(std::stoi(val));
        else if (key == "unlockToken") m_unlockToken = val;
        else if (key == "studyStartTimestamp") m_studyStartTimestamp = std::stoll(val);
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

