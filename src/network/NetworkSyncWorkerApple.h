#pragma once

#include <string>

namespace efd {

// iOS / macOS 原生 HTTPS POST 上傳模組 (使用 NSURLSession 與 Google Apps Script 通訊)
bool PerformAppleHttpUpload(const std::string& url, const std::string& jsonBody, std::string& outResponse);

} // namespace efd
