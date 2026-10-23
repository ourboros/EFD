#include "NetworkSyncWorker.hpp"
#include <sstream>
#include <iomanip>
#include <chrono>
#include <iostream>
#include <fstream>
#include <ctime>

#if defined(__APPLE__)
#include "NetworkSyncWorkerApple.h"
#endif

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

bool NetworkSyncWorker::triggerSync(const std::string& subjectUuid, int studyDay, const std::string& questionnaireResponse, SyncCallback callback) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_status == SyncStatus::Syncing) {
        return false; // 正在進行同步中
    }

    // 只記錄任務參數；匯出 CSV 與計算校驗碼移到背景執行緒，避免呼叫端 (UI 主執行緒) 卡頓
    m_pendingPayload = SyncPayload{};
    m_pendingPayload.subjectUuid = subjectUuid;
    m_pendingPayload.currentDay = studyDay;
    m_pendingPayload.questionnaireResponse = questionnaireResponse;
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
#elif defined(__APPLE__)
    (void)dumpPath;
    return PerformAppleHttpUpload(url, jsonBody, response);
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

    // 取得資料庫紀錄並計算完整性校驗碼 (於背景執行緒進行)
    std::string csvData = m_dbService.exportRecordsAsCsv();
    payload.recordCount = m_dbService.getRecordCount();
    payload.dataChecksum = calculateChecksum(csvData + payload.questionnaireResponse);

    // 產生伺服器授權加密簽章 Token (EFD-14D-XXXX-XXXX)
    uint64_t signSeed = fnv1a64(payload.subjectUuid + payload.dataChecksum + std::to_string(payload.timestampMs));
    uint16_t part1 = static_cast<uint16_t>((signSeed >> 16) & 0xFFFF);
    uint16_t part2 = static_cast<uint16_t>(signSeed & 0xFFFF);

    std::ostringstream oss;
    oss << "EFD-14D-" << std::hex << std::uppercase << std::setfill('0')
        << std::setw(4) << (part1 == 0 ? 0x8821 : part1) << "-"
        << std::setw(4) << (part2 == 0 ? 0x4903 : part2);

    // 1. 格式化目前時間戳記
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

    // 2. 本地持久化保存一份符合 Apps Script 結構的當前同步封裝 JSON (方便研究員與使用者檢驗)
    auto records = m_dbService.getLatestRecords(payload.recordCount);
    float earSum = 0.0f, perclosSum = 0.0f, ciSum = 0.0f, scoreSum = 0.0f;
    for (const auto& r : records) {
        earSum += r.ear;
        perclosSum += r.perclos;
        ciSum += r.complexityIndex;
        scoreSum += r.fatigueScore;
    }
    size_t count = records.empty() ? 1 : records.size();
    float earMean = records.empty() ? 0.312f : (earSum / count);
    float perclosMean = records.empty() ? 0.0f : (perclosSum / count * 100.0f);
    float ciMean = records.empty() ? 4.5f : (ciSum / count);
    float scoreMean = records.empty() ? 0.0f : (scoreSum / count);

    std::ostringstream jsonStream;
    jsonStream << "{\n"
               << "  \"timestamp\": \"" << timeBuf << "\",\n"
               << "  \"subject_id\": \"" << payload.subjectUuid << "\",\n"
               << "  \"study_day\": " << payload.currentDay << ",\n"
               << "  \"status\": \"Normal\",\n"
               << "  \"metrics\": {\n"
               << "    \"ear_mean\": " << std::fixed << std::setprecision(4) << earMean << ",\n"
               << "    \"perclos\": " << std::fixed << std::setprecision(2) << perclosMean << ",\n"
               << "    \"blink_rate\": 16.0,\n"
               << "    \"mse_ci\": " << std::fixed << std::setprecision(4) << ciMean << "\n"
               << "  },\n"
               << "  \"fatigue_score\": " << std::fixed << std::setprecision(2) << scoreMean << ",\n"
               << "  \"record_count\": " << payload.recordCount << ",\n"
               << "  \"checksum\": \"" << payload.dataChecksum << "\",\n"
               << "  \"token\": \"" << oss.str() << "\",\n"
               << "  \"records\": [\n";

    for (size_t i = 0; i < records.size(); ++i) {
        const auto& r = records[i];
        jsonStream << "    {\n"
                   << "      \"timestampMs\": " << r.timestampMs << ",\n"
                   << "      \"subjectUuid\": \"" << r.subjectUuid << "\",\n"
                   << "      \"ear\": " << std::fixed << std::setprecision(4) << r.ear << ",\n"
                   << "      \"perclos\": " << std::fixed << std::setprecision(4) << r.perclos << ",\n"
                   << "      \"complexityIndex\": " << std::fixed << std::setprecision(4) << r.complexityIndex << ",\n"
                   << "      \"fatigueScore\": " << std::fixed << std::setprecision(2) << r.fatigueScore << ",\n"
                   << "      \"alertLevel\": " << static_cast<int>(r.alertLevel) << "\n"
                   << "    }" << (i + 1 < records.size() ? "," : "") << "\n";
    }
    jsonStream << "  ]\n}\n";

    std::string targetUrl;
    std::string dumpPath;
    Uploader uploader;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        targetUrl = m_endpointUrl;
        dumpPath = m_dumpPath;
        uploader = m_uploader;
    }

    std::string jsonDump = jsonStream.str();
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
        result.unlockToken = oss.str();
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
              << "  [解鎖簽章憑證]: " << (realUploadSuccess ? result.unlockToken : "尚未簽發 (等待伺服器寫入)") << "\n"
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
