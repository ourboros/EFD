# Eye Fatigue Detection (EFD) 系統架構設計規範 - C++ 跨平台原生架構

## 1. 系統簡介 (System Overview)

本研究旨在開發一款基於 **C++ 單一核心語言** 的跨平台眼睛疲勞偵測與干預應用程式（EFD Native App）。應用程式具備完整的跨平台能力，能在 **Android、iOS、macOS、Windows** 四大主流作業系統上以原生高效能模式運行。

系統採用本機端邊緣運算（純 Client-Side Edge Computing），透過裝置攝影機捕捉影像，融合「常規幾何統計特徵（EAR, PERCLOS）」與「非線性時序複雜度特徵（EMD, MSE）」，動態評估疲勞狀態並給予個人化提醒，同時安全管控為期 14 天的科研實驗數據生命週期。

---

## 2. 核心技術棧 (Tech Stack)

- **核心開發語言:** **Modern C++ (C++20)**（涵蓋 90% 以上業務邏輯、演算法、多執行緒管線、資料庫與平台抽象）
- **跨平台 GUI 框架:** **Qt 6 (C++ / QML)**（支援 Desktop Windows/macOS 與 Mobile Android/iOS 原生渲染）
- **視覺推論引擎:** **ONNX Runtime C++ / MediaPipe C++ Core**
  - _Windows 加速:_ DirectML / CPU (AVX2/AVX-512)
  - _macOS / iOS 加速:_ CoreML / Metal Execution Provider
  - _Android 加速:_ NNAPI / Vulkan Execution Provider
- **非線性訊號處理:** 原生 C++ 演算法庫（結合 **Eigen** 與 SIMD 指令集加速 EMD 與 MSE 運算）
- **本機結構化儲存:** **原生 SQLite3 (C++ API)** + **WAL (Write-Ahead Logging)** 模式（零資料遺失）
- **網路同步傳輸:** **libcurl / Qt Network (C++)**（安全傳輸科研數據與問卷 Token）

---

## 3. 系統分層架構 (Layered Architecture)

```mermaid
graph TB
    subgraph UI_Layer ["1. 表現與互動層 (UI Layer - Qt 6 QML / C++)"]
        Indicator[FatigueIndicator 浮動指標]
        Calib[CalibrationHelper 自適應校準]
        Gatekeeper[ExperimentalGatekeeper 14天門禁問卷]
        Tray[SystemTray & Notification 系統托盤/原生通知]
    end

    subgraph State_Layer ["2. 狀態與流程控制層 (C++ Core Controller)"]
        FSM[FatigueStateMachine 20/5/5 狀態機]
        Tracker[StudyWorkflowTracker 14天週期/解鎖驗證]
        Adapter[PlatformLifecycleAdapter 平台生命週期適配]
    end

    subgraph Engine_Layer ["3. 感知與運算引擎層 (C++ Pipeline Engine)"]
        Cam[CameraCaptureService 跨平台視訊擷取]
        Vision[VisionInferenceWorker 人臉 468 點特徵定位]
        Signals[FeatureExtractor & ComplexityEngine EMD/MSE/CI 運算]
    end

    subgraph Storage_Layer ["4. 本機資料與同步層 (C++ Data Layer)"]
        DB[DatabaseService SQLite3 WAL 本地資料庫]
        Sync[NetworkSyncWorker 條件觸發背景同步]
    end

    Cam --> Vision --> Signals --> FSM
    FSM --> Indicator
    FSM --> DB
    Tracker --> Gatekeeper
    Adapter --> FSM
    DB --> Sync
```

### 3.1 表現與互動層 (UI & Presentation Layer)

