#pragma once

#include <cstdint>
#include <vector>

#include "gameplay/item.hpp"
#include "world/block.hpp"

namespace mc {

class TextureAtlas;

// Procedural inventory icon atlas. One 32x32 cell per item:
//  - block items (id < BLOCK_COUNT) render as isometric cube previews built
//    from the block's own atlas tiles (cross/plant/liquid blocks render flat),
//  - tool/material/food items render as hand-designed pixel art.
// Cell index for an item: block items map 1:1 by id, registry items map to
// BLOCK_COUNT + (id - 256). See icon_cell().
class ItemIcons {
public:
    static constexpr int ICON_PX = 32;
    static constexpr int ATLAS_COLS = 16;

    // CPU generation. Samples tile pixels from the given generated atlas.
    void generate(const TextureAtlas& atlas);

    // Upload to GL as a RGBA8 texture. Requires an active GL context.
    void upload();
    void shutdown();
    void bind(uint32_t slot = 0) const;

    [[nodiscard]] uint32_t gl_texture() const { return gl_tex_; }
    [[nodiscard]] int rows() const { return rows_; }
    // UV rect of an item's cell inside the atlas.
    void uv_for(ItemId id, float& u0, float& v0, float& u1, float& v1) const;
    // UV rect of an arbitrary atlas cell (HUD sprites live past the items).
    void uv_for_cell(int cell, float& u0, float& v0, float& u1, float& v1) const;

    // CPU-side RGBA buffer (offline asset dumping / QA).
    [[nodiscard]] const std::vector<uint8_t>& rgba_data() const { return pixels_; }

private:
    std::vector<uint8_t> pixels_;
    int rows_ = 0;
    uint32_t gl_tex_ = 0;
};

// Atlas cell index for an item id (block ids map 1:1, registry items offset).
[[nodiscard]] inline int icon_cell(ItemId id) {
    return id < 256 ? static_cast<int>(id)
                    : static_cast<int>(BLOCK_COUNT) + (static_cast<int>(id) - 256);
}

// HUD stat sprites painted after the item cells (hearts, hunger, armor...).
enum UiSprite {
    kSpriteHeartFull = 112,
    kSpriteHeartHalf,
    kSpriteHeartEmpty,
    kSpriteHungerFull,
    kSpriteHungerHalf,
    kSpriteHungerEmpty,
    kSpriteArmorFull,
    kSpriteArmorHalf,
    kSpriteArmorEmpty,
    kSpriteBubble,
    kSpriteCount,
};

} // namespace mc
