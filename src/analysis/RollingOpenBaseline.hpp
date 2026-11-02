#pragma once

#include <deque>

namespace efd {

// 模型偵測器 (MediaPipe / ONNX) 的閉眼門檻：以「最近 2 分鐘的睜眼 EAR」為準，依文獻 PERCLOS P80 定義閉眼。
// - PERCLOS 閉眼：眼瞼閉合 >= 80%，EAR < 谷底 + 0.2 x (睜眼 - 谷底)
// - 眨眼：閉合 >= 50%，EAR < 谷底 + 0.5 x (睜眼 - 谷底)；10~15 FPS 不一定取樣到眨眼谷底，門檻較寬
// 舊做法 (校準平均 - 1.5 個標準差) 只代表閉合約 27%，三星 A71 實測低頭看手機時 64% 的幀被判成閉眼；
// 以同一段實測逐幀資料模擬 P80 + 滾動睜眼基準：一般看螢幕 0~5%、低頭 20%
class RollingOpenBaseline {
public:
    static constexpr float kClosedEarFloor = 0.05f;      // A71 實測完全閉眼 / 眨眼谷底 EAR 約 0.04~0.07
    static constexpr float kPerclosClosure = 0.8f;       // P80
    static constexpr float kBlinkClosure = 0.5f;
    static constexpr float kWindowSeconds = 120.0f;
    static constexpr float kMinSeconds = 10.0f;          // 累積不足時沿用校準的睜眼 EAR
    static constexpr float kExcludeLowestFraction = 0.2f; // 計算睜眼基準時排除最低 20% (眨眼與閉眼)

    explicit RollingOpenBaseline(float initialOpenEar = 0.26f);

    // 校準得到的睜眼 EAR，累積不足時使用
    void setInitialOpenEar(float ear);
    // 加入一個在座樣本 (dtSeconds 為此樣本代表的時間)
    void add(float ear, float dtSeconds);
    void reset();

    float openEar() const { return m_openEar; }
    float perclosThreshold() const;
    float blinkThreshold() const;

private:
    struct Sample {
        float ear;
        float dt;
    };
    std::deque<Sample> m_samples;
    float m_totalSeconds = 0.0f;
    float m_initialOpenEar;
    float m_openEar;
    float m_sinceRecompute = 0.0f;

    void recompute();
};

} // namespace efd
