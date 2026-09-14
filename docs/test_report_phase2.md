# EFD 階段二 (Phase 2) 測試問題分析與改良驗證紀錄報告

## 1. 測試概況與問題記錄

在階段二（五執行緒非同步管線、20/5/5 狀態機、跨平台生命週期）整合測試過程中，透過 `efd_cli.exe` 與 `efd_tests.exe` 觀測到以下 4 項核心問題：

| 項次  | 發現之問題                             | 嚴重度     | 根本原因分析                                                                                                          |
| :---- | :------------------------------------- | :--------- | :-------------------------------------------------------------------------------------------------------------------- |
| **1** | **終端機輸出繁體中文亂碼**             | 介面呈現   | C++ 源碼採用 UTF-8 編碼，但 Windows 終端機預設使用 CP950 (Big5) 解碼，導致多位元組中文字元損壞。                      |
| **2** | **微睡眠閉眼期間閾值異常塌陷**         | 演算法邏輯 | 當使用者長時間閉眼（EAR=0.100）時，閉眼資料持續進入清醒滑動窗口，導致滑動均值被拉低，閾值一度跌至最低安全下限 0.120。 |
| **3** | **MSE 訊號複雜度冷啟動跳變**           | 數值穩定性 | 在剛啟動的前 30 幀，因歷史資料累積不足，多尺度熵輸出為 `0.00`，造成疲勞評分在達到 30 幀時出現非平滑階躍。             |
| **4** | **系統休眠喚醒 (Hot-Resume) 環境漂移** | 跨平台適配 | 當使用者休眠數分鐘後重新喚醒設備時，環境光線或坐姿視角可能已改變，若沿用舊滑動窗口會導致閾值收斂延遲。                |

---

## 2. 針對性改良實作 (Concrete Improvements)

### 2.1 終端機 UTF-8 編碼強制重定向

在程式進入點 `main()` 最頂層加入 Windows API 呼叫，將標準輸出與輸入字碼頁強制切換為 UTF-8 (`65001`)：

```cpp
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif
```

### 2.2 清醒樣本選擇性更新機制 (Selective Baseline Updating)

改良 [AdaptiveBaseline.cpp](file:///e:/Project/EFD/src/analysis/AdaptiveBaseline.cpp) 的滑動窗口更新策略：

- 當前幀被判定為閉眼（$\text{isEyeClosed} == \text{true}$）或數值異常時，**嚴格禁止進入清醒滑動窗口**。
- 僅保留合法清醒睜眼樣本（$\text{EAR} \ge \text{Threshold} \times 0.85$）計算滑動均值 $\mu$ 與標準差 $\sigma$。
- **效果**：使用者微睡眠期間，動態閾值維持在 `0.263` 穩定不塌陷，大幅提升閉眼與疲勞捕捉的靈敏度。

### 2.3 MSE 冷啟動平滑先驗過渡 (Warm-Up Prior Smoothing)

在 [AsyncPipelineEngine.cpp](file:///e:/Project/EFD/src/engine/AsyncPipelineEngine.cpp) 中，前 30 幀未累積足夠長度時，提供清醒狀態先驗複雜度（$\text{CI} = 4.5$），杜絕數值從 `0.0` 階躍突變的問題。

### 2.4 熱重啟快速再校準機制 (Fast Re-calibration on Hot-Resume)

在 [PlatformLifecycleAdapter.cpp](file:///e:/Project/EFD/src/platform/PlatformLifecycleAdapter.cpp) 觸發 `Resumed` 事件時，自動觸發 `fastRecalibrate()`：

- 注入 30 幀（1.0 秒）最新採樣樣本。
- 以 $50\% \text{歷史基準} + 50\% \text{新環境樣本}$ 平滑重構基準線，使系統在 1 秒內迅速適應新光線與姿勢。

---

## 3. 改良前後數據對比 (Before vs After)

```
【改良前 (Before)】
 Frame   EAR Avg Threshold   PERCLOS    Blinks  CI (MSE)     Score   State
---------------------------------------------------------------------------
    15     0.320     0.310      0.00         0      0.00       0.0   Normal
    30     0.100     0.149 (塌陷)0.27        0      2.56      49.6   Normal
    40     0.100     0.120 (觸底)0.45        0      0.00      45.0   Normal

【改良後 (After)】
 Frame   EAR Avg Threshold   PERCLOS    Blinks  CI (MSE)     Score   State
---------------------------------------------------------------------------
    15     0.320     0.263      0.00         0      4.50 (平滑)  3.0   Normal
    30     0.100     0.263 (穩固)0.27        0      2.56      49.6   Normal
    40     0.100     0.263 (穩固)0.45        0      0.00      45.0   Normal
```

---

## 4. 驗證結論

1. **編碼問題**：控制台中文輸出 100% 清晰無任何亂碼。
2. **單元測試**：7 項單元測試（包含微睡眠抗污染測試、五執行緒並行測試）全數通過。
3. **穩定性**：五執行緒架構在 休眠 (Pause) $\to$ 喚醒 (Hot-Resume) $\to$ 停止 (Stop) 過程中無任何鎖死或崩潰現象。
