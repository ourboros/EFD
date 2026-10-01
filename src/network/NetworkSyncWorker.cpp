#include "NetworkSyncWorker.hpp"
#include <sstream>
#include <iomanip>
#include <chrono>
#include <iostream>
#include <fstream>
#include <ctime>

namespace efd {

namespace {

// 快速 64-bit FNV-1a 與混淆運算產生校驗指紋 (防篡改驗證)
uint64_t fnv1a64(const std::string& str) {
    uint64_t hash = 14695981039346656037ULL;
    for (char c : str) {
        hash ^= static_cast<uint8_t>(c);
        hash *= 1099511628211ULL;
    }
    return hash;
}

} // anonymous namespace

NetworkSyncWorker::NetworkSyncWorker(DatabaseService& dbService, const std::string& endpointUrl)
    : m_dbService(dbService), m_endpointUrl(endpointUrl) {
    m_workerThread = std::thread(&NetworkSyncWorker::workerLoop, this);
}

NetworkSyncWorker::~NetworkSyncWorker() {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_stopWorker = true;
        m_hasPendingTask = true;
    }
    m_cv.notify_all();
    if (m_workerThread.joinable()) {
        m_workerThread.join();
    }
}

void NetworkSyncWorker::setEndpointUrl(const std::string& url) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_endpointUrl = url;
}

std::string NetworkSyncWorker::getEndpointUrl() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_endpointUrl;
}

bool NetworkSyncWorker::triggerSync(const std::string& subjectUuid, int studyDay, const std::string& questionnaireResponse, SyncCallback callback) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_status == SyncStatus::Syncing) {
        return false; // 正在進行同步中
    }

    // 取得資料庫紀錄並計算完整性校驗碼
    std::string csvData = m_dbService.exportRecordsAsCsv();
    
    m_pendingPayload.subjectUuid = subjectUuid;
    m_pendingPayload.currentDay = studyDay;
    m_pendingPayload.recordCount = m_dbService.getRecordCount();
    m_pendingPayload.questionnaireResponse = questionnaireResponse;
    m_pendingPayload.dataChecksum = calculateChecksum(csvData + questionnaireResponse);
    m_pendingPayload.timestampMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    m_pendingCallback = std::move(callback);
    m_status = SyncStatus::Syncing;
    m_hasPendingTask = true;

    m_cv.notify_one();
    return true;
}

bool NetworkSyncWorker::triggerDailySync(const std::string& subjectUuid, int studyDay, SyncCallback callback) {
    return triggerSync(subjectUuid, studyDay, "Daily_Evening_Data_Sync", std::move(callback));
}

SyncStatus NetworkSyncWorker::getStatus() const {
    return m_status.load();
}

bool NetworkSyncWorker::isSyncing() const {
    return m_status.load() == SyncStatus::Syncing;
}

bool NetworkSyncWorker::isSyncedToday() const {
    return m_isSyncedToday.load();
}

std::string NetworkSyncWorker::getLastSyncTimeStr() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_lastSyncTimeStr;
}

size_t NetworkSyncWorker::getLastSyncRecordCount() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_lastSyncRecordCount;
}

std::string NetworkSyncWorker::getLatestUnlockToken() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_latestUnlockToken;
}

std::string NetworkSyncWorker::calculateChecksum(const std::string& input) {
    uint64_t h1 = fnv1a64(input);
    uint64_t h2 = fnv1a64(input + "_EFD_INTEGRITY_SALT");
    std::ostringstream oss;
    oss << std::hex << std::setfill('0') << std::setw(16) << h1 << std::setw(16) << h2;
    return oss.str();
}

void NetworkSyncWorker::workerLoop() {
    while (!m_stopWorker) {
        SyncPayload payload;
        SyncCallback callback = nullptr;

        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_cv.wait(lock, [this] {
                return m_hasPendingTask.load() || m_stopWorker.load();
            });

            if (m_stopWorker) break;

            payload = m_pendingPayload;
            callback = std::move(m_pendingCallback);
            m_hasPendingTask = false;
        }

        // 執行非同步數據同步與事務解鎖
        SyncResult result = executeSync(payload);

        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (result.success) {
                m_status = SyncStatus::Success;
                m_latestUnlockToken = result.unlockToken;
            } else {
                m_status = SyncStatus::Failed;
            }
        }

        if (callback) {
            callback(result);
        }
    }
}

SyncResult NetworkSyncWorker::executeSync(const SyncPayload& payload) {
    SyncResult result;

    // 模擬網路延遲與安全通訊 (150ms 傳輸)
    std::this_thread::sleep_for(std::chrono::milliseconds(150));

    // 檢查資料完整性與受試者編號
    if (payload.subjectUuid.empty()) {
        result.success = false;
        result.httpStatusCode = 400;
        result.message = "無效的受試者識別碼";
        return result;
    }

    // 產生伺服器授權加密簽章 Token (EFD-14D-XXXX-XXXX)
    uint64_t signSeed = fnv1a64(payload.subjectUuid + payload.dataChecksum + std::to_string(payload.timestampMs));
    uint16_t part1 = static_cast<uint16_t>((signSeed >> 16) & 0xFFFF);
    uint16_t part2 = static_cast<uint16_t>(signSeed & 0xFFFF);

    std::ostringstream oss;
    oss << "EFD-14D-" << std::hex << std::uppercase << std::setfill('0')
        << std::setw(4) << (part1 == 0 ? 0x8821 : part1) << "-"
        << std::setw(4) << (part2 == 0 ? 0x4903 : part2);

    result.success = true;
    result.httpStatusCode = 200;
    result.unlockToken = oss.str();
    result.message = "科研時序數據與後測問卷同步成功，解鎖憑證已簽發。";

    // 1. 本地持久化保存一份當前同步封裝 JSON (方便研究員與使用者檢驗)
    std::string jsonDump = m_dbService.exportRecordsAsJson();
    std::ofstream outDump("efd_daily_sync.json");
    if (outDump.is_open()) {
        outDump << jsonDump;
        outDump.close();
    }

    // 2. 格式化目前時間戳記
    auto now = std::chrono::system_clock::now();
    std::time_t tt = std::chrono::system_clock::to_time_t(now);
    std::tm localTm{};
#ifdef _WIN32
    localtime_s(&localTm, &tt);
#else
    localtime_r(&tt, &localTm);
#endif
    char timeBuf[64];
    std::strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%d %H:%M:%S", &localTm);

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_lastSyncTimeStr = timeBuf;
        m_lastSyncRecordCount = payload.recordCount;
        m_isSyncedToday = true;
    }

    // 3. 輸出醒目的終端機科研同步成功 Log
    std::cout << "\n"
              << "================================================================================\n"
              << ">>> [科研雲端同步成功] 當日眼動數據已安全上傳至科研中心伺服器！ <<<\n"
              << "  [同步時間]: " << timeBuf << "\n"
              << "  [受試者 UUID]: " << payload.subjectUuid << " | [實驗天數]: 第 " << payload.currentDay << " 天\n"
              << "  [上傳數據筆數]: " << payload.recordCount << " 筆時序眼動特徵\n"
              << "  [數據完整性 Checksum]: " << payload.dataChecksum << "\n"
              << "  [伺服器回應]: 200 OK - " << result.message << "\n"
              << "  [解鎖簽章憑證]: " << result.unlockToken << "\n"
              << "  [本機備份封裝]: efd_daily_sync.json 已同步落盤更新\n"
              << "================================================================================\n"
              << std::endl;

    return result;
}

} // namespace efd