- **FatigueIndicator (疲勞指示器):** 桌面端提供置頂輕量半透明懸浮視窗 / 行動端提供動態狀態列與卡片，具備「放鬆（綠）」、「注意（黃）」、「警報升級（紅）」三段視覺反饋。
- **CalibrationHelper (動態校準引導):** 首次啟動引導用戶進行眼睛基準線校準，並持續在背景執行**動態滑動窗口基準自適應**，消除光線與鏡頭角度造成的特徵漂移。
- **ExperimentalGatekeeper (實驗門禁):** 第 14 天到期自動鎖定主功能介面，強制進入問卷填答流程。
- **SystemTray & Notification (原生通知適配):**
  - _Windows / macOS:_ 系統托盤圖示常駐、Windows Toast / macOS UserNotification 提示。
  - _Android / iOS:_ 原生本地推播通知 (Local Push Notifications)。

### 3.2 狀態機與流程控制層 (Business Logic & State Layer)

- **FatigueStateMachine (20/5/5 狀態機):** 嚴格調度偵測頻率，管理 20 分鐘冷卻期、5 分鐘輕量快篩與 5 分鐘離座清零邏輯。
- **StudyWorkflowTracker (實驗跟蹤器):** 追蹤 14 天實驗進程、維護實驗加密金鑰、管理問卷上傳與事務型解鎖驗證。
- **PlatformLifecycleAdapter (平台適配層):** 統一處理 Windows/macOS 睡眠喚醒、Android 前台服務 (Foreground Service) 與 iOS 生命週期切換（進入背景暫停、前景熱恢復）。

### 3.3 運算與感知引擎層 (Computation Engine Layer)

- **CameraCaptureService:** 封裝 Qt Multimedia (`QCamera`) / 原生平台 API，提供穩定 30 FPS 的 YUV/RGB 影像串流。
- **VisionInferenceWorker:** 透過 ONNX Runtime C++ 加載 Face Mesh 模型，即時輸出 468 點 3D 臉部特徵座標。
- **ComplexityEngine (C++ 核心):**
  - 幾何特徵：計算雙眼 $\text{EAR}$ (Eye Aspect Ratio)、$\text{PERCLOS}$、眨眼頻率。
  - 非線性特徵：利用 C++ 高效實作 **EMD (經驗模態分解)** 分離瞬態特徵，並透過 **MSE (多尺度熵)** 計算信號複雜度指數（$\text{CI}$）。

### 3.4 本機儲存與同步層 (Data & Persistence Layer)

- **DatabaseService (原生 SQLite3 C++):**
  - 啟用 `PRAGMA journal_mode=WAL;` 與 `PRAGMA synchronous=NORMAL;`，保證資料即時落盤且不阻塞查詢。
  - 保存每分鐘特徵統計、警報歷史、校準參數與問卷記錄。
- **NetworkSyncWorker (C++ 背景同步):** 當裝置滿足「連線 Wi-Fi + 夜間/充電中」條件時，將結構化資料安全上傳至研究中心伺服器。

---

## 4. 原生多執行緒非阻塞管線 (Threading & Pipeline Model)

為杜絕介面掉幀（Jank），系統採用全非同步管線模型，各執行緒職責明確：

```
[Camera Hardware]
       │
       ▼ (Raw Frames)
[Thread 1: Video Capture Thread] ──── (Lock-free Ring Buffer)
       │
       ▼ (Frame Buffer)
[Thread 2: Vision & Inference Worker] ─── (468 Landmarks)
       │
       ▼ (Time-Series Features)
[Thread 3: Signal Processing & State Worker] (EMD / MSE / 20-5-5 FSM)
       │                                  │
       ▼ (State Update Event)             ▼ (Batch Metrics)
[Thread 0: Main UI Thread]        [Thread 4: Database IO Worker]
  (60/120 FPS QML Render)            (SQLite WAL Commit)
```

1.  **Main UI Thread (執行緒 0):** 專注於 Qt Quick 畫面渲染與用戶點擊互動，零重度計算，確保 60/120 FPS 流暢度。
2.  **Video Capture Thread (執行緒 1):** 負責攝影機取幀，透過無鎖環形緩衝區（Lock-free Ring Buffer）傳遞最新影格，自動丟棄過期幀。
3.  **Vision & Inference Thread (執行緒 2):** 呼叫硬體加速（Metal/DirectML/NNAPI）執行神經網路推論。
4.  **Signal & State Worker (執行緒 3):** 累積時序訊號，執行 EMD/MSE 計算，驅動疲勞狀態機轉移。
5.  **Database Worker (執行緒 4):** 佇列化批次寫入 SQLite，完全消除磁碟 I/O 對運算與畫面的影響。

