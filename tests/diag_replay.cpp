// 診斷重播工具：把手機診斷模式匯出的畫面 (PPM, RGB) 逐張送進 FaceLandmarker，
// 印出每張卡在哪一關與各項數值，用來在電腦上重現手機上的偵測失敗。
//
// 用法：efd_diag_replay <目錄或 .ppm 檔> [...]
//       目錄會讀取其中 (含子資料夾) 所有 .ppm 檔 (例如專案根目錄的 debug_frames/)
//       efd_diag_replay --capture <輸出.ppm> [相機編號，預設 0]
//       用這台電腦的實體相機拍一張 (經過與 App 相同的驅動與色彩轉換)，存檔後一併判斷；
//       用來檢查不同電腦的鏡頭格式 (畫面黑白條紋、偏紅等問題)

#include "vision/FaceLandmarker.hpp"
#include "vision/CameraService.hpp"

#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace fs = std::filesystem;

namespace {

// 讀取二進位 PPM (P6, maxval 255)
bool readPpm(const fs::path& path, std::vector<uint8_t>& rgb, int& width, int& height) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;

    auto nextToken = [&in]() {
        std::string token;
        char c;
        while (in.get(c)) {
            if (c == '#') {
                std::string comment;
                std::getline(in, comment);
                continue;
            }
            if (std::isspace(static_cast<unsigned char>(c))) {
                if (!token.empty()) break;
                continue;
            }
            token += c;
        }
        return token;
    };

    if (nextToken() != "P6") return false;
    try {
        width = std::stoi(nextToken());
        height = std::stoi(nextToken());
        if (std::stoi(nextToken()) != 255) return false;
    } catch (const std::exception&) {
        return false;
    }
    if (width <= 0 || height <= 0) return false;

    rgb.resize(static_cast<size_t>(width) * height * 3);
    in.read(reinterpret_cast<char*>(rgb.data()), static_cast<std::streamsize>(rgb.size()));
    return static_cast<size_t>(in.gcount()) == rgb.size();
}

// 以實體相機擷取一張 (跳過前 30 幀讓自動曝光穩定)，存成 PPM
bool captureFromCamera(const fs::path& out, int deviceIndex) {
    efd::CameraService camera(640, 480, 30.0f);
    std::mutex mutex;
    efd::RawFrame captured;
    std::atomic<int> frames{0};
    camera.setFrameCallback([&](const efd::RawFrame& frame) {
        if (frame.isSynthetic || !frame.isValid()) return;
        if (++frames == 30) {
            std::lock_guard<std::mutex> lock(mutex);
            captured = frame;
        }
    });
    if (!camera.start(deviceIndex)) {
        std::cerr << "無法開啟相機\n";
        return false;
    }
    for (int i = 0; i < 100 && frames.load() < 31; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    camera.stop();

    std::lock_guard<std::mutex> lock(mutex);
    if (!captured.isValid()) {
        std::cerr << "10 秒內沒有收到實體相機畫面 (收到 " << frames.load() << " 幀)\n";
        return false;
    }
    std::ofstream file(out, std::ios::binary);
    file << "P6\n" << captured.width << " " << captured.height << "\n255\n";
    file.write(reinterpret_cast<const char*>(captured.data.data()),
               static_cast<std::streamsize>(static_cast<size_t>(captured.width) * captured.height * 3));
    std::cout << "已擷取相機畫面 " << captured.width << "x" << captured.height << " -> " << out.string() << "\n";
    return true;
}

void collect(const fs::path& arg, std::vector<fs::path>& files) {
    if (fs::is_directory(arg)) {
        // 包含子資料夾：整個資料夾 (例如 debug_frames/ 下依日期分類的子資料夾)
        for (const auto& entry : fs::recursive_directory_iterator(arg)) {
            if (entry.is_regular_file() && entry.path().extension() == ".ppm") {
                files.push_back(entry.path());
            }
        }
    } else if (fs::is_regular_file(arg)) {
        files.push_back(arg);
    } else {
        std::cerr << "找不到: " << arg.string() << "\n";
    }
}

} // anonymous namespace

int main(int argc, char** argv) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif
    if (argc < 2) {
        std::cerr << "用法: efd_diag_replay <目錄或 .ppm 檔> [...]\n"
                  << "      efd_diag_replay --capture <輸出.ppm> [相機編號]   (用這台電腦的相機拍一張並判斷)\n";
        return 2;
    }

    std::vector<fs::path> files;
    if (std::string(argv[1]) == "--capture") {
        const fs::path out = fs::u8path(argc >= 3 ? argv[2] : "camera_capture.ppm");
        const int deviceIndex = (argc >= 4) ? std::atoi(argv[3]) : 0;
        if (!captureFromCamera(out, deviceIndex)) return 1;
        files.push_back(out);
    } else {
        for (int i = 1; i < argc; ++i) {
            collect(fs::u8path(argv[i]), files);
        }
    }
    std::sort(files.begin(), files.end());
    if (files.empty()) {
        std::cerr << "沒有可讀取的 .ppm 檔\n";
        return 2;
    }

    int passed = 0;
    for (const auto& file : files) {
        std::vector<uint8_t> rgb;
        int w = 0, h = 0;
        if (!readPpm(file, rgb, w, h)) {
            std::cerr << file.filename().string() << ": 不是有效的 P6 PPM\n";
            continue;
        }

        // 每張圖用全新的偵測器，只看單幀的原始判斷 (不受前後幀追蹤影響)
        efd::FaceLandmarker landmarker;
        landmarker.detect(rgb.data(), w, h, efd::PixelFormat::RGB888, false);
        const auto d = landmarker.getLatestPresenceDiagnostic();
        const bool ok = d.rawPersonInFrame && d.rawFacingCamera;
        if (ok) ++passed;

        std::cout << std::fixed << std::setprecision(3)
                  << "== " << file.parent_path().filename().string() << "/" << file.filename().string()
                  << " (" << w << "x" << h << ")\n"
                  << "  結果: " << (ok ? "有人臉" : "沒有人臉")
                  << (d.failStage.empty() ? "" : "  卡在: " + d.failStage) << "\n";
        if (!d.unconfirmedReason.empty() && !ok) {
            std::cout << "  原因: " << d.unconfirmedReason << "\n";
        }
        std::cout << "  膚色區域: x=" << d.skinBoxX << " y=" << d.skinBoxY
                  << " w=" << d.skinBoxW << " h=" << d.skinBoxH
                  << "  膚色像素=" << d.skinPixels
                  << "  長寬比=" << d.aspectRatio << "  填充率=" << d.fillDensity << "\n"
                  << "  臉部方框: x=" << d.faceBoxX << " y=" << d.faceBoxY
                  << " w=" << d.faceBoxW << " h=" << d.faceBoxH
                  << "  平均膚色亮度=" << d.avgSkinLum << "\n"
                  << "  眼睛中心: 左(" << d.eyeCenterX[0] << "," << d.eyeCenterY << ") 右("
                  << d.eyeCenterX[1] << "," << d.eyeCenterY << ")\n"
                  << "  左眼: 暗點=" << d.eyeDarkFraction[0] << " 反差=" << d.eyeContrast[0]
                  << " 邊緣=" << d.eyeGradient[0] << "\n"
                  << "  右眼: 暗點=" << d.eyeDarkFraction[1] << " 反差=" << d.eyeContrast[1]
                  << " 邊緣=" << d.eyeGradient[1] << "\n"
                  << "  左右對稱=" << d.symmetryRatio << "\n";
    }

    std::cout << "\n共 " << files.size() << " 張，判定有人臉 " << passed << " 張\n";
    return 0;
}
