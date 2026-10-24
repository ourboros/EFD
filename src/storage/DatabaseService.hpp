#pragma once

#include <string>
#include <vector>
#include <mutex>
#include <cstdint>
#include "efd/types.hpp"

namespace efd {

struct FatigueRecord {
    int64_t timestampMs = 0;
    std::string subjectUuid;
    float ear = 0.0f;
    float perclos = 0.0f;
    float complexityIndex = 0.0f;
    float fatigueScore = 0.0f;
    FatigueLevel alertLevel = FatigueLevel::Relaxed;
    float blinkRatePerMin = 0.0f;  // 第 8 欄 (新增於 v2.1)；舊版 7 欄資料讀入時為 0
};

class DatabaseService {
public:
    explicit DatabaseService(const std::string& dbPath = "efd_study_data.dat");
    ~DatabaseService();

    void setDatabasePath(const std::string& path);
    bool initialize();
    bool logRecord(const FatigueRecord& record);
    bool logBatch(const std::vector<FatigueRecord>& records);
    
    size_t getRecordCount() const;
    // 最舊一筆紀錄的時間戳 (無紀錄回傳 0)，用於判斷是否有前幾天未上傳的資料
    int64_t getOldestRecordTimestampMs() const;
    std::vector<FatigueRecord> getLatestRecords(size_t count) const;
    std::string exportRecordsAsJson() const;
    std::string exportRecordsAsCsv() const;
    static std::string recordsToCsv(const std::vector<FatigueRecord>& records);
    void flush();
    void clearAllData();

private:
    std::string m_dbPath;
    std::string m_walPath;
    mutable std::mutex m_mutex;
    std::vector<FatigueRecord> m_memoryCache;
    bool m_initialized = false;

    void flushToDiskUnlocked();
    void loadFromDiskUnlocked();
    static bool parseRecordLine(const std::string& line, FatigueRecord& record);
};

} // namespace efd