---

## 5. 核心演算法與動態防打擾邏輯

### 5.1 動態滑動窗口基準自適應 (Sliding-window Adaptive Baseline)

針對不同鏡頭角度與環境光線差異，系統採用動態自適應 EAR 閾值：
$$\text{EAR}_{\text{threshold}}(t) = \alpha \cdot \text{EAR}_{\text{baseline}} + (1 - \alpha) \cdot \mu_{\text{window}}(t) - k \cdot \sigma_{\text{window}}(t)$$

- $\text{EAR}_{\text{baseline}}$: 初始校準基礎值。
- $\mu_{\text{window}}, \sigma_{\text{window}}$: 過去 10 分鐘清醒狀態之滑動窗口平均值與標準差。
- $\alpha \in [0.6, 0.8]$: 動態衰減權重，自動抵消光線漸變引起的特徵漂移。
- **生理安全硬下限保護 (Physiological Clamp):** 閉眼狀態下停止侵蝕基準，且自適應動態閾值設有剛性安全下限 $\text{EAR}_{\text{threshold}} \ge 0.180$（上限 $\le 0.350$），杜絕閾值因長時間微閉眼單調下沉至 0.103 等不可達水平，確保閉眼與眨眼事件隨時可被靈敏捕捉。

### 5.2 20/5/5 防打擾與算力節能狀態機

本節為 20/5/5 的唯一規格來源，實作位於 `src/state/FatigueStateMachine.cpp`，並由 `tests/test_features.cpp` 第 5、13 項測試驗證。

- **判定分數 (Smoothed Score):** 每幀計算即時疲勞分數後，以時間常數 60 秒的指數加權移動平均 (EWMA) 得到平滑分數；疲勞等級與所有警報判定皆以平滑分數為準（$<45$ Relaxed、$\ge 45$ Attention、$\ge 65$ SevereWarning），避免揉眼、短暫誤判造成誤報。
- **觸發提醒 (Fatigue Alert):** 常態追蹤中，平滑分數 $\ge 65$ 且**持續 10 秒**才發送提醒，並進入 **20 分鐘冷卻期**。$45 \sim 65$ 只更新畫面狀態，不彈窗、不消耗冷卻。
- **冷卻期 (20m Cooldown):** 冷卻期間完全不提醒，但疲勞等級仍反映真實狀態。冷卻以真實時間倒數，使用者離座期間照常倒數。
- **快篩 (5m Fast Screening):** 冷卻期滿後進入快篩，以**每 5 分鐘在座時間**的平均即時分數判定一次：
  - 平均 $\ge 65$：再提醒一次並重新進入 20 分鐘冷卻；
  - 平均 $< 45$：回到常態追蹤；
  - 介於兩者之間：不提醒，繼續下一輪 5 分鐘快篩。
  - 窗口未滿 5 分鐘不提前判定。
- **離座自動歸零 (5m Auto-Reset):** 連續 5 分鐘未偵測到人臉，視為已休息：清除冷卻與快篩，平滑分數歸零，回到常態追蹤。App 在背景 (iOS) 或關閉期間比照離座計時。
- **提示靈敏度 (Sensitivity):** 只調整「提醒門檻」與「需持續秒數」，冷卻 20 分鐘、快篩 5 分鐘、離座 5 分鐘清零等時間規則不變；設定保存在設定檔的 `sensitivityLevel` 欄位 (由第 20 項測試驗證)。Windows / macOS / iOS 以開關切換標準與高，Android 可在三段間切換。

  | 等級 | 提醒門檻 (平滑分數) | 需持續 |
  | :--- | :--- | :--- |
  | 低 | $\ge 70$ | 15 秒 |
  | 標準 (預設) | $\ge 65$ | 10 秒 |
  | 高 | $\ge 58$ | 5 秒 |

  快篩期的「再次提醒」同樣使用該等級的門檻；「回到常態追蹤」的 $< 45$ 不隨等級改變。
