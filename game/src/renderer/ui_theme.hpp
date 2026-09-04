#pragma once

// Central design tokens for the UI — classic Minecraft skin.
// Every UI draw should source its colors from here instead of inlining
// magic RGBA tuples, so the look stays consistent and rethemable.

#include <cstdint>

namespace mc::ui {

// Panel (#C6C6C6 family) — inventory / menu surfaces.
inline constexpr uint8_t kPanelR = 198, kPanelG = 198, kPanelB = 198, kPanelA = 248;
inline constexpr uint8_t kBevelLight = 255;              // top/left edge
inline constexpr uint8_t kBevelDark = 85;                // bottom/right edge (#555555)
inline constexpr uint8_t kOutline = 16;                  // 1px black-ish frame

// Slot inset (#8B8B8B family) — dark inset with reversed bevel.
inline constexpr uint8_t kSlotR = 139, kSlotG = 139, kSlotB = 139;
inline constexpr uint8_t kSlotShadowR = 55, kSlotShadowG = 55, kSlotShadowB = 55;

// Hotbar: dark translucent band with a gray frame (in-game HUD).
inline constexpr uint8_t kHotbarR = 12, kHotbarG = 12, kHotbarB = 14, kHotbarA = 170;
inline constexpr uint8_t kHotbarFrameR = 160, kHotbarFrameG = 160, kHotbarFrameB = 165;

// Buttons — flat gray slab, light/dark bevel, black outline.
inline constexpr uint8_t kBtnR = 125, kBtnG = 127, kBtnB = 125;
inline constexpr uint8_t kBtnHoverR = 118, kBtnHoverG = 132, kBtnHoverB = 168;
inline constexpr uint8_t kBtnPressR = 96, kBtnPressG = 98, kBtnPressB = 96;

// Text.
inline constexpr uint8_t kTextLight = 224;               // default light text
inline constexpr uint8_t kTextHoverR = 255, kTextHoverG = 255, kTextHoverB = 160;
inline constexpr uint8_t kTextDark = 60;                 // on light panels
inline constexpr uint8_t kTextShadowAlpha = 130;         // drop shadow under text

// Accents.
inline constexpr uint8_t kGoldR = 255, kGoldG = 215, kGoldB = 120;      // quest/XP accents
inline constexpr uint8_t kHealthR = 235, kHealthG = 40, kHealthB = 40;
inline constexpr uint8_t kXpR = 118, kXpG = 225, kXpB = 60;

// Standard alpha steps (replace the scattered 50/80/100/120/160 literals).
inline constexpr uint8_t kAlphaGhost = 50;
inline constexpr uint8_t kAlphaSoft = 90;
inline constexpr uint8_t kAlphaPanel = 140;
inline constexpr uint8_t kAlphaSolid = 220;

} // namespace mc::ui
