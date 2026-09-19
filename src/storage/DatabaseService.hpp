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
};

class DatabaseService {
public:
    explicit DatabaseService(const std::string& dbPath = "efd_study_data.dat");
    ~DatabaseService();

    bool initialize();
    bool logRecord(const FatigueRecord& record);
    bool logBatch(const std::vector<FatigueRecord>& records);
    
    size_t getRecordCount() const;
    std::vector<FatigueRecord> getLatestRecords(size_t count) const;
    std::string exportRecordsAsJson() const;
    std::string exportRecordsAsCsv() const;
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
};

} // namespace efd