- **持久化 (Persistence):** 冷卻狀態每 10 秒與暫停/停止時寫入 `efd_fsm_state.txt`；重啟時扣除關閉期間的時間繼續倒數，關閉滿 5 分鐘則比照離座清零。
- **高精度實時時鐘驅動 (Real-time Clock Delta):** 狀態機以 std::chrono::steady_clock 取得每幀真實時間增量（$\Delta t$，上限 2 秒），不以固定 1/FPS 估算。

### 5.3 眼睛特徵萃取與滑動窗口計算規範 (Eye Feature Extraction Pipeline)

- **目前的臉部與眼部偵測方式 (實作現況):** 尚未整合 MediaPipe / ONNX 特徵點模型。`FaceLandmarker` 以膚色網格密度聚類定位臉部，眼部需同時具備明顯暗點 (瞳孔/睫毛，亮度 < 膚色 60% 的像素 ≥ 4%)、反差 ≥ 20 與垂直梯度 ≥ 3 才判定為真人；連續 1 秒未偵測到才判定離座。此為啟發式方法，光線過暗或深色鏡框可能影響準確度。
- **AOI 二值化與瞳孔/睫毛黑色像素比例 (Black Pixels Ratio):** 在雙眼感興趣區域 (AOI) 計算黑色像素佔比 $P_{\text{black}}$；睜眼基準取最近 60 秒暗部比例的第 90 百分位 (可隨光線上下調整)，透過動態範圍映射獲得連續開闔度。
- **校準 (Calibration):** 兩段式收集，只採用校準期間偵測到人臉的幀；有效幀不足 60% 或少於 20 幀即校準失敗並保留原基準。閉眼閾值限制在清醒基準的 65%~85%，之後的自動調整只能在校準閾值 ±15% 內浮動。校準結果保存於設定檔，重啟後自動還原。
- **時間窗口 PERCLOS (Time-based PERCLOS):** 以**時間**定義的 60 秒窗口 (非固定幀數)，PERCLOS = 窗口內閉眼時間 / 窗口總時間；每個樣本代表的時間依實測幀率計算，手機 10~15 FPS 與桌機 30 FPS 結果一致。
- **生理有效眨眼檢驗 (Physiological Blink Validation):** 眨眼判定要求閉眼持續 60 ~ 800 ms (以時間判斷，與幀率無關)；窗口累積 30 秒前不輸出眨眼率 (不計分)；每分鐘眨眼率設置生理硬上限（[0.0, 60.0] 次/分）。
- **複雜度 (EMD + MSE):** 每秒一次，將 60 秒 EAR 序列依時間重新取樣為 10 Hz，以 EMD 去除最高頻 IMF 後計算 MSE (尺度 1~5)；資料不足 30 秒時使用清醒先驗值 CI = 4.5 (不計分)。
- **量表範圍與日誌一致性:** 疲勞評分統一採用標準 0.0 ~ 100.0 分制（注意閾值 45.0，警報閾值 65.0）；人臉在場診斷日誌使用動態符號（`>=` 或 `<` 及追蹤寬限標籤），杜絕日誌條件與數值矛盾。

---

## 6. 四大平台特性與常駐支援 (Cross-Platform Implementation)

```
┌──────────────────────────────────────────────────────────────────┐
│                   EFD Core C++20 Shared Library                  │
│       (Algorithms, Vision, State Machine, SQLite, Network)       │
└────────────────┬────────────────┬────────────────┬───────────────┘
                 │                │                │
        ┌────────┴───────┐ ┌──────┴────────┐ ┌─────┴──────────┐
        ▼                ▼ ▼               ▼ ▼                ▼
┌──────────────┐ ┌──────────────┐ ┌────────────────┐ ┌────────────────┐
│   Windows    │ │    macOS     │ │    Android     │ │      iOS       │
│ Qt Desktop   │ │ Qt Desktop   │ │ Qt Android/NDK │ │ Qt iOS/Obj-C++ │
│ WinToast     │ │ NSUserNotif  │ │ Foreground Svc │ │ Local Push     │
│ System Tray  │ │ Status Item  │ │ Camera2 / NDK  │ │ Hot-Resume Mgr │
└──────────────┘ └──────────────┘ └────────────────┘ └────────────────┘
```

