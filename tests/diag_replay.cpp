// 診斷重播工具：把手機診斷模式匯出的畫面 (PPM, RGB) 逐張送進 FaceLandmarker，
// 印出每張卡在哪一關與各項數值，用來在電腦上重現手機上的偵測失敗。
//
// 用法：efd_diag_replay <目錄或 .ppm 檔> [...]
//       目錄會讀取其中 (含子資料夾) 所有 .ppm 檔 (例如專案根目錄的 debug_frames/)
//       efd_diag_replay --capture <輸出.ppm> [相機編號，預設 0]
//       用這台電腦的實體相機拍一張 (經過與 App 相同的驅動與色彩轉換)，存檔後一併判斷；
//       用來檢查不同電腦的鏡頭格式 (畫面黑白條紋、偏紅等問題)
//       efd_diag_replay --engine <目錄或 .ppm 檔> [...]
//       同一批圖片同時用啟發式偵測器與 ONNX 模型 (models/) 判斷，比較是否找到臉與 EAR
//       efd_diag_replay --csv <檔案> [校準睜眼 EAR，預設 0.26]
//       把一段 EAR 時間序列依 App 相同的流程重算疲勞分數 (P80 滾動基準、眨眼與長閉眼、
//       固定容許誤差的 MSE、20/5/5 狀態機)，每 30 秒印一列並標出提醒時間。可讀：
//         - 手機 logcat 的 EFD_Frame 行 (debug 版 App；adb logcat -s EFD_Frame > log.txt)
//         - CSV「秒,EAR[,有臉 0/1]」，第一列可為標題

#include "vision/FaceLandmarker.hpp"
#include "vision/CameraService.hpp"
#include "vision/OnnxFaceLandmarker.hpp"
#include "analysis/FeatureExtractor.hpp"
#include "analysis/RollingOpenBaseline.hpp"
#include "analysis/EmdCalculator.hpp"
#include "analysis/MseCalculator.hpp"
#include "state/FatigueStateMachine.hpp"

#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <sstream>
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

// 命令列參數為 UTF-8 (Windows 主控台已切換為 UTF-8)
fs::path argPath(const char* utf8) {
    const std::string bytes(utf8);
    return fs::path(std::u8string(bytes.begin(), bytes.end()));
}

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

// 由特徵點計算兩眼平均 EAR (與 App 相同的 MediaPipe 索引)
float earFromLandmarks(const std::vector<efd::Point3D>& landmarks) {
    efd::FeatureExtractor extractor;
    return extractor.processFrame(landmarks, 30.0f).earAvg;
}

// --engine：同一張圖比較啟發式偵測器與 ONNX 模型
int compareDetectors(const std::vector<fs::path>& files) {
    auto model = efd::createModelLandmarkDetector(efd::defaultModelDirectory());
    if (!model) {
        std::cerr << "載入 ONNX 模型失敗 (需要 onnxruntime.dll 與 models/ 在執行檔旁)\n";
        return 1;
    }
    int heuristicFaces = 0, modelFaces = 0, total = 0;
    std::cout << std::fixed << std::setprecision(3)
              << "檔案                                   啟發式(臉/EAR)    ONNX模型(臉/EAR)\n";
    for (const auto& file : files) {
        efd::RawFrame frame;
        if (!readPpm(file, frame.data, frame.width, frame.height)) continue;
        ++total;
        efd::FaceLandmarker heuristic;
        const auto h = heuristic.detect(frame);
        model->resetTracking();
        const auto m = model->detect(frame);
        heuristicFaces += h.hasFace ? 1 : 0;
        modelFaces += m.hasFace ? 1 : 0;
        std::string name = file.filename().string();
        if (name.size() > 36) name = name.substr(0, 33) + "...";
        std::cout << std::left << std::setw(39) << name << std::right
                  << (h.hasFace ? "有  " : "無  ") << std::setw(6) << (h.hasFace ? earFromLandmarks(h.landmarks) : 0.0f)
                  << "        " << (m.hasFace ? "有  " : "無  ") << std::setw(6) << (m.hasFace ? earFromLandmarks(m.landmarks) : 0.0f)
                  << "\n";
    }
    std::cout << "\n共 " << total << " 張：啟發式找到臉 " << heuristicFaces << " 張，ONNX 模型找到臉 " << modelFaces << " 張\n"
              << "(啟發式的 EAR 是依開合度推算的網格，模型的 EAR 是實際量測)\n";
    return 0;
}

