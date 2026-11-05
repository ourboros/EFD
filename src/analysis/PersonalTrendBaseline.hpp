#pragma once

#include <cstddef>
#include <deque>

namespace efd {

// 個人基準：保存最近 maxMinutes 個「在座分鐘」的數值，取第 75 百分位作為這個人較清醒時的平常水準。
// 用於眼皮下垂 (睜眼 EAR) 與眼球移動 (速度、視線轉移次數)：這些數值因人、因手機拿法差很多
// (OPPO 實測一般使用的睜眼 EAR 只有校準值的 77%)，不能用固定門檻，只能和自己比。
// 疲勞是在使用中慢慢出現的，取較高的百分位讓疲勞的分鐘不容易把基準拉低。
// 累積不足 minMinutes 時 ready() 為 false，對應的分數不計分。
class PersonalTrendBaseline {
public:
    explicit PersonalTrendBaseline(size_t maxMinutes = 120, size_t minMinutes = 10, float percentile = 0.75f);

    void addMinute(float value);
    bool ready() const { return m_values.size() >= m_minMinutes; }
    float value() const { return m_value; }
    size_t count() const { return m_values.size(); }
    void reset();

private:
    std::deque<float> m_values;
    size_t m_maxMinutes;
    size_t m_minMinutes;
    float m_percentile;
    float m_value = 0.0f;
};

} // namespace efd
