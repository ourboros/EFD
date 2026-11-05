#pragma once

#include "efd/types.hpp"
#include <deque>
#include <vector>

namespace efd {

// 眼球移動特徵：以 MediaPipe 478 點中的虹膜中心 (468 / 473) 相對兩側眼角的位置估計視線，
// 除以眼角距離 (眼寬) 後與頭部遠近、畫面解析度無關，頭部平移也大致抵銷。
// 疲勞時眼球掃視變慢、視線轉移變少、注視變得單調，以 60 秒窗口統計：
// - speed：視線每秒移動幾個眼寬 (10 Hz 重新取樣、3 點中位數濾波後的相鄰位移總和 / 時間)
// - shiftsPerMin：視線在 0.1 秒內跳動超過 kShiftThreshold 個眼寬的次數/分 (連續跳動只算一次)
// 閉眼與眨眼的幀虹膜位置不可靠，不納入計算；不到 478 點 (啟發式偵測器) 時沒有資料。
// 只用特徵點座標，不使用影像。
class GazeTracker {
public:
    static constexpr float kWindowSeconds = 60.0f;
    static constexpr float kMinValidSeconds = 30.0f;
    static constexpr float kResampleHz = 10.0f;
    static constexpr float kShiftThreshold = 0.06f;   // 0.1 秒內移動超過 6% 眼寬視為一次視線轉移
    static constexpr float kJitter = 0.004f;          // 特徵點抖動：每 0.1 秒 0.4% 眼寬以內不算移動

    struct Metrics {
        bool  ready = false;          // 窗口內有效 (睜眼) 時間 >= 30 秒
        float speed = 0.0f;           // 眼寬/秒
        float shiftsPerMin = 0.0f;
        float validSeconds = 0.0f;
    };

    // 由 478 點計算正規化視線位置 (兩眼平均)；點數不足或眼寬過小回傳 false
    static bool computeGaze(const std::vector<Point3D>& landmarks, float& gx, float& gy);

    // 加入一幀：eyeOpen = false (閉眼、眨眼) 時只推進時間，不採用虹膜位置
    void addFrame(const std::vector<Point3D>& landmarks, bool eyeOpen, float dtSeconds);
    void addSample(bool valid, float gx, float gy, float dtSeconds);

    Metrics compute() const;
    void reset();

private:
    struct Sample {
        bool valid;
        float gx;
        float gy;
        float dt;
    };
    std::deque<Sample> m_samples;
    float m_totalSeconds = 0.0f;
};

} // namespace efd