struct EarSample {
    double t = 0.0;
    float ear = 0.0f;
    bool face = false;
};

// 讀取 logcat 的 EFD_Frame 行或「秒,EAR[,有臉]」CSV
std::vector<EarSample> readEarSeries(const fs::path& path) {
    std::vector<EarSample> out;
    std::ifstream in(path);
    std::string line;
    auto field = [](const std::string& l, const std::string& key) -> std::string {
        const size_t p = l.find(key);
        if (p == std::string::npos) return "";
        const size_t start = p + key.size();
        const size_t end = l.find_first_of(" \r", start);
        return l.substr(start, end == std::string::npos ? std::string::npos : end - start);
    };
    while (std::getline(in, line)) {
        try {
            if (line.find("EFD_Frame") != std::string::npos) {
                // 11-03 12:33:20.123 ... EFD_Frame: face=1 ear=0.0360 ...
                std::istringstream ss(line);
                std::string date, time;
                ss >> date >> time;
                int hh = 0, mm = 0;
                double sec = 0.0;
                if (std::sscanf(time.c_str(), "%d:%d:%lf", &hh, &mm, &sec) != 3) continue;
                EarSample s;
                s.t = hh * 3600.0 + mm * 60.0 + sec;
                s.face = field(line, "face=") == "1";
                s.ear = std::stof(field(line, "ear="));
                out.push_back(s);
            } else if (!line.empty() && (std::isdigit(static_cast<unsigned char>(line[0])) || line[0] == '.')) {
                std::istringstream ss(line);
                std::string a, b, c;
                std::getline(ss, a, ',');
                std::getline(ss, b, ',');
                std::getline(ss, c, ',');
                // 整欄都要是數字 (logcat 其他行也以日期數字開頭，不可誤讀)
                size_t usedT = 0, usedEar = 0;
                EarSample s;
                s.t = std::stod(a, &usedT);
                s.ear = std::stof(b, &usedEar);
                if (usedT != a.size() || usedEar != b.size()) continue;
                s.face = c.empty() ? s.ear > 0.0f : std::stoi(c) != 0;
                out.push_back(s);
            }
        } catch (const std::exception&) {
            // 格式不符的列略過
        }
    }
    return out;
}

std::string clockText(double t) {
    const int s = static_cast<int>(t);
    std::ostringstream o;
    o << std::setfill('0') << std::setw(2) << (s / 3600) % 24 << ":" << std::setw(2) << (s / 60) % 60
      << ":" << std::setw(2) << s % 60;
    return o.str();
}