> 註：上圖為原始規劃。目前各平台介面皆為原生實作 (Windows Win32 + GDI+、macOS Cocoa、Android View、iOS UIKit)，未使用 Qt。

| 平台        | 常駐機制 (Background Mode)                                            | 視訊擷取 (Camera Input)                   | 資料目錄 / 每日同步                               |
| :---------- | :-------------------------------------------------------------------- | :---------------------------------------- | :------------------------------------------------ |
| **Windows** | 最小化至系統托盤 (System Tray)，背景持續偵測                          | Media Foundation                          | 執行檔工作目錄；C++ 引擎 21:00 同步 + 補傳 (PowerShell) |
| **macOS**   | 頂部選單列狀態圖示 (Status Bar Item)，背景持續偵測                    | AVFoundation                              | `~/Library/Application Support/EFD`；C++ 引擎同步 + 補傳 (NSURLSession) |
| **Android** | **前景服務 (camera 類型)** 擁有偵測引擎，App 介面被劃掉後仍持續偵測與提醒；首次進入監控頁引導加入電池最佳化白名單。Android 14+ 不允許開機自動開相機，改發「點此開始今日偵測」通知 | CameraX (ImageAnalysis) → JNI | `filesDir`；Java `EfdSyncManager` 21:00 鬧鐘 + 補傳 + 失敗 10 分鐘重試 (C++ 端同步關閉) |
| **iOS**     | **iOS 不允許背景使用相機**：只在前景偵測；進入背景時暫停並保存冷卻狀態，排定一次休息提醒 (冷卻中延到冷卻結束) | AVFoundation / AVCaptureSession           | `Application Support/EFD`；開啟/回前景時補傳 + BGAppRefreshTask (執行時間由 iOS 決定) |

- **每日同步共同規則:** 上傳成功才標記已同步；失敗 10 分鐘後重試；21:00 前若發現昨天 (含) 以前的資料尚未上傳，立即補傳。
- **模擬相機:** 實體鏡頭無畫面時會切換為模擬相機以維持管線運作，但模擬影格不寫入研究資料庫。
- **上傳 JSON 格式 (四平台共用):** 由 `NetworkSyncWorker::buildPayloadJson()` 產生，Android 經 JNI 呼叫同一函式 (不附逐筆 `records`)。`metrics` 的 EAR、PERCLOS、CI 與 `fatigue_score` 為資料庫紀錄的實際平均；`blink_rate` 為有效眨眼率的平均 (窗口未滿 30 秒的 0 值不計入)；無紀錄時上述欄位為 `null`、`record_count` 為 0。`status` 為研究流程狀態 (`Active` / `Locked` / `Completed`)；`checksum` 由紀錄內容計算；`token` 為填寫後測問卷後產生的解鎖憑證，未取得時為空字串。資料庫每列新增第 8 欄 `BlinkRatePerMin`，讀取時相容舊版 7 欄資料。
- **開機自動啟動:** Windows 寫入 `HKCU\...\Run` (舊版另建的「啟動」資料夾捷徑會被清除，避免開機啟動兩次)；macOS 寫入 `~/Library/LaunchAgents/com.efd.EyeFatigueDetection.plist`；iOS 系統不允許，已移除該設定；Android 14+ 以開機通知代替。

---

## 7. 14 天實驗流程與事務型安全門禁 (14-Day Study Lifecycle)

1.  **Day 1 ~ Day 13 (正常實驗採樣期):**
    - 系統背景執行 20/5/5 偵測與提醒。
    - SQLite 持續記錄特徵時序資料（匿名 UUID 標記）。
