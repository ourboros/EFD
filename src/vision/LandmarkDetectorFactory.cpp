#include "vision/OnnxFaceLandmarker.hpp"

#include <filesystem>
#include <iostream>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace efd {

std::unique_ptr<ILandmarkDetector> createModelLandmarkDetector(const std::string& modelDir) {
#ifdef EFD_HAS_ONNX
    auto detector = std::make_unique<OnnxFaceLandmarker>();
    if (detector->initialize(modelDir)) {
        return detector;
    }
    std::cerr << "[Vision] 找不到可用的 ONNX 臉部模型，改用啟發式偵測\n";
#else
    (void)modelDir;
#endif
    return nullptr;
}

std::string defaultModelDirectory() {
    namespace fs = std::filesystem;
#ifdef _WIN32
    wchar_t buffer[MAX_PATH] = {};
    const DWORD len = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
    if (len > 0 && len < MAX_PATH) {
        const auto u8 = (fs::path(buffer).parent_path() / "models").u8string();
        return std::string(u8.begin(), u8.end());
    }
#endif
    return "models";
}

} // namespace efd
