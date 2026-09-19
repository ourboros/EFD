#pragma once

#include "efd/types.hpp"
#include "storage/DatabaseService.hpp"
#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>

namespace efd {

enum class SyncStatus : uint8_t {
    Idle,
    Syncing,
    Success,
    Failed
};

struct SyncPayload {
    std::string subjectUuid;
    int currentDay = 1;
    size_t recordCount = 0;
    std::string questionnaireResponse;
    std::string dataChecksum;
    int64_t timestampMs = 0;
};

struct SyncResult {
    bool success = false;
    std::string unlockToken;
    std::string message;
    int httpStatusCode = 200;
};

class NetworkSyncWorker {
public:
    using SyncCallback = std::function<void(const SyncResult& result)>;

    explicit NetworkSyncWorker(DatabaseService& dbService, const std::string& endpointUrl = "https://api.efd-research.org/v1/sync");
    ~NetworkSyncWorker();

    // 設定科研雲端端點
    void setEndpointUrl(const std::string& url);
    std::string getEndpointUrl() const;

    // 觸發非同步科研資料同步 (包含問卷答案上傳與解鎖 Token 交換)
    bool triggerSync(const std::string& subjectUuid, int studyDay, const std::string& questionnaireResponse, SyncCallback callback = nullptr);

    // 查詢同步狀態
    SyncStatus getStatus() const;
    bool isSyncing() const;

    // 取得最新解鎖 Token (若同步成功)
    std::string getLatestUnlockToken() const;

    // 工具函式：計算字串 SHA-256 簡化雜湊碼 (用於校驗時序完整性)
    static std::string calculateChecksum(const std::string& input);

private:
    DatabaseService& m_dbService;
    std::string m_endpointUrl;
    std::atomic<SyncStatus> m_status{SyncStatus::Idle};
    std::string m_latestUnlockToken;

    mutable std::mutex m_mutex;
    std::condition_variable m_cv;
    std::atomic<bool> m_stopWorker{false};
    std::atomic<bool> m_hasPendingTask{false};

    SyncPayload m_pendingPayload;
    SyncCallback m_pendingCallback;

    std::thread m_workerThread;

    void workerLoop();
    SyncResult executeSync(const SyncPayload& payload);
};

} // namespace efd