2.  **Day 14 (期滿鎖定):**
    - `StudyWorkflowTracker` 偵測到實驗期滿，觸發 `ExperimentalGatekeeper`。
    - 鎖定所有疲勞偵測 UI，全螢幕呈現後測研究問卷。
3.  **事務型問卷上傳與安全解鎖 (Transactional Unlock):**
    - 用戶提交問卷 $\to$ `NetworkSyncWorker` 將「14天時序指標總結 + 問卷回答」打包上傳伺服器。
    - 伺服器驗證資料完整無缺後，回傳經數位簽章的 **Unlock Token**。
    - App 驗證 Token 成功後，解鎖本機 SQLite，並向受試者顯示「實驗完成代碼」與「軟體解除安裝安全指南」。

---

## 8. 模組代碼架構規劃 (Recommended Code Structure)

```
efd-core/
├── CMakeLists.txt                # 跨平台 CMake 建置腳本 (支援 Desktop / Android NDK / iOS)
├── src/
│   ├── main.cpp                  # 應用程式進入點與 Qt 實例化
│   ├── vision/                   # 視覺與特徵偵測
│   │   ├── CameraService.hpp/cpp # 跨平台相機串流封裝
│   │   └── FaceLandmarker.hpp/cpp# ONNX Runtime 人臉特徵推論
│   ├── analysis/                 # 訊號與數學運算
│   │   ├── FeatureExtractor.hpp  # EAR / PERCLOS / 眨眼特徵
│   │   ├── EmdCalculator.hpp/cpp # C++ 經驗模態分解
│   │   └── MseCalculator.hpp/cpp # 多尺度熵與 CI 複雜度指數
│   ├── state/                    # 狀態機與業務邏輯
│   │   ├── FatigueStateMachine.hpp/cpp # 20/5/5 狀態機
│   │   └── AdaptiveBaseline.hpp  # 滑動窗口動態基準
│   ├── storage/                  # 資料庫與持久化
│   │   └── DatabaseManager.hpp/cpp # SQLite3 WAL 原生封裝
│   ├── study/                    # 科研實驗控管
│   │   └── StudyLifecycle.hpp/cpp# 14 天計時與安全門禁
│   ├── platform/                 # 跨平台原生適配
│   │   ├── windows/              # Windows 托盤與通知
│   │   ├── macos/                # macOS 狀態列與通知
│   │   ├── android/              # Android Foreground Service / JNI
│   │   └── ios/                  # iOS 生命週期管理與 Push
│   └── ui/                       # Qt Quick / QML 介面
│       ├── qml/
│       │   ├── Main.qml
│       │   ├── FatigueIndicator.qml
│       │   ├── CalibrationDialog.qml
│       │   └── QuestionnaireGate.qml
│       └── UIController.hpp/cpp  # QML 與 C++ Core 資料綁定
```

---

## 9. 建議實作路線圖 (Implementation Roadmap)

- **階段一：C++ 原生運算核心與管線 (Core Pipeline MVP)**
  - 搭建 CMake 跨平台建置系統（支援 MSVC/Clang/Android NDK/Xcode）。
  - 整合 ONNX Runtime C++，完成相機取幀到 468 點特徵定位。
  - 實作 EAR、EMD、MSE 演算法，建立獨立單元測試 (Unit Tests)。
- **階段二：狀態機與多執行緒整合 (Multithreaded Engine)**
  - 建立 5 執行緒非同步管線（Ring Buffer）。
  - 實作 `FatigueStateMachine` (20/5/5) 與 `AdaptiveBaseline` 動態自適應。
- **階段三：資料持久化與科研流程 (Data & Study Control)**
  - 實作 SQLite3 WAL 本地資料庫服務，保證零資料遺失。
  - 實作 14 天實驗追蹤與 `ExperimentalGatekeeper` 事務解鎖。
- **階段四：Qt 6 UI 渲染與四大平台原生打磨 (Polishing & Deployment)**
  - 開發 QML 懸浮指標與校準 UI。
  - 完成 Windows 托盤、macOS 狀態列、Android 前台服務及 iOS 生命週期適配。
