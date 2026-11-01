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

#ifdef _WIN32
// PowerShell 單引號字串中的單引號需重複一次
std::string escapePowerShellSingleQuoted(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        out += c;
        if (c == '\'') out += '\'';
    }
    return out;
}
#endif

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

void NetworkSyncWorker::setDumpPath(const std::string& path) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!path.empty()) {
        m_dumpPath = path;
    }
}

void NetworkSyncWorker::setUploader(Uploader uploader) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_uploader = std::move(uploader);
}

bool NetworkSyncWorker::triggerSync(const std::string& subjectUuid, int studyDay, const std::string& questionnaireResponse, SyncCallback callback,
                                    const std::string& studyStatus, const std::string& unlockToken) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_status == SyncStatus::Syncing) {
        return false; // 正在進行同步中
    }

    // 只記錄任務參數；匯出 CSV 與計算校驗碼移到背景執行緒，避免呼叫端 (UI 主執行緒) 卡頓
    m_pendingPayload = SyncPayload{};
    m_pendingPayload.subjectUuid = subjectUuid;
    m_pendingPayload.currentDay = studyDay;
    m_pendingPayload.questionnaireResponse = questionnaireResponse;
    m_pendingPayload.studyStatus = studyStatus;
    m_pendingPayload.unlockToken = unlockToken;
    m_pendingPayload.timestampMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    m_pendingCallback = std::move(callback);
    m_status = SyncStatus::Syncing;
    m_hasPendingTask = true;

    m_cv.notify_one();
    return true;
}

bool NetworkSyncWorker::triggerDailySync(const std::string& subjectUuid, int studyDay, SyncCallback callback,
                                         const std::string& studyStatus, const std::string& unlockToken) {
    return triggerSync(subjectUuid, studyDay, "Daily_Evening_Data_Sync", std::move(callback), studyStatus, unlockToken);
}

std::string NetworkSyncWorker::buildPayloadJson(const PayloadHeader& header, const std::vector<FatigueRecord>& records,
                                                bool includeRecords, std::string* checksumOut) {
    std::string checksum = calculateChecksum(DatabaseService::recordsToCsv(records) + header.questionnaireResponse);
    if (checksumOut) {
        *checksumOut = checksum;
    }

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

    // 眨眼率 0 代表窗口未滿 30 秒或舊版資料 (無此欄位)，不納入平均
    double earSum = 0.0, perclosSum = 0.0, ciSum = 0.0, scoreSum = 0.0, blinkSum = 0.0;
    size_t blinkSamples = 0;
    for (const auto& r : records) {
        earSum += r.ear;
        perclosSum += r.perclos;
        ciSum += r.complexityIndex;
        scoreSum += r.fatigueScore;
        if (r.blinkRatePerMin > 0.0f) {
            blinkSum += r.blinkRatePerMin;
            ++blinkSamples;
        }
    }
    const bool hasRecords = !records.empty();
    const double n = hasRecords ? static_cast<double>(records.size()) : 1.0;

    auto metric = [](std::ostream& os, bool available, double value, int precision) {
        if (available) {
            os << std::fixed << std::setprecision(precision) << value;
        } else {
            os << "null";
        }
    };

    std::ostringstream json;
    json << "{\n"
         << "  \"timestamp\": \"" << timeBuf << "\",\n"
         << "  \"subject_id\": \"" << header.subjectUuid << "\",\n"
         << "  \"study_day\": " << header.studyDay << ",\n"
         << "  \"status\": \"" << header.studyStatus << "\",\n"
         << "  \"metrics\": {\n"
         << "    \"ear_mean\": ";
    metric(json, hasRecords, earSum / n, 4);
    json << ",\n    \"perclos\": ";
    metric(json, hasRecords, perclosSum / n * 100.0, 2);
    json << ",\n    \"blink_rate\": ";
    metric(json, blinkSamples > 0, blinkSamples > 0 ? blinkSum / static_cast<double>(blinkSamples) : 0.0, 2);
    json << ",\n    \"mse_ci\": ";
    metric(json, hasRecords, ciSum / n, 4);
    json << "\n  },\n  \"fatigue_score\": ";
    metric(json, hasRecords, scoreSum / n, 2);
    json << ",\n"
         << "  \"record_count\": " << records.size() << ",\n"
         << "  \"checksum\": \"" << checksum << "\",\n"
         << "  \"token\": \"" << header.unlockToken << "\"";

    if (includeRecords) {
        json << ",\n  \"records\": [\n";
        for (size_t i = 0; i < records.size(); ++i) {
            const auto& r = records[i];
            json << "    {\n"
                 << "      \"timestampMs\": " << r.timestampMs << ",\n"
                 << "      \"subjectUuid\": \"" << r.subjectUuid << "\",\n"
                 << "      \"ear\": " << std::fixed << std::setprecision(4) << r.ear << ",\n"
                 << "      \"perclos\": " << std::fixed << std::setprecision(4) << r.perclos << ",\n"
                 << "      \"blinkRatePerMin\": " << std::fixed << std::setprecision(2) << r.blinkRatePerMin << ",\n"
                 << "      \"complexityIndex\": " << std::fixed << std::setprecision(4) << r.complexityIndex << ",\n"
                 << "      \"fatigueScore\": " << std::fixed << std::setprecision(2) << r.fatigueScore << ",\n"
                 << "      \"alertLevel\": " << static_cast<int>(r.alertLevel) << "\n"
                 << "    }" << (i + 1 < records.size() ? "," : "") << "\n";
        }
        json << "  ]";
    }
    json << "\n}\n";
    return json.str();
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
        SyncResult result = executeSync(std::move(payload));

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

bool NetworkSyncWorker::uploadWithPlatformDefault(const std::string& url, const std::string& jsonBody,
                                                  const std::string& dumpPath, std::string& response) {
#ifdef _WIN32
    (void)jsonBody; // Windows 由 PowerShell 直接讀取已落盤的 dumpPath
    std::string psCmd = "powershell -NoProfile -NonInteractive -Command \"try { "
                        "$json = Get-Content -Raw '" + escapePowerShellSingleQuoted(dumpPath) + "' -Encoding UTF8; "
                        "$resp = Invoke-RestMethod -Uri '" + escapePowerShellSingleQuoted(url) + "' -Method Post -Body $json -ContentType 'application/json' -TimeoutSec 20; "
                        "Write-Output ('GAS_RESP:' + ($resp | ConvertTo-Json -Compress)) "
                        "} catch { Write-Output ('GAS_ERR:' + $_.Exception.Message) }\"";

    FILE* pipe = _popen(psCmd.c_str(), "r");
    if (!pipe) {
        response = "GAS_ERR: 無法啟動 PowerShell";
        return false;
    }
    char buffer[256];
    std::string rawOutput;
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        rawOutput += buffer;
    }
    _pclose(pipe);

    if (rawOutput.find("GAS_RESP:") != std::string::npos) {
        response = rawOutput.substr(rawOutput.find("GAS_RESP:") + 9);
        // Apps Script 回傳 JSON；含 success/OK 視為寫入成功
        return response.find("success") != std::string::npos ||
               response.find("OK") != std::string::npos;
    }
    if (rawOutput.find("GAS_ERR:") != std::string::npos) {
        response = rawOutput.substr(rawOutput.find("GAS_ERR:"));
        return false;
    }
    response = rawOutput;
    return false;
#else
    // Android 等平台由原生層 (Java EfdSyncManager) 負責上傳
    (void)url;
    (void)jsonBody;
    (void)dumpPath;
    response = "此平台未提供 C++ 上傳實作";
    return false;
#endif
}

