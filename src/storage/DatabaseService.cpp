#include "DatabaseService.hpp"
#include <fstream>
#include <sstream>
#include <iomanip>
#include <iostream>
#include <chrono>

namespace efd {

DatabaseService::DatabaseService(const std::string& dbPath)
    : m_dbPath(dbPath), m_walPath(dbPath + ".wal") {
}

DatabaseService::~DatabaseService() {
    std::lock_guard<std::mutex> lock(m_mutex);
    flushToDiskUnlocked();
}

bool DatabaseService::initialize() {
    std::lock_guard<std::mutex> lock(m_mutex);
    loadFromDiskUnlocked();
    m_initialized = true;
    return true;
}

bool DatabaseService::logRecord(const FatigueRecord& record) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_memoryCache.push_back(record);

    // Write-Ahead Logging (WAL) 即時落盤，防止崩潰遺失資料
    std::ofstream wal(m_walPath, std::ios::app);
    if (wal.is_open()) {
        wal << record.timestampMs << ","
            << record.subjectUuid << ","
            << record.ear << ","
            << record.perclos << ","
            << record.complexityIndex << ","
            << record.fatigueScore << ","
            << static_cast<int>(record.alertLevel) << "\n";
    }

    // 每 20 筆記錄定期合併至主資料庫檔案
    if (m_memoryCache.size() % 20 == 0) {
        flushToDiskUnlocked();
    }
    return true;
}

bool DatabaseService::logBatch(const std::vector<FatigueRecord>& records) {
    if (records.empty()) return true;
    std::lock_guard<std::mutex> lock(m_mutex);
    for (const auto& r : records) {
        m_memoryCache.push_back(r);
    }
    flushToDiskUnlocked();
    return true;
}

size_t DatabaseService::getRecordCount() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_memoryCache.size();
}

std::vector<FatigueRecord> DatabaseService::getLatestRecords(size_t count) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_memoryCache.size() <= count) {
        return m_memoryCache;
    }
    return std::vector<FatigueRecord>(m_memoryCache.end() - static_cast<ptrdiff_t>(count), m_memoryCache.end());
}

std::string DatabaseService::exportRecordsAsJson() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::ostringstream oss;
    oss << "{\n  \"recordCount\": " << m_memoryCache.size() << ",\n  \"records\": [\n";
    for (size_t i = 0; i < m_memoryCache.size(); ++i) {
        const auto& r = m_memoryCache[i];
        oss << "    {\n"
            << "      \"timestampMs\": " << r.timestampMs << ",\n"
            << "      \"subjectUuid\": \"" << r.subjectUuid << "\",\n"
            << "      \"ear\": " << std::fixed << std::setprecision(4) << r.ear << ",\n"
            << "      \"perclos\": " << std::fixed << std::setprecision(4) << r.perclos << ",\n"
            << "      \"complexityIndex\": " << std::fixed << std::setprecision(4) << r.complexityIndex << ",\n"
            << "      \"fatigueScore\": " << std::fixed << std::setprecision(2) << r.fatigueScore << ",\n"
            << "      \"alertLevel\": " << static_cast<int>(r.alertLevel) << "\n"
            << "    }" << (i + 1 < m_memoryCache.size() ? "," : "") << "\n";
    }
    oss << "  ]\n}";
    return oss.str();
}

std::string DatabaseService::exportRecordsAsCsv() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::ostringstream oss;
    oss << "TimestampMs,SubjectUUID,EAR,PERCLOS,ComplexityIndex,FatigueScore,AlertLevel\n";
    for (const auto& r : m_memoryCache) {
        oss << r.timestampMs << ","
            << r.subjectUuid << ","
            << std::fixed << std::setprecision(4) << r.ear << ","
            << std::fixed << std::setprecision(4) << r.perclos << ","
            << std::fixed << std::setprecision(4) << r.complexityIndex << ","
            << std::fixed << std::setprecision(2) << r.fatigueScore << ","
            << static_cast<int>(r.alertLevel) << "\n";
    }
    return oss.str();
}

void DatabaseService::flush() {
    std::lock_guard<std::mutex> lock(m_mutex);
    flushToDiskUnlocked();
}

void DatabaseService::clearAllData() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_memoryCache.clear();
    std::remove(m_dbPath.c_str());
    std::remove(m_walPath.c_str());
}

void DatabaseService::flushToDiskUnlocked() {
    if (m_memoryCache.empty()) return;
    std::ofstream db(m_dbPath, std::ios::trunc);
    if (!db.is_open()) return;

    db << "TimestampMs,SubjectUUID,EAR,PERCLOS,ComplexityIndex,FatigueScore,AlertLevel\n";
    for (const auto& r : m_memoryCache) {
        db << r.timestampMs << ","
           << r.subjectUuid << ","
           << r.ear << ","
           << r.perclos << ","
           << r.complexityIndex << ","
           << r.fatigueScore << ","
           << static_cast<int>(r.alertLevel) << "\n";
    }
    db.close();

    // 清空 WAL 記錄
    std::ofstream wal(m_walPath, std::ios::trunc);
    wal.close();
}

void DatabaseService::loadFromDiskUnlocked() {
    m_memoryCache.clear();
    
    // 1. 讀取主資料庫檔案
    std::ifstream db(m_dbPath);
    if (db.is_open()) {
        std::string line;
        if (std::getline(db, line)) { // 跳過標題列
            while (std::getline(db, line)) {
                if (line.empty()) continue;
                std::stringstream ss(line);
                std::string token;
                FatigueRecord r;
                if (std::getline(ss, token, ',')) r.timestampMs = std::stoll(token);
                if (std::getline(ss, token, ',')) r.subjectUuid = token;
                if (std::getline(ss, token, ',')) r.ear = std::stof(token);
                if (std::getline(ss, token, ',')) r.perclos = std::stof(token);
                if (std::getline(ss, token, ',')) r.complexityIndex = std::stof(token);
                if (std::getline(ss, token, ',')) r.fatigueScore = std::stof(token);
                if (std::getline(ss, token, ',')) r.alertLevel = static_cast<FatigueLevel>(std::stoi(token));
                m_memoryCache.push_back(r);
            }
        }
        db.close();
    }

    // 2. WAL 故障恢復重放 (WAL Replay Recovery)
    std::ifstream wal(m_walPath);
    if (wal.is_open()) {
        std::string line;
        bool hasWalRecords = false;
        while (std::getline(wal, line)) {
            if (line.empty()) continue;
            std::stringstream ss(line);
            std::string token;
            FatigueRecord r;
            if (std::getline(ss, token, ',')) r.timestampMs = std::stoll(token);
            if (std::getline(ss, token, ',')) r.subjectUuid = token;
            if (std::getline(ss, token, ',')) r.ear = std::stof(token);
            if (std::getline(ss, token, ',')) r.perclos = std::stof(token);
            if (std::getline(ss, token, ',')) r.complexityIndex = std::stof(token);
            if (std::getline(ss, token, ',')) r.fatigueScore = std::stof(token);
            if (std::getline(ss, token, ',')) r.alertLevel = static_cast<FatigueLevel>(std::stoi(token));
            m_memoryCache.push_back(r);
            hasWalRecords = true;
        }
        wal.close();

        // 若有 WAL 記錄，進行 Checkpoint 合併至主檔案
        if (hasWalRecords) {
            flushToDiskUnlocked();
        }
    }
}

} // namespace efd