// --csv：依 AsyncPipelineEngine 的訊號流程 (模型偵測器路徑) 重算
int replayEarSeries(const fs::path& path, float calibOpenEar) {
    const auto samples = readEarSeries(path);
    if (samples.size() < 2) {
        std::cerr << "讀不到 EAR 資料: " << path.string() << "\n";
        return 1;
    }
    efd::RollingOpenBaseline baseline(calibOpenEar);
    efd::FeatureExtractor extractor(60.0f, baseline.perclosThreshold());
    extractor.setBlinkThreshold(baseline.blinkThreshold());
    efd::EmdCalculator emd(4, 15, 0.05f);
    efd::MseCalculator mse(5, 2, 0.15f);
    efd::FatigueStateMachine fsm;
    std::vector<std::pair<double, float>> alerts;
    double now = samples.front().t;
    fsm.setAlertCallback([&](efd::FatigueLevel, float score, const std::string&) { alerts.emplace_back(now, score); });

    float measuredFps = 30.0f, absentSec = 0.0f, ci = 2.0f, ciTimer = 0.0f;
    double nextPrint = samples.front().t + 30.0;
    efd::EyeMetrics m;
    std::cout << std::fixed
              << "時間      PERCLOS 眨眼/分 眨眼ms 長閉眼/分  CI    門檻   平滑分數 狀態\n";
    for (size_t i = 1; i < samples.size(); ++i) {
        const EarSample& s = samples[i];
        now = s.t;
        const float dt = std::clamp(static_cast<float>(s.t - samples[i - 1].t), 0.001f, 2.0f);
        measuredFps = std::clamp(measuredFps * 0.9f + (1.0f / dt) * 0.1f, 5.0f, 60.0f);
        if (s.face) {
            absentSec = 0.0f;
            m = extractor.processEar(s.ear, s.ear, measuredFps);
            baseline.add(m.earAvg, dt);
            extractor.setEyeClosedThreshold(baseline.perclosThreshold());
            extractor.setBlinkThreshold(baseline.blinkThreshold());
            ciTimer += dt;
            if (ciTimer >= 1.0f) {
                ciTimer = 0.0f;
                std::vector<float> signal = extractor.getEarHistoryResampled(10.0f);
                if (signal.size() >= 300) {
                    efd::EmdResult r = emd.decompose(signal);
                    if (!r.imfs.empty() && r.imfs.front().size() == signal.size()) {
                        for (size_t k = 0; k < signal.size(); ++k) signal[k] -= r.imfs.front()[k];
                    }
                    const float range = std::max(0.05f, baseline.openEar() - efd::RollingOpenBaseline::kClosedEarFloor);
                    ci = mse.calculateComplexity(signal, 0.15f * range).complexityIndex;
                } else {
                    ci = 2.0f;
                }
            }
        } else {
            extractor.resetTemporaryCounters();
            absentSec += dt;
            if (absentSec >= 3.0f) extractor.reset();
            m = efd::EyeMetrics{};
            ciTimer = 0.0f;
        }
        const efd::SystemState st = fsm.update(s.face, m.perclos, m.blinkRatePerMin, s.face ? ci : 0.0f, dt,
                                               m.windowBlinkDurationMs, m.longClosuresPerMin);
        if (s.t >= nextPrint) {
            nextPrint = s.t + 30.0;
            const char* state = !s.face ? "未偵測到臉"
                : st.cooldownState == efd::CooldownState::InCooldown ? "冷卻中"
                : st.cooldownState == efd::CooldownState::FastScreening ? "快篩中" : "";
            std::cout << clockText(s.t) << "  " << std::setprecision(2) << std::setw(5) << m.perclos
                      << std::setprecision(1) << std::setw(8) << m.blinkRatePerMin
                      << std::setprecision(0) << std::setw(7) << m.windowBlinkDurationMs
                      << std::setprecision(1) << std::setw(9) << m.longClosuresPerMin
                      << std::setprecision(2) << std::setw(7) << ci
                      << std::setprecision(3) << std::setw(7) << baseline.perclosThreshold()
                      << std::setprecision(1) << std::setw(9) << st.smoothedFatigueScore << "  " << state << "\n";
        }
    }
    std::cout << "\n共 " << samples.size() << " 筆，" << clockText(samples.front().t) << " ~ " << clockText(samples.back().t) << "\n";
    if (alerts.empty()) {
        std::cout << "期間沒有觸發疲勞提醒\n";
    }
    for (const auto& a : alerts) {
        std::cout << "疲勞提醒: " << clockText(a.first) << " (分數 " << std::setprecision(1) << a.second << ")\n";
    }
    return 0;
}

} // anonymous namespace

int main(int argc, char** argv) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif
    if (argc < 2) {
        std::cerr << "用法: efd_diag_replay <目錄或 .ppm 檔> [...]\n"
                  << "      efd_diag_replay --capture <輸出.ppm> [相機編號]   (用這台電腦的相機拍一張並判斷)\n"
                  << "      efd_diag_replay --engine <目錄或 .ppm 檔> [...]   (比較啟發式與 ONNX 模型)\n"
                  << "      efd_diag_replay --csv <EFD_Frame 記錄或 CSV> [校準睜眼 EAR]   (重算疲勞分數)\n";
        return 2;
    }

    const std::string mode = argv[1];
    if (mode == "--csv") {
        if (argc < 3) return 2;
        const float openEar = (argc >= 4) ? std::stof(argv[3]) : 0.26f;
        return replayEarSeries(argPath(argv[2]), openEar);
    }

    std::vector<fs::path> files;
    if (mode == "--engine") {
        for (int i = 2; i < argc; ++i) {
            collect(argPath(argv[i]), files);
        }
        std::sort(files.begin(), files.end());
        return compareDetectors(files);
    }
    if (mode == "--capture") {
        const fs::path out = argPath(argc >= 3 ? argv[2] : "camera_capture.ppm");
        const int deviceIndex = (argc >= 4) ? std::atoi(argv[3]) : 0;
        if (!captureFromCamera(out, deviceIndex)) return 1;
        files.push_back(out);
    } else {
        for (int i = 1; i < argc; ++i) {
            collect(argPath(argv[i]), files);
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