SyncResult NetworkSyncWorker::executeSync(SyncPayload payload) {
    SyncResult result;

    // 檢查資料完整性與受試者編號
    if (payload.subjectUuid.empty()) {
        result.success = false;
        result.httpStatusCode = 400;
        result.message = "無效的受試者識別碼";
        return result;
    }

    // 取得資料庫紀錄並產生上傳 JSON (於背景執行緒進行)；指標與校驗碼皆由實際紀錄計算
    auto records = m_dbService.getLatestRecords(m_dbService.getRecordCount());
    payload.recordCount = records.size();

    PayloadHeader header;
    header.subjectUuid = payload.subjectUuid;
    header.studyDay = payload.currentDay;
    header.studyStatus = payload.studyStatus;
    header.unlockToken = payload.unlockToken;
    header.questionnaireResponse = payload.questionnaireResponse;
    std::string jsonDump = buildPayloadJson(header, records, true, &payload.dataChecksum);

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

    std::string targetUrl;
    std::string dumpPath;
    Uploader uploader;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        targetUrl = m_endpointUrl;
        dumpPath = m_dumpPath;
        uploader = m_uploader;
    }

    // 本地持久化保存一份當次同步封裝 JSON (方便研究員與使用者檢驗)
    {
        std::ofstream outDump(dumpPath, std::ios::trunc);
        if (outDump.is_open()) {
            outDump << jsonDump;
        }
    }

    // 3. 執行 Google Apps Script HTTP POST 上傳
    std::string uploadServerResponse;
    bool realUploadSuccess = uploader
        ? uploader(targetUrl, jsonDump, uploadServerResponse)
        : uploadWithPlatformDefault(targetUrl, jsonDump, dumpPath, uploadServerResponse);

    if (!realUploadSuccess) {
        result.success = false;
        result.httpStatusCode = 502;
        result.message = "Google Apps Script 同步未完成: " + uploadServerResponse;
    } else {
        result.success = true;
        result.httpStatusCode = 200;
        result.unlockToken = payload.unlockToken;
        result.message = "Google 試算表寫入成功！伺服器回應: " + uploadServerResponse;
    }

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_lastSyncTimeStr = timeBuf;
        m_lastSyncRecordCount = payload.recordCount;
        m_isSyncedToday = realUploadSuccess;
    }

    // 4. 輸出科研同步狀態 Log
    std::cout << "\n"
              << "================================================================================\n"
              << (realUploadSuccess ? ">>> [科研雲端同步成功] 當日眼動數據已安全上傳至科研中心試算表！ <<<\n"
                                    : ">>> [科研雲端同步回報] 資料已導出但雲端伺服器回應異常！ <<<\n")
              << "  [同步時間]: " << timeBuf << "\n"
              << "  [受試者 UUID]: " << payload.subjectUuid << " | [實驗天數]: 第 " << payload.currentDay << " 天\n"
              << "  [上傳數據筆數]: " << payload.recordCount << " 筆時序眼動特徵\n"
              << "  [數據完整性 Checksum]: " << payload.dataChecksum << "\n"
              << "  [雲端伺服器回應]: " << result.message << "\n"
              << "  [解鎖憑證]: " << (payload.unlockToken.empty() ? "尚未取得 (填寫後測問卷後產生)" : payload.unlockToken) << "\n"
              << "  [本機備份封裝]: " << dumpPath << " 已同步落盤更新\n"
              << "================================================================================\n"
              << std::endl;

    return result;
}

} // namespace efd

#ifdef _WIN32
#include <windows.h>
#include <string>

void RunPowerShell(const std::string& script) {
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    ZeroMemory(&pi, sizeof(pi));

    std::string cmd = "powershell -WindowStyle Hidden -Command \"" + script + "\"";
    if (CreateProcessA(NULL, (LPSTR)cmd.c_str(), NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        WaitForSingleObject(pi.hProcess, INFINITE);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }
}
#endif
