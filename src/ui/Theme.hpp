#pragma once

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <gdiplus.h>
#endif

namespace efd {
namespace theme {

// 專案五大核心標準色盤
// 1. 翡翠綠 (Primary Mint / Emerald): 0x1EB18A -> RGB(30, 177, 138)
// 2. 柔和天藍 (Accent Light Blue):     0x96C5F7 -> RGB(150, 197, 247)
// 3. 純白 (Pure White):                0xFFFFFF -> RGB(255, 255, 255)
// 4. 溫暖鵝黃 (Warning Vanilla Gold):   0xF7E3AF -> RGB(247, 227, 175)
// 5. 深墨黑曜 (Dark Slate / Charcoal): 0x25291C -> RGB(37, 41, 28)

#ifdef _WIN32
inline Gdiplus::Color Emerald(BYTE a = 255) { return Gdiplus::Color(a, 30, 177, 138); }
inline Gdiplus::Color LightBlue(BYTE a = 255) { return Gdiplus::Color(a, 150, 197, 247); }
inline Gdiplus::Color PureWhite(BYTE a = 255) { return Gdiplus::Color(a, 255, 255, 255); }
inline Gdiplus::Color VanillaGold(BYTE a = 255) { return Gdiplus::Color(a, 247, 227, 175); }
inline Gdiplus::Color DarkCharcoal(BYTE a = 255) { return Gdiplus::Color(a, 37, 41, 28); }

// 開關切換專屬色 (依據使用者規範之五色盤映射)
// 開啟 (ON): 軌道使用薄荷翡翠底色 (0x1EB18A 帶透明度/淺化)，滑塊使用實心翡翠 0x1EB18A
// 關閉 (OFF): 軌道使用深色底 0x25291C (半透明)，滑塊使用溫和白/金 0xFFFFFF / 0xF7E3AF
#endif

} // namespace theme
} // namespace efd
