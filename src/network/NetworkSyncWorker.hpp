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
    std::string studyStatus = "Active";
    std::string unlockToken;   // 填寫後測問卷後才有，未取得時為空字串
};

// 上傳 JSON 的表頭欄位 (各平台共用同一份產生邏輯)
struct PayloadHeader {
    std::string subjectUuid;
    int studyDay = 1;
    std::string studyStatus = "Active";
    std::string unlockToken;
    std::string questionnaireResponse;
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
    // 實際 HTTP 上傳函式 (回傳是否成功，response 填入伺服器回應)；可替換以便測試
    using Uploader = std::function<bool(const std::string& url, const std::string& jsonBody, std::string& response)>;

    explicit NetworkSyncWorker(DatabaseService& dbService, const std::string& endpointUrl = kDefaultCloudSyncEndpoint);
    ~NetworkSyncWorker();

    // 設定科研雲端端點
    void setEndpointUrl(const std::string& url);
    std::string getEndpointUrl() const;

    // 本機同步封裝 JSON 的存放路徑 (預設為工作目錄下 efd_daily_sync.json)
    void setDumpPath(const std::string& path);

    // 替換上傳實作 (未設定時使用平台預設：Windows PowerShell / Apple NSURLSession)
    void setUploader(Uploader uploader);

    // 觸發非同步科研資料同步 (包含問卷答案上傳與解鎖 Token 交換)
    bool triggerSync(const std::string& subjectUuid, int studyDay, const std::string& questionnaireResponse, SyncCallback callback = nullptr,
                     const std::string& studyStatus = "Active", const std::string& unlockToken = "");

    // 觸發當日眼動特徵數據同步 (夜間自動儲存或使用者手動觸發)
    bool triggerDailySync(const std::string& subjectUuid, int studyDay, SyncCallback callback = nullptr,
                          const std::string& studyStatus = "Active", const std::string& unlockToken = "");

    // 產生上傳 JSON：指標為紀錄的實際平均值，無紀錄時指標為 null、筆數為 0
    // checksum 由紀錄內容計算；includeRecords 為 false 時不附逐筆資料 (Android 上傳)
    static std::string buildPayloadJson(const PayloadHeader& header, const std::vector<FatigueRecord>& records,
                                        bool includeRecords, std::string* checksumOut = nullptr);

    // 查詢同步狀態與資訊
    SyncStatus getStatus() const;
    bool isSyncing() const;
    bool isSyncedToday() const;
    std::string getLastSyncTimeStr() const;
    size_t getLastSyncRecordCount() const;

    // 取得最新解鎖 Token (若同步成功)
    std::string getLatestUnlockToken() const;

    // 工具函式：計算字串 SHA-256 簡化雜湊碼 (用於校驗時序完整性)
    static std::string calculateChecksum(const std::string& input);

private:
    DatabaseService& m_dbService;
    std::string m_endpointUrl;
    std::string m_dumpPath = "efd_daily_sync.json";
    Uploader m_uploader;
    std::atomic<SyncStatus> m_status{SyncStatus::Idle};
    std::string m_latestUnlockToken;
    std::string m_lastSyncTimeStr;
    size_t m_lastSyncRecordCount = 0;
    std::atomic<bool> m_isSyncedToday{false};

    mutable std::mutex m_mutex;
    std::condition_variable m_cv;
    std::atomic<bool> m_stopWorker{false};
    std::atomic<bool> m_hasPendingTask{false};

    SyncPayload m_pendingPayload;
    SyncCallback m_pendingCallback;

    std::thread m_workerThread;

    void workerLoop();
    SyncResult executeSync(SyncPayload payload);
    bool uploadWithPlatformDefault(const std::string& url, const std::string& jsonBody,
                                   const std::string& dumpPath, std::string& response);
};

} // namespace efd

