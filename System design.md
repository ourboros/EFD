# Eye Fatigue Detection (EFD) System Design - React Web Architecture

## 1. 系統簡介
本研究旨在開發一款基於 **React Web 架構** 的眼睛疲勞偵測提醒應用程式（EFD Web App）。應用程式完全在使用者瀏覽器端運作（純 client-side），透過一般前鏡頭，融合「常規統計特徵」與「WebAssembly 計算之複雜度特徵」，動態判定疲勞狀態並給予個人化提醒，同時安全控管為期 14 天的研究實驗數據[cite: 1.1.1, 1.1.2]。

## 2. 核心技術棧 (Tech Stack)

*   **UI 框架:** **React (Web)**
*   **語言:** TypeScript / JavaScript / C++ (轉 WASM) [cite: 1.2.2, 1.3.1]
*   **眼球偵測:** **MediaPipe Face Landmarker for JavaScript SDK** [cite: 1.3.2]
*   **計算引擎:** EMD/MSE C++ 演算法 compiled to **WebAssembly (Wasm)** [cite: 1.2.2, 1.4.3]
*   **本機儲存:** SQLite Wasm (`sql.js`) + 持久化至 **IndexedDB** [cite: 1.4.3]
*   **跨平台交付:** 瀏覽器 PWA (Progressive Web App)

## 3. 系統模組與運作邏輯

### 3.1 使用者介面層 (UI Layer) - React Components
負責純介面呈現、與使用者互動、以及介面狀態同步：
*   **CalibrationHelper (校準引導):** React 全螢幕組件，引導使用者進行視覺校準，並將 Baseline 寫入儲存層。
*   **FatigueIndicator (提醒浮燈):** 輕量級 React 組件，懸浮於所有 UI 之上，依據邏輯層狀態機顯示「放鬆」、「警告」、「警報升級」狀態[cite: 1.1.2]。
*   **ExperimentalGatekeeper (實驗門禁):** 第 14 天觸發，鎖定核心 UI，只顯示問卷系統。

### 3.2 狀態機與邏輯層 (Business Logic) - React Hooks & Context
負責所有非 UI 的流程控制、計時器管理、與事務型事務：
*   **FatigueStateMachine (疲勞狀態機):** 管理 20 分鐘冷卻期、5 分鐘低頻快篩、離座 5 分鐘清零邏輯[cite: 1.1.1]。
*   **StudyWorkflowTracker (實驗流程跟蹤):** 計算 14 天實驗進度、事務型安全解鎖（問卷 Token 驗證）[cite: 1.2.2]。
*   **useMediaPipe (眼偵測 Hook):** 初始化 MediaPipe，連接 `<video>`，將偵測到的特徵點資料發送至分析引擎。

### 3.3 疲勞分析引擎 (Fatigue Analysis Engine) - Wasm Core
負責高效能運算：
*   **Wasm Calculator:** C++ 編譯之 WebAssembly 模組，接收人臉特徵點座標，計算 EAR（眼睛縱橫比）、EMD（經驗模態分解）、MSE（多尺度熵），輸出個人化自適應疲勞指數。

### 3.4 資料與同步層 (Data & Sync)
*   **SQLiteWasmService (`sql.js`):** 本機端 SQLite 運作，負責 SQL 特徵指標統計及問卷儲存[cite: 1.4.3]。
*   **PersistenceBridge (持久化橋接器):** 定期將記憶體中的 SQLiteWasm 二進位資料 export 並存入 **IndexedDB**（防止瀏覽器關閉導致數據流失）[cite: 1.4.3]。
*   **DataSyncWorker (背景同步):** 條件觸發（夜間+Wi-Fi+充電），將本機數據上傳至研究資料庫。

## 4. 資料流 (Data Flow)

1.  **輸入:** `<video>` 藉由 `MediaDevices API` 擷取影像 [cite: 1.3.1]。
2.  **眼動特徵捕捉:** `MediaPipe JS` 處理影像，輸出 468 個特徵點 [cite: 1.3.2]。
3.  **Wasm 高速運算:** `MediaPipe` 資料被 `Dart FFI` (在 JS 中即直接呼叫 Wasm 介面) 發送至 `Wasm Calculator`。計算 EAR, EMD, MSE, CI 指數[cite: 1.2.2]。
4.  **邏輯判定:** `React Context` 更新分析引擎的指數。`FatigueStateMachine` 計時並判定是否觸發提醒[cite: 1.1.1]。
5.  **介面呈現:** `FatigueIndicator` React 組件更新狀態。
6.  **儲存:** 資料存入記憶體 SQLite (`sql.js`) [cite: 1.4.3]。
7.  **持久化:** 每 5 分鐘（或關閉前）`PersistenceBridge` 將 SQLite Wasm 二進位導出至 IndexedDB [cite: 1.4.3]。

## 5. 系統冷卻防打擾邏輯（20/5/5）
此邏輯由 React Hooks (`useEffect` 結合計時器) 嚴格控制：
*   **Reminder Condition:** Fatigue Index > Threshold. Display reminder, start 20m timer [cite: 1.1.1].
*   **20m Cooldown (Downsampling):** Every 5m, fast screen (blink frequency/PERCLOS only).
    *   *If rising:* Upgrade reminder intensity.
*   **User Idle (Auto-Reset):** If user face disappears for 5m.
    *   *If inactive:* Reset 20m timer to 0 [cite: 1.1.1].
    *   *Return:* Restart full analysis engine.

## 6. 14 天實驗結束偵測與事務型安全門禁（Transactional Gate）
*   **期滿控制:** 第 14 天背景計時器觸發，React UI 鎖定。
*   **強制作業:** 強制顯示問卷 React Component [cite: 1.2.2]。
*   **事務處理:** 使用者點擊提交問卷：背景上傳問卷數據 + User ID[cite: 1.2.2]。
*   **金鑰發放:** 純本機接收伺服器 Token 驗證成功，本機 SQLite 解鎖，並顯示解除安裝安全金鑰與引點機制（UNINSTALL KEY & GUIDE）[cite: 1.2.2]。

🚀 建議的開發步驟順序 (Recommended Implementation Flow)
為了避免工程積壓，建議按照數據流和依賴性來分階段開發：

階段一：基礎骨架與數據流 (MVP)
實現 Video Input ($\to$ MediaPipe JS)。
Mock Wasm 的輸出（先不連 C++，直接在 JS 模擬出一個隨機的疲勞指數）。
建立 React 的基礎佈局和狀態上下文。
實現基礎的本地數據寫入（模擬指標寫入 IndexedDB）。
階段二：核心邏輯實現 (The Brain)
整合 Wasm 核心：將實際的 C++ $\to$ Wasm 通訊鏈路搭建起來，確保計算的穩定性。
實作 FatigueStateMachine：加入時間計時和狀態轉換的邏輯。
階段三：健壯性與流程控制 (Robustness)
實作 PersistenceBridge：確保數據能可靠地從記憶體轉移到 IndexedDB。
實作 ExperimentalGatekeeper 和相關的 14 天計時邏輯。
階段四：優化與部署 (Polish)
優化 UI/UX：添加視覺動畫和用戶提示。
實作 DataSyncWorker：實現條件觸發的背景上傳。
簡而言之，你需要製作的不是單個功能，而是一個高度耦合、多層次的、從硬體輸入到計算到持久化儲存的完整計算管線 (Pipeline)。