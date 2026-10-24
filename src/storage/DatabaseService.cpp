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

void DatabaseService::setDatabasePath(const std::string& path) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!path.empty()) {
        m_dbPath = path;
        m_walPath = path + ".wal";
        loadFromDiskUnlocked();
    }
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
            << static_cast<int>(record.alertLevel) << ","
            << record.blinkRatePerMin << "\n";
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
            << "      \"alertLevel\": " << static_cast<int>(r.alertLevel) << ",\n"
            << "      \"blinkRatePerMin\": " << std::fixed << std::setprecision(2) << r.blinkRatePerMin << "\n"
            << "    }" << (i + 1 < m_memoryCache.size() ? "," : "") << "\n";
    }
    oss << "  ]\n}";
    return oss.str();
}

std::string DatabaseService::exportRecordsAsCsv() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return recordsToCsv(m_memoryCache);
}

std::string DatabaseService::recordsToCsv(const std::vector<FatigueRecord>& records) {
    std::ostringstream oss;
    oss << "TimestampMs,SubjectUUID,EAR,PERCLOS,ComplexityIndex,FatigueScore,AlertLevel,BlinkRatePerMin\n";
    for (const auto& r : records) {
        oss << r.timestampMs << ","
            << r.subjectUuid << ","
            << std::fixed << std::setprecision(4) << r.ear << ","
            << std::fixed << std::setprecision(4) << r.perclos << ","
            << std::fixed << std::setprecision(4) << r.complexityIndex << ","
            << std::fixed << std::setprecision(2) << r.fatigueScore << ","
            << static_cast<int>(r.alertLevel) << ","
            << std::fixed << std::setprecision(2) << r.blinkRatePerMin << "\n";
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

    db << "TimestampMs,SubjectUUID,EAR,PERCLOS,ComplexityIndex,FatigueScore,AlertLevel,BlinkRatePerMin\n";
    for (const auto& r : m_memoryCache) {
        db << r.timestampMs << ","
           << r.subjectUuid << ","
           << r.ear << ","
           << r.perclos << ","
           << r.complexityIndex << ","
           << r.fatigueScore << ","
           << static_cast<int>(r.alertLevel) << ","
           << r.blinkRatePerMin << "\n";
    }
    db.close();

    // 清空 WAL 記錄
    std::ofstream wal(m_walPath, std::ios::trunc);
    wal.close();
}

bool DatabaseService::parseRecordLine(const std::string& line, FatigueRecord& r) {
    if (line.empty()) return false;
    // 斷電或寫入中斷可能留下半行資料：略過該行，不讓整個 App 因例外閃退
    try {
        std::stringstream ss(line);
        std::string fields[7];
        for (std::string& field : fields) {
            if (!std::getline(ss, field, ',')) return false; // 欄位不足 (半行) 視為無效
        }
        r.timestampMs = std::stoll(fields[0]);
        r.subjectUuid = fields[1];
        r.ear = std::stof(fields[2]);
        r.perclos = std::stof(fields[3]);
        r.complexityIndex = std::stof(fields[4]);
        r.fatigueScore = std::stof(fields[5]);
        int level = std::stoi(fields[6]);
        r.alertLevel = (level >= 0 && level <= static_cast<int>(FatigueLevel::UserAway))
            ? static_cast<FatigueLevel>(level) : FatigueLevel::Relaxed;
        // 第 8 欄眨眼率為選填：舊版 7 欄資料或損毀時以 0 代替
        r.blinkRatePerMin = 0.0f;
        std::string blinkField;
        if (std::getline(ss, blinkField, ',') && !blinkField.empty()) {
            try {
                r.blinkRatePerMin = std::stof(blinkField);
            } catch (const std::exception&) {
                r.blinkRatePerMin = 0.0f;
            }
        }
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

int64_t DatabaseService::getOldestRecordTimestampMs() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    int64_t oldest = 0;
    for (const auto& r : m_memoryCache) {
        if (r.timestampMs > 0 && (oldest == 0 || r.timestampMs < oldest)) {
            oldest = r.timestampMs;
        }
    }
    return oldest;
}

void DatabaseService::loadFromDiskUnlocked() {
    m_memoryCache.clear();
    
    // 1. 讀取主資料庫檔案
    std::ifstream db(m_dbPath);
    if (db.is_open()) {
        std::string line;
        if (std::getline(db, line)) { // 跳過標題列
            while (std::getline(db, line)) {
                FatigueRecord r;
                if (parseRecordLine(line, r)) {
                    m_memoryCache.push_back(r);
                }
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
            FatigueRecord r;
            if (parseRecordLine(line, r)) {
                m_memoryCache.push_back(r);
                hasWalRecords = true;
            }
        }
        wal.close();

        // 若有 WAL 記錄，進行 Checkpoint 合併至主檔案
        if (hasWalRecords) {
            flushToDiskUnlocked();
        }
    }
}

} // namespace efd

