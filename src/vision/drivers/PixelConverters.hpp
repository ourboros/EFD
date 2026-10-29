#pragma once

// 相機原始格式轉 RGB888 (與平台無關，供 Windows Media Foundation 驅動與單元測試共用)
//
// stride (每行位元組數) 由呼叫端提供：相機常在每行末端補齊位元組 (例如 640 寬補到 704)，
// 若直接用「寬 x 每像素位元組」計算位址，畫面會斜切成條紋。RGB 格式的 stride 可為負數，
// 代表影像由下而上存放 (Windows DIB 慣例)，此時 data 指向第一行 (畫面最上方) 的起點。
//
// 色彩轉換採 BT.601 全範圍公式 (與既有 YUY2 轉換一致)：
//   R = Y + 1.402 V'，G = Y - 0.344 U' - 0.714 V'，B = Y + 1.772 U'  (U' = U-128, V' = V-128)

#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace efd {
namespace pixel {

inline void yuvToRgb(int y, int u, int v, uint8_t* out) {
    out[0] = static_cast<uint8_t>(std::clamp(y + ((359 * v) >> 8), 0, 255));
    out[1] = static_cast<uint8_t>(std::clamp(y - ((88 * u + 183 * v) >> 8), 0, 255));
    out[2] = static_cast<uint8_t>(std::clamp(y + ((454 * u) >> 8), 0, 255));
}

// NV12：Y 平面 (height 行) 之後接 UV 交錯平面 (height/2 行)，兩者 stride 相同
inline void nv12ToRgb888(const uint8_t* nv12, int stride, int width, int height, uint8_t* rgb) {
    const uint8_t* yPlane = nv12;
    const uint8_t* uvPlane = nv12 + static_cast<ptrdiff_t>(stride) * height;
    for (int y = 0; y < height; ++y) {
        const uint8_t* yRow = yPlane + static_cast<ptrdiff_t>(stride) * y;
        const uint8_t* uvRow = uvPlane + static_cast<ptrdiff_t>(stride) * (y / 2);
        uint8_t* out = rgb + static_cast<ptrdiff_t>(width) * 3 * y;
        for (int x = 0; x < width; ++x) {
            const int c = x & ~1;
            yuvToRgb(yRow[x], uvRow[c] - 128, uvRow[c + 1] - 128, out + x * 3);
        }
    }
}

// YUY2：[Y0 U Y1 V]，每 2 個像素 4 位元組
inline void yuy2ToRgb888(const uint8_t* src, int stride, int width, int height, uint8_t* rgb) {
    for (int y = 0; y < height; ++y) {
        const uint8_t* row = src + static_cast<ptrdiff_t>(stride) * y;
        uint8_t* out = rgb + static_cast<ptrdiff_t>(width) * 3 * y;
        for (int x = 0; x + 1 < width; x += 2) {
            const uint8_t* p = row + x * 2;
            const int u = p[1] - 128, v = p[3] - 128;
            yuvToRgb(p[0], u, v, out + x * 3);
            yuvToRgb(p[2], u, v, out + (x + 1) * 3);
        }
    }
}

// UYVY：[U Y0 V Y1]，與 YUY2 長度相同但通道順序不同 (誤用 YUY2 解碼會使畫面全紅)
inline void uyvyToRgb888(const uint8_t* src, int stride, int width, int height, uint8_t* rgb) {
    for (int y = 0; y < height; ++y) {
        const uint8_t* row = src + static_cast<ptrdiff_t>(stride) * y;
        uint8_t* out = rgb + static_cast<ptrdiff_t>(width) * 3 * y;
        for (int x = 0; x + 1 < width; x += 2) {
            const uint8_t* p = row + x * 2;
            const int u = p[0] - 128, v = p[2] - 128;
            yuvToRgb(p[1], u, v, out + x * 3);
            yuvToRgb(p[3], u, v, out + (x + 1) * 3);
        }
    }
}

// RGB24 (記憶體排列 B G R)；stride 可為負數 (由下而上)
inline void bgr24ToRgb888(const uint8_t* src, int stride, int width, int height, uint8_t* rgb) {
    for (int y = 0; y < height; ++y) {
        const uint8_t* row = src + static_cast<ptrdiff_t>(stride) * y;
        uint8_t* out = rgb + static_cast<ptrdiff_t>(width) * 3 * y;
        for (int x = 0; x < width; ++x) {
            out[x * 3]     = row[x * 3 + 2];
            out[x * 3 + 1] = row[x * 3 + 1];
            out[x * 3 + 2] = row[x * 3];
        }
    }
}

// RGB32 (記憶體排列 B G R X)；stride 可為負數 (由下而上)
inline void bgraToRgb888(const uint8_t* src, int stride, int width, int height, uint8_t* rgb) {
    for (int y = 0; y < height; ++y) {
        const uint8_t* row = src + static_cast<ptrdiff_t>(stride) * y;
        uint8_t* out = rgb + static_cast<ptrdiff_t>(width) * 3 * y;
        for (int x = 0; x < width; ++x) {
            out[x * 3]     = row[x * 4 + 2];
            out[x * 3 + 1] = row[x * 4 + 1];
            out[x * 3 + 2] = row[x * 4];
        }
    }
}

// RGB888 整數倍縮小 (factor x factor 區塊平均)，維持長寬比；輸出 (width/factor) x (height/factor)
inline void downscaleRgb888(const uint8_t* src, int width, int height, int factor, uint8_t* dst) {
    const int outW = width / factor, outH = height / factor;
    const int area = factor * factor;
    for (int oy = 0; oy < outH; ++oy) {
        for (int ox = 0; ox < outW; ++ox) {
            int sum[3] = {0, 0, 0};
            for (int dy = 0; dy < factor; ++dy) {
                const uint8_t* row = src + (static_cast<ptrdiff_t>(oy * factor + dy) * width + ox * factor) * 3;
                for (int dx = 0; dx < factor; ++dx) {
                    sum[0] += row[dx * 3];
                    sum[1] += row[dx * 3 + 1];
                    sum[2] += row[dx * 3 + 2];
                }
            }
            uint8_t* out = dst + (static_cast<ptrdiff_t>(oy) * outW + ox) * 3;
            out[0] = static_cast<uint8_t>(sum[0] / area);
            out[1] = static_cast<uint8_t>(sum[1] / area);
            out[2] = static_cast<uint8_t>(sum[2] / area);
        }
    }
}

// 各格式一幀所需的最少位元組數 (|stride| 已含行尾補齊)；緩衝區不足時應略過該幀，避免越界讀取
enum class RawFormat { NV12, YUY2, UYVY, RGB24, RGB32, Unknown };

inline size_t requiredBytes(RawFormat format, int absStride, int height) {
    switch (format) {
    case RawFormat::NV12: return static_cast<size_t>(absStride) * height * 3 / 2;
    case RawFormat::YUY2:
    case RawFormat::UYVY:
    case RawFormat::RGB24:
    case RawFormat::RGB32: return static_cast<size_t>(absStride) * height;
    default: return 0;
    }
}

inline int minimumStride(RawFormat format, int width) {
    switch (format) {
    case RawFormat::NV12: return width;
    case RawFormat::YUY2:
    case RawFormat::UYVY: return width * 2;
    case RawFormat::RGB24: return width * 3;
    case RawFormat::RGB32: return width * 4;
    default: return 0;
    }
}

} // namespace pixel
} // namespace efd
