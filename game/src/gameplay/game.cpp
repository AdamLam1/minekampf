#include "gameplay/game.hpp"
#include <sstream>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>
#ifdef _WIN32
#include <direct.h>
#endif

#include <glad/gl.h>
#include <GLFW/glfw3.h>

#include "world/world.hpp"
#include "save/level_storage.hpp"
#include "core/event_bus.hpp"
#include "core/config.hpp"
#include "core/logger.hpp"
#include "core/math.hpp"
#include "gameplay/block_interaction.hpp"
#include "gameplay/survival.hpp"
#include "physics/movement.hpp"
#include "physics/raycast.hpp"
#include "renderer/mesh_builder.hpp"
#include "renderer/ui_theme.hpp"
#include "world/lighting.hpp"
#include "core/profiler.hpp"
#include "core/mod_manager.hpp"
#include "gameplay/combat.hpp"

namespace mc {

namespace {
// Enabled with MINEKAMPF_AI_DIAG=1; routed to the game log.
void ai_diag(const char* fmt, ...) {
    static const bool on = std::getenv("MINEKAMPF_AI_DIAG") != nullptr;
    if (!on) return;
    char buf[256];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    spdlog::info("[ai] {}", buf);
}

float day_brightness(float t) {
    // t in [0,1): 0=midnight, 0.5=noon. Smooth cosine curve.
    float b = 0.5f - 0.5f * std::cos(t * 6.2831853f);
    return std::max(0.06f, b); // floor so night isn't pitch black

}
BlockId block_id_by_name(const std::string& name) {
    std::string n;
    n.reserve(name.size());
    for (char c : name) n += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    for (BlockId id = 0; id < BLOCK_COUNT; ++id) {
        if (BLOCK_PROPERTIES_TABLE[id].name == n) return id;
    }
    return BLOCK_AIR;
}
ItemId item_id_by_name(const std::string& name) {
    BlockId b = block_id_by_name(name);
    if (b != BLOCK_AIR) return static_cast<ItemId>(b);
    std::string n;
    n.reserve(name.size());
    for (char c : name) n += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    for (ItemId id = 1; id < ITEM_COUNT; ++id) {
        if (ItemRegistry::get(id).name == n) return id;
    }
    return ITEM_AIR;
}

// Shared icon colors so HUD hotbar and inventory render items identically.
void item_icon_color(ItemId id, uint8_t& r, uint8_t& g, uint8_t& b) {
    r = 128; g = 128; b = 128;
    if (id == BLOCK_GRASS) { r = 90; g = 150; b = 60; }
    else if (id == BLOCK_DIRT) { r = 134; g = 96; b = 67; }
    else if (id == BLOCK_STONE) { r = 130; g = 130; b = 130; }
    else if (id == BLOCK_COBBLESTONE) { r = 110; g = 110; b = 110; }
    else if (id == BLOCK_OAK_PLANKS || id == BLOCK_CRAFTING_TABLE) { r = 160; g = 130; b = 80; }
    else if (id == BLOCK_FURNACE) { r = 100; g = 100; b = 105; }
    else if (id == BLOCK_QUEST_NPC) { r = 210; g = 180; b = 90; }
    else if (id == ITEM_BOW) { r = 150; g = 110; b = 60; }
    else if (id == ITEM_ARROW) { r = 200; g = 200; b = 205; }
    else if (id == ITEM_COOKED_MEAT) { r = 165; g = 90; b = 50; }
    else if (id == ITEM_CHARCOAL) { r = 45; g = 45; b = 50; }
    else if (id == BLOCK_OAK_LOG) { r = 120; g = 90; b = 50; }
    else if (id == BLOCK_GLASS) { r = 210; g = 230; b = 240; }
    else if (id == BLOCK_OBSIDIAN) { r = 30; g = 25; b = 40; }
    else if (id == BLOCK_NETHERRACK) { r = 110; g = 50; b = 50; }
    else if (id == BLOCK_GLOWSTONE) { r = 220; g = 200; b = 120; }
    else if (id == BLOCK_REDSTONE_WIRE) { r = 200; g = 30; b = 30; }
    else if (id == BLOCK_REDSTONE_TORCH || id == ITEM_COAL) { r = 40; g = 40; b = 45; }
    else if (id == BLOCK_NETHER_PORTAL) { r = 150; g = 50; b = 220; }
    else if (id == BLOCK_END_PORTAL) { r = 20; g = 10; b = 30; }
    else if (id == BLOCK_TORCH) { r = 255; g = 180; b = 60; }
    else if (id == BLOCK_CACTUS) { r = 70; g = 140; b = 62; }
    else if (id == BLOCK_SAND) { r = 235; g = 215; b = 150; }
    else if (id == BLOCK_SPRUCE_LOG || id == BLOCK_BIRCH_LOG) { r = 120; g = 90; b = 50; }
    else if (id == BLOCK_SNOW) { r = 245; g = 248; b = 255; }
    else if (id == BLOCK_WATER) { r = 40; g = 200; b = 220; }
    else if (id == ITEM_APPLE) { r = 220; g = 40; b = 40; }
    else if (id == ITEM_RAW_MEAT) { r = 230; g = 110; b = 110; }
    else if (id == ITEM_STICK) { r = 150; g = 115; b = 65; }
    else if (id == ITEM_IRON_INGOT || id == ITEM_WOODEN_SWORD || id == ITEM_STONE_SWORD ||
             id == ITEM_IRON_SWORD || id == ITEM_DIAMOND_SWORD || id == ITEM_WOODEN_PICKAXE ||
             id == ITEM_STONE_PICKAXE || id == ITEM_IRON_PICKAXE || id == ITEM_DIAMOND_PICKAXE) {
        // Tool/ingot palette by material.
        switch (id) {
            case ITEM_STONE_SWORD: case ITEM_STONE_PICKAXE: r = 120; g = 120; b = 125; break;
            case ITEM_IRON_SWORD: case ITEM_IRON_PICKAXE: case ITEM_IRON_INGOT: r = 216; g = 216; b = 222; break;
            case ITEM_DIAMOND_SWORD: case ITEM_DIAMOND_PICKAXE: r = 80; g = 230; b = 210; break;
            default: r = 170; g = 135; b = 85; break; // wooden
        }
    } else if (id == ITEM_GOLD_INGOT) { r = 250; g = 210; b = 70; }
    else if (id == ITEM_DIAMOND) { r = 90; g = 235; b = 215; }
    else if (id == BLOCK_ENCHANTING_TABLE) { r = 60; g = 40; b = 100; }
    else if (id == BLOCK_BOOKSHELF) { r = 140; g = 100; b = 60; }
    else if (id == BLOCK_LEVER_OFF || id == BLOCK_LEVER_ON) { r = 130; g = 110; b = 80; }
    else if (id == BLOCK_PRESSURE_PLATE_OFF || id == BLOCK_PRESSURE_PLATE_ON) { r = 130; g = 130; b = 130; }
    else if (id == BLOCK_REDSTONE_LAMP_OFF) { r = 100; g = 70; b = 40; }
    else if (id == BLOCK_REDSTONE_LAMP_ON) { r = 255; g = 210; b = 120; }
    else if (id == BLOCK_REPEATER_OFF || id == BLOCK_REPEATER_ON) { r = 120; g = 120; b = 120; }
    else if (id == ITEM_BOOK) { r = 150; g = 60; b = 50; }
}
const char* item_label(ItemId id) {
    if (id < BLOCK_COUNT && id > BLOCK_AIR) return BLOCK_PROPERTIES_TABLE[id].name.data();
    return ItemRegistry::get(id).name.data();
}

// Draws the procedural pixel-art icon for an item via the UI icon atlas path.
void draw_item_icon(UIRenderer& ui, const ItemIcons& icons, ItemId id, float x, float y, float size) {
    float u0, v0, u1, v1;
    icons.uv_for(id, u0, v0, u1, v1);
    ui.draw_icon_quad(x, y, size, size, u0, v0, u1, v1);
}

// Durability bar under an item icon (green -> yellow -> red as it wears).
void draw_durability_bar(UIRenderer& ui, const ItemStack& stack, float x, float y, float w) {
    const ItemProperties& props = ItemRegistry::get(stack.item);
    if (props.max_damage == 0 || stack.damage == 0) return;
    float frac = 1.0f - static_cast<float>(stack.damage) / static_cast<float>(props.max_damage);
    frac = std::clamp(frac, 0.0f, 1.0f);
    ui.draw_rect(x - 1, y - 1, w + 2, 4, 0, 0, 0, 200);
    uint8_t r = static_cast<uint8_t>(255 - frac * 175);
    uint8_t g = static_cast<uint8_t>(60 + frac * 160);
    ui.draw_rect(x, y, w * frac, 2, r, g, 40, 255);
}

// Draws a HUD stat sprite (heart/hunger/armor cell) from the icon atlas.
void draw_ui_sprite(UIRenderer& ui, const ItemIcons& icons, int cell, float x, float y, float size) {
    float u0, v0, u1, v1;
    icons.uv_for_cell(cell, u0, v0, u1, v1);
    ui.draw_icon_quad(x, y, size, size, u0, v0, u1, v1);
}

// Crafting-result arrow built from rects (shaft + triangular tip).
void draw_arrow_right(UIRenderer& ui, float x, float y, float w, float h) {
    ui.draw_rect(x, y + h * 0.25f, w * 0.6f, h * 0.5f, 180, 180, 180, 255);
    for (int i = 0; i < 3; ++i) {
        float seg_h = h * (0.75f - 0.25f * i);
        ui.draw_rect(x + w * 0.6f + i * (w * 0.133f), y + (h - seg_h) * 0.5f,
                     w * 0.14f, seg_h, 180, 180, 180, 255);
    }
}

std::string json_escape(std::string s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        if (c == '"' || c == '\\') { out += '\\'; }
        out += c;
    }
    return out;
}
} // namespace

bool Game::init() {
    mc::log::init();
    settings_ = Settings::load();

    if (!renderer_.init(1280, 720, "Minekampf")) return false;
    if (!particles_.init()) return false;
    if (!ui_.init(renderer_.width(), renderer_.height())) return false;

    // Procedural inventory icons: built from the generated block atlas, then
    // bound to the UI renderer once (texture id is stable for the session).
    item_icons_.generate(renderer_.atlas());
    item_icons_.upload();
    ui_.set_icon_texture(item_icons_.gl_texture());

    audio_ = std::make_unique<AudioEngine>();
    if (!audio_->init()) {
        MC_LOG_WARN("Audio device unavailable — running without sound output.");
    }
    apply_settings();

    GLFWwindow* w = renderer_.window();
    glfwSetWindowUserPointer(w, this);
    glfwSetKeyCallback(w, key_callback);
    glfwSetCursorPosCallback(w, cursor_callback);
    glfwSetMouseButtonCallback(w, mouse_callback);
    glfwSetScrollCallback(w, scroll_callback);
    glfwSetCharCallback(w, char_callback);
    glfwSetFramebufferSizeCallback(w, resize_callback);
    // Cursor visible in menu state.
    glfwSetInputMode(w, GLFW_CURSOR, GLFW_CURSOR_NORMAL);

    camera_.aspect = std::max(static_cast<float>(renderer_.width()), 1.0f) / std::max(static_cast<float>(renderer_.height()), 1.0f);
    camera_.fov = 70.0f;
    camera_.far_plane = static_cast<float>(render_distance_) * CHUNK_SIZE + 64.0f;

    state_ = GameState::MainMenu;
    running_ = true;
    prepare_menu_world();
    MC_LOG_INFO("Minekampf initialized. Start in MainMenu.");
    return true;
}

bool Game::start_game(const WorldMeta& meta) {
    current_world_meta_ = meta;
    MC_LOG_INFO("start_game: '{}' seed={} mode={} size={}", meta.name, meta.seed,
                WorldManager::game_mode_str(meta.game_mode), WorldManager::world_size_str(meta.world_size));

    // Discard the menu panorama world and any in-flight generation for it.
    menu_world_active_ = false;
    renderer_.clear_meshes();
    pool_.wait();
    GenResult stale;
    while (gen_channel_.try_pop(stale)) {}
    {
        std::pair<ChunkPos, ChunkMeshData> sm;
        while (mesh_channel_.try_pop(sm)) { recycled_meshes_.push(std::move(sm.second)); }
    }
    gen_in_flight_.clear();
    mesh_in_flight_.clear();
    worlds_.clear();
    generators_.clear();

    // Set world bounds based on WorldSize.
    // Override WORLD_CHUNK_HALF globally — use a runtime override instead.
    // For simplicity, set render_distance_ based on size.

    // Create storage and world instances. Bot/screenshot runs use an
    // in-memory world: deterministic, no disk writes, no save pollution.
    if (in_memory_world_) {
        storage_.reset();
    } else {
        std::string world_path = WorldManager::world_dir(meta.name);
        storage_ = std::make_unique<LevelStorage>(world_path);
    }

    worlds_[DimensionId::Overworld] = std::make_unique<World>(meta.seed, DimensionId::Overworld);
    worlds_[DimensionId::Nether] = std::make_unique<World>(meta.seed, DimensionId::Nether);
    worlds_[DimensionId::End] = std::make_unique<World>(meta.seed, DimensionId::End);

    generators_[DimensionId::Overworld] = std::make_unique<WorldGenerator>(meta.seed);
    generators_[DimensionId::Nether] = std::make_unique<WorldGenerator>(meta.seed);
    generators_[DimensionId::End] = std::make_unique<WorldGenerator>(meta.seed);

    // Try to load existing world; if no chunks saved, generate fresh.
    bool loaded = false;
    if (storage_) {
        storage_->load_level_dat(*worlds_[DimensionId::Overworld]);
        loaded = storage_->load_player_dat(player_);
        if (loaded) {
            time_of_day_ = storage_->load_time_of_day();
        }
    }
    is_new_world_ = !loaded;

    mobs_.clear();
    furnaces_.clear();
    projectiles_.clear();
    furnace_open_ = false;
    if (storage_) {
        for (const auto& fs : storage_->load_furnaces()) {
            FurnaceKey key{fs.dim, fs.x, fs.y, fs.z};
            auto& f = furnaces_[key];
            f.input = ItemStack(static_cast<ItemId>(fs.in_item), static_cast<uint8_t>(fs.in_count));
            f.fuel = ItemStack(static_cast<ItemId>(fs.fuel_item), static_cast<uint8_t>(fs.fuel_count));
            f.output = ItemStack(static_cast<ItemId>(fs.out_item), static_cast<uint8_t>(fs.out_count));
            f.burn_left = fs.burn_left;
            f.burn_total = fs.burn_total;
            f.cook_progress = fs.cook;
            f.pending_xp = fs.pending_xp;
        }
    }
    ChunkPos spawn_cp{0, 0};
    if (loaded) {
        int px = static_cast<int>(std::floor(player_.pos.x));
        int pz = static_cast<int>(std::floor(player_.pos.z));
        spawn_cp.x = px < 0 ? (px - (CHUNK_SIZE - 1)) / CHUNK_SIZE : px / CHUNK_SIZE;
        spawn_cp.z = pz < 0 ? (pz - (CHUNK_SIZE - 1)) / CHUNK_SIZE : pz / CHUNK_SIZE;
    }

    auto spawn_chunk = mc::World::chunk_pool.acquire();
    spawn_chunk->reset(spawn_cp);
    if (!storage_ || !storage_->load_chunk(*spawn_chunk, DimensionId::Overworld)) {
        generators_[DimensionId::Overworld]->generate(*spawn_chunk);
    }
    if (Chunk* c = spawn_chunk.get()) {
        compute_light(*c, *worlds_[DimensionId::Overworld]);
    }
    worlds_[DimensionId::Overworld]->insert_chunk(std::move(spawn_chunk));

    Chunk* center = worlds_[DimensionId::Overworld]->get_chunk(spawn_cp);
    
    int best_lx = 8, best_lz = 8;
    int best_h = MIN_Y;
    if (center) {
        // Scan 8x8 center area for the best surface Y above sea level
        for (int lz = 4; lz < 12; ++lz) {
            for (int lx = 4; lx < 12; ++lx) {
                int h = center->heightmap[lz * CHUNK_SIZE + lx];
                if (h > best_h && h > SEA_LEVEL) {
                    best_h = h;
                    best_lx = lx;
                    best_lz = lz;
                }
            }
        }
        // Fallback: any column at all above MIN_Y
        if (best_h <= MIN_Y) {
            best_lx = 8;
            best_lz = 8;
            best_h = center->heightmap[best_lz * CHUNK_SIZE + best_lx];
            if (best_h <= MIN_Y) best_h = SEA_LEVEL + 1;
        }
    }
    int actual_h = (best_h > MIN_Y) ? best_h : SEA_LEVEL;

    // Ensure there are 2 air blocks above the spawn point using world coordinates
    if (center) {
        int wx = best_lx + spawn_cp.x * CHUNK_SIZE;
        int wz = best_lz + spawn_cp.z * CHUNK_SIZE;
        int spawn_y = actual_h;
        for (int y = actual_h; y < MAX_Y - 2; ++y) {
            BlockId b1 = worlds_[DimensionId::Overworld]->get_block({wx, y, wz});
            BlockId b2 = worlds_[DimensionId::Overworld]->get_block({wx, y + 1, wz});
            if (b1 == BLOCK_AIR && b2 == BLOCK_AIR) {
                spawn_y = y;
                break;
            }
        }
        if (spawn_y > actual_h + 10) spawn_y = actual_h; // safety clamp
        actual_h = spawn_y;
    }

    bool suffocating = false;
    if (loaded && center) {
        int local_x = static_cast<int>(std::floor(player_.pos.x)) - spawn_cp.x * CHUNK_SIZE;
        int local_z = static_cast<int>(std::floor(player_.pos.z)) - spawn_cp.z * CHUNK_SIZE;
        int py = static_cast<int>(std::floor(player_.pos.y));
        if (py >= MIN_Y && py < MAX_Y - 1) {
            if (is_opaque(center->get_block(local_x, py, local_z)) || 
                is_opaque(center->get_block(local_x, py + 1, local_z))) {
                suffocating = true;
            }
        }
    }

    if (!loaded || player_.pos.y <= MIN_Y || suffocating) {
        player_.pos.x = static_cast<float>(best_lx) + 0.5f;
        player_.pos.z = static_cast<float>(best_lz) + 0.5f;
        player_.pos.y = static_cast<float>(actual_h) + 0.1f;
        player_.prev_pos = player_.pos;
    }
    MC_LOG_INFO("start_game: loaded={} new_world={} player=({:.1f}, {:.1f}, {:.1f})",
                loaded, is_new_world_, player_.pos.x, player_.pos.y, player_.pos.z);
    player_.mode = meta.game_mode;
    player_.flying = false;
    if (meta.game_mode == GameMode::Creative) {
        // Starter building palette for creative worlds only.
        player_.inventory.set_slot(5, ItemStack(BLOCK_GLASS, 64));
        player_.inventory.set_slot(6, ItemStack(BLOCK_OBSIDIAN, 64));
        player_.inventory.set_slot(7, ItemStack(BLOCK_NETHERRACK, 64));
        player_.inventory.set_slot(8, ItemStack(BLOCK_TORCH, 64));
    }

    // Submit remaining chunks for async generation. This also sets
    // loading_total_chunks_ to the exact number of submitted chunks.
    loading_done_chunks_ = 0;
    generate_initial_chunks();

    state_ = GameState::Loading;
    glfwSetInputMode(renderer_.window(), GLFW_CURSOR, GLFW_CURSOR_NORMAL);
    MC_LOG_INFO("Starting game: {} seed={} mode={}", meta.name, meta.seed, WorldManager::game_mode_str(meta.game_mode));
    return true;
}

void Game::shutdown_world() {
    pool_.wait();
    if (storage_) {
        storage_->save_player_dat(player_);
        for (auto& [id, w] : worlds_) {
            storage_->save_all_dirty(*w);
        }
        std::vector<FurnaceSave> fsave;
        fsave.reserve(furnaces_.size());
        for (const auto& [key, f] : furnaces_) {
            FurnaceSave fs;
            fs.dim = key.dim;
            fs.x = key.x; fs.y = key.y; fs.z = key.z;
            fs.in_item = f.input.item; fs.in_count = f.input.count;
            fs.fuel_item = f.fuel.item; fs.fuel_count = f.fuel.count;
            fs.out_item = f.output.item; fs.out_count = f.output.count;
            fs.burn_left = f.burn_left; fs.burn_total = f.burn_total;
            fs.cook = f.cook_progress;
            fs.pending_xp = f.pending_xp;
            fsave.push_back(fs);
        }
        storage_->save_furnaces(fsave);
    }
    worlds_.clear();
    generators_.clear();
    storage_.reset();
    mobs_.clear();
    current_tick_ = 0;
}

void Game::return_to_menu() {
    shutdown_world();
    state_ = GameState::MainMenu;
    glfwSetInputMode(renderer_.window(), GLFW_CURSOR, GLFW_CURSOR_NORMAL);
    prepare_menu_world();
}

void Game::prepare_menu_world() {
    if (menu_world_active_) return;
    renderer_.clear_meshes();
    worlds_.clear();
    generators_.clear();
    current_dimension_ = DimensionId::Overworld;

    constexpr uint64_t kMenuSeed = 12345;
    worlds_[DimensionId::Overworld] = std::make_unique<World>(kMenuSeed, DimensionId::Overworld);
    generators_[DimensionId::Overworld] = std::make_unique<WorldGenerator>(kMenuSeed);

    // Submit a disc of chunks around the origin for the panorama backdrop.
    constexpr int rd = 4;
    for (int dz = -rd; dz <= rd; ++dz) {
        for (int dx = -rd; dx <= rd; ++dx) {
            if (dx * dx + dz * dz > rd * rd) continue;
            ChunkPos cp{dx, dz};
            pool_.submit([this, cp] {
                auto chunk = mc::World::chunk_pool.acquire();
                chunk->reset(cp);
                generators_[DimensionId::Overworld]->generate(*chunk);
                compute_light(*chunk, *worlds_[DimensionId::Overworld], /*cross_chunk_writes=*/false);
                gen_channel_.push(GenResult{DimensionId::Overworld, cp, std::move(chunk)});
            });
        }
    }
    menu_world_active_ = true;
    MC_LOG_INFO("Menu panorama world submitted (seed {}).", kMenuSeed);
}

void Game::drain_menu_chunks() {
    if (!menu_world_active_) return;
    drain_gen_results();
    build_dirty_meshes(2);
}

void Game::generate_initial_chunks() {
    int px = static_cast<int>(std::floor(player_.pos.x));
    int pz = static_cast<int>(std::floor(player_.pos.z));
    int cx = px < 0 ? (px - (CHUNK_SIZE - 1)) / CHUNK_SIZE : px / CHUNK_SIZE;
    int cz = pz < 0 ? (pz - (CHUNK_SIZE - 1)) / CHUNK_SIZE : pz / CHUNK_SIZE;

    int rd = render_distance_;
    loading_total_chunks_ = 0;
    for (int dz = -rd; dz <= rd; ++dz) {
        for (int dx = -rd; dx <= rd; ++dx) {
            if (dx * dx + dz * dz > rd * rd) continue;
            ChunkPos cp{cx + dx, cz + dz};
            loading_total_chunks_++;
            pool_.submit([this, cp] {
                auto chunk = mc::World::chunk_pool.acquire();
                chunk->reset(cp);
                if (!storage_ || !storage_->load_chunk(*chunk, DimensionId::Overworld)) {
                    generators_[DimensionId::Overworld]->generate(*chunk);
                }
                compute_light(*chunk, *worlds_[DimensionId::Overworld], /*cross_chunk_writes=*/false);
                gen_channel_.push(GenResult{DimensionId::Overworld, cp, std::move(chunk)});
            });
        }
    }
    MC_LOG_INFO("Initial chunk generation submitted: {} chunks around ({}, {}), render_distance={}",
                loading_total_chunks_, cx, cz, rd);
}

void Game::shutdown() {
    shutdown_world();
    pool_.shutdown();
    automation_.stop();
    if (audio_) {
        audio_->shutdown();
        audio_.reset();
    }
    ui_.shutdown();
    particles_.shutdown();
    renderer_.shutdown();
    mc::log::shutdown();
}

void Game::apply_settings() {
    render_distance_ = settings_.render_distance;
    renderer_.set_shadows_enabled(settings_.shadows);
    renderer_.set_vsync(settings_.vsync);
    renderer_.set_fullscreen(settings_.fullscreen);
    renderer_.set_quality(static_cast<Renderer::QualityPreset>(settings_.quality));
    if (audio_) {
        audio_->set_category_volume(SoundCategory::MASTER, settings_.volume_master);
        audio_->set_category_volume(SoundCategory::BLOCKS, settings_.volume_blocks);
        audio_->set_category_volume(SoundCategory::AMBIENT, settings_.volume_ambient);
        audio_->set_category_volume(SoundCategory::WEATHER, settings_.volume_weather);
    }
}

void Game::finish_frame() {
    if (capture_pending_) {
        take_screenshot(capture_path_);
        capture_path_.clear();
        capture_pending_ = false;
    }
    renderer_.present();
}

void Game::run() {
    while (running_ && !renderer_.should_close()) {
        switch (state_) {
            case GameState::MainMenu:    draw_main_menu();    break;
            case GameState::WorldSelect: draw_world_select(); break;
            case GameState::CreateWorld: draw_create_world(); break;
            case GameState::Loading:     run_loading();       break;
            case GameState::Playing:     run_game();          break;
            case GameState::Paused:      run_paused();        break;
            case GameState::Inventory:   run_game();          break;
        }
    }
    running_ = false;
}

// =================================================================
// MENU SCREENS
// =================================================================

namespace {
void clear_screen(uint8_t r, uint8_t g, uint8_t b) {
    glClearColor(r / 255.0f, g / 255.0f, b / 255.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void clear_screen_alpha(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    // Fallback: just clear to the alpha-blended color (no actual alpha overlay).
    float bf = a / 255.0f;
    float cr = r / 255.0f;
    float cg = g / 255.0f;
    float cb = b / 255.0f;
    // Mix toward target color based on alpha
    float alpha = bf;
    float mixed_r = cr * alpha + 0.0f * (1.0f - alpha);
    float mixed_g = cg * alpha + 0.0f * (1.0f - alpha);
    float mixed_b = cb * alpha + 0.0f * (1.0f - alpha);
    glClearColor(mixed_r, mixed_g, mixed_b, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void poll_ui_mouse(Game& game, UIRenderer& ui, GLFWwindow* wnd) {
    (void)game;
    double mx, my;
    int state = glfwGetMouseButton(wnd, GLFW_MOUSE_BUTTON_LEFT);
    static thread_local bool prev_click = false;

    glfwGetCursorPos(wnd, &mx, &my);
    bool cur_click = (state == GLFW_PRESS);
    bool just_clicked = cur_click && !prev_click;
    prev_click = cur_click;

    ui.set_mouse(static_cast<float>(mx), static_cast<float>(my), just_clicked, cur_click);
}

// --- Settings widgets -------------------------------------------------------
float settings_slider(UIRenderer& ui, const char* label, float x, float y, float w,
                      float v, float lo, float hi, const char* fmt) {
    ui.draw_text(label, x, y, 1.2f, 215, 225, 240);
    char buf[48];
    snprintf(buf, sizeof(buf), fmt, v);
    ui.draw_text(buf, x + w - ui.text_width(buf, 1.0f), y, 1.1f, 255, 215, 120);
    float ty = y + 22, th = 10;
    ui.draw_rect(x, ty, w, th, 35, 42, 58, 230);
    float t = std::clamp((v - lo) / (hi - lo), 0.0f, 1.0f);
    ui.draw_rect(x, ty, w * t, th, 95, 155, 225, 255);
    ui.draw_rect(x + w * t - 5.0f, ty - 5.0f, 10.0f, th + 10.0f, 235, 240, 250, 255);
    if (ui.mouse_inside(x - 4.0f, ty - 8.0f, w + 8.0f, th + 16.0f) && ui.mouse_down()) {
        t = std::clamp((ui.mouse_x() - x) / w, 0.0f, 1.0f);
        v = lo + t * (hi - lo);
    }
    return v;
}

bool settings_toggle(UIRenderer& ui, const char* label, float x, float y, float w, bool value) {
    ui.draw_text(label, x, y + 8, 1.2f, 215, 225, 240);
    float bw = 120;
    if (ui.button(value ? "Włączone" : "Wyłączone", x + w - bw, y, bw, 28, 1.1f)) {
        return !value;
    }
    return value;
}
}

void Game::draw_menu_background() {
    drain_menu_chunks();
    Camera menu_camera;
    // Slow orbit over the panorama world for a living main-menu backdrop.
    float t = static_cast<float>(glfwGetTime()) * 0.05f;
    glm::vec3 eye(8.0f + std::sin(t) * 8.0f, 84.0f, 8.0f + std::cos(t) * 8.0f);
    menu_camera.position = eye;
    // Always aim at the middle of the panorama disc so the terrain stays in frame.
    glm::vec3 target(8.0f, 72.0f, 8.0f);
    glm::vec3 dir = glm::normalize(target - eye);
    menu_camera.yaw = std::atan2(-dir.x, dir.z);
    menu_camera.pitch = std::asin(-dir.y);
    menu_camera.far_plane = 400.0f;
    menu_camera.aspect = std::max(static_cast<float>(renderer_.width()), 1.0f) / std::max(static_cast<float>(renderer_.height()), 1.0f);

    float sky_brightness = 1.0f;
    renderer_.set_sky_brightness(sky_brightness);
    renderer_.set_sun_angle(0.25f * 6.2831853f);
    renderer_.set_fog_mode(0, 0.0f);
    renderer_.set_underwater(false);

    renderer_.begin_frame(menu_camera);
    renderer_.set_shadow_casters(nullptr, 0.0f);
    renderer_.render_opaque(menu_camera);
    renderer_.render_transparent(menu_camera);
    renderer_.end_frame();
}

void Game::draw_main_menu() {
    GLFWwindow* w = renderer_.window();
    renderer_.poll_events();
    automation_.update(*this);
    if (renderer_.should_close()) { running_ = false; return; }
    // ESC in the main menu is intentionally a no-op — quitting the whole game
    // on a stray Escape felt hostile (users expect menus, not an exit).
    poll_ui_mouse(*this, ui_, w);

    draw_menu_background();
    ui_.begin_frame();
    int sw = ui_.screen_width();
    int sh = ui_.screen_height();
    float cx = sw * 0.5f;

    // Title (with soft drop shadow so it reads on any sky)
    ui_.draw_text_centered("MINEKAMPF", cx + 3.0f, sh * 0.14f + 3.0f, 5.0f, 20, 30, 50, 160);
    ui_.draw_text_centered("MINEKAMPF", cx, sh * 0.14f, 5.0f, 235, 245, 255);
    ui_.draw_text_centered("Odkrywaj  ·  Buduj  ·  Przetrwaj", cx + 2.0f, sh * 0.14f + 54.0f, 1.5f, 20, 30, 50, 150);
    ui_.draw_text_centered("Odkrywaj  ·  Buduj  ·  Przetrwaj", cx, sh * 0.14f + 52, 1.5f, 200, 215, 240);

    if (settings_open_) {
        draw_settings_panel();
    } else {
        // Buttons
        float btn_w = 320, btn_h = 44, btn_y = sh * 0.36f, spacing = 56;
        if (ui_.button("Singleplayer", cx - btn_w * 0.5f, btn_y, btn_w, btn_h, 2.0f)) {
            state_ = GameState::WorldSelect;
            world_list_ = WorldManager::list_worlds();
            world_sel_index_ = world_list_.empty() ? -1 : 0;
            return;
        }
        if (ui_.button("Opcje", cx - btn_w * 0.5f, btn_y + spacing, btn_w, btn_h, 2.0f)) {
            settings_open_ = true;
            settings_tab_ = 0;
            return;
        }
        if (ui_.button("Wyjdź", cx - btn_w * 0.5f, btn_y + spacing * 2, btn_w, btn_h, 2.0f)) {
            running_ = false;
        }
    }

    ui_.draw_text(std::string("Minekampf ") + MINEKAMPF_VERSION, 10.0f,
                  static_cast<float>(sh) - 24.0f, 1.0f, 150, 160, 180);
    ui_.end_frame();
    finish_frame();
}

void Game::draw_world_select() {
    GLFWwindow* w = renderer_.window();
    renderer_.poll_events();
    automation_.update(*this);
    if (renderer_.should_close()) { running_ = false; return; }
    if (glfwGetKey(w, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
        static double esc_time = 0;
        double now = glfwGetTime();
        if (now - esc_time > 0.3) { state_ = GameState::MainMenu; return; }
        esc_time = now;
    }

    poll_ui_mouse(*this, ui_, w);
    draw_menu_background();
    ui_.begin_frame();

    int sw = ui_.screen_width();
    int sh = ui_.screen_height();
    float cx = sw * 0.5f;

    ui_.draw_text_centered("Wybierz Świat", cx, 30, 3.0f, 200, 220, 255);

    // Refresh world list
    static double last_refresh = 0;
    if (glfwGetTime() - last_refresh > 0.5) {
        MC_LOG_TRACE("Refreshing world list...");
        world_list_ = WorldManager::list_worlds();
        MC_LOG_TRACE("World list refreshed, size: {}", world_list_.size());
        last_refresh = glfwGetTime();
    }

    // World list (scrollable with mouse wheel — simple page)
    float list_x = cx - 280, list_y = 90, list_w = 560, item_h = 64;
    int max_visible = 6;

    ui_.draw_glass_panel(list_x, list_y, list_w, static_cast<float>(max_visible) * item_h);

    for (int i = 0; i < static_cast<int>(world_list_.size()) && i < max_visible; ++i) {
        auto& m = world_list_[i];
        float item_y = list_y + i * item_h;
        bool hover = ui_.button("", list_x + 8, item_y + 4, list_w - 16, item_h - 8, 1.0f);
        // draw bg
        if (i == world_sel_index_) {
            ui_.draw_rect(list_x + 8, item_y + 4, list_w - 16, item_h - 8, 60, 80, 120, 200);
        }
        ui_.draw_text(m.display_name, list_x + 16, item_y + 10, 2.0f, 255, 255, 255);
        ui_.draw_text(WorldManager::game_mode_str(m.game_mode) + " · " + WorldManager::world_size_str(m.world_size),
                      list_x + 16, item_y + 34, 1.0f, 160, 180, 200);
        if (hover) {
            world_sel_index_ = i;
            if (glfwGetMouseButton(w, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS) {
                static double last_click = 0;
                if (glfwGetTime() - last_click > 0.3) {
                    last_click = glfwGetTime();
                    start_game(world_list_[i]);
                    return;
                }
            }
        }
    }
    if (world_list_.empty()) {
        ui_.draw_text_centered("Brak zapisanych światów. Kliknij \"Nowy Świat\" aby utworzyć.", cx, list_y + 100, 1.5f, 150, 150, 150);
    }

    // Delete modal
    if (world_delete_index_ >= 0 && world_delete_index_ < static_cast<int>(world_list_.size())) {
        float mo_w = 400, mo_h = 140;
        float mo_x = cx - mo_w * 0.5f, mo_y = sh * 0.3f;
        ui_.draw_glass_panel(mo_x, mo_y, mo_w, mo_h);
        ui_.draw_text_centered("Usunąć świat?", cx, mo_y + 16, 2.0f, 255, 100, 100);
        ui_.draw_text_centered("\"" + world_list_[world_delete_index_].display_name + "\"", cx, mo_y + 50, 1.5f, 200, 200, 200);
        if (ui_.button("Tak", mo_x + 40, mo_y + mo_h - 48, 140, 36, 1.5f)) {
            WorldManager::delete_world(world_list_[world_delete_index_].name);
            world_delete_index_ = -1;
            world_list_ = WorldManager::list_worlds();
        }
        if (ui_.button("Nie", mo_x + mo_w - 180, mo_y + mo_h - 48, 140, 36, 1.5f)) {
            world_delete_index_ = -1;
        }
    } else {
        // Bottom buttons
        bool any_selected = world_sel_index_ >= 0 && world_sel_index_ < static_cast<int>(world_list_.size());
        if (ui_.button("Nowy Świat", cx - 380, static_cast<float>(sh - 64), 160, 44, 1.5f)) {
            state_ = GameState::CreateWorld;
            new_world_name_.clear();
            new_world_seed_.clear();
            new_world_mode_ = GameMode::Survival;
            new_world_size_ = WorldSize::Small;
            name_input_active_ = false;
            seed_input_active_ = false;
        }
        if (ui_.button("Graj", cx - 80, static_cast<float>(sh - 64), 160, 44, 1.5f)) {
            if (any_selected) {
                start_game(world_list_[world_sel_index_]);
                return;
            }
        }
        if (ui_.button("Usuń", cx + 220, static_cast<float>(sh - 64), 160, 44, 1.5f)) {
            if (any_selected) {
                world_delete_index_ = world_sel_index_;
            }
        }
        if (ui_.button("Wstecz", cx - 30, static_cast<float>(sh - 64 - 50), 160, 36, 1.2f)) {
            state_ = GameState::MainMenu;
        }
    }

    ui_.end_frame();
    finish_frame();
}

void Game::draw_create_world() {
    GLFWwindow* w = renderer_.window();
    renderer_.poll_events();
    automation_.update(*this);
    if (renderer_.should_close()) { running_ = false; return; }
    if (glfwGetKey(w, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
        static double esc_time = 0;
        double now = glfwGetTime();
        if (now - esc_time > 0.3) { state_ = GameState::WorldSelect; return; }
        esc_time = now;
    }
    poll_ui_mouse(*this, ui_, w);
    draw_menu_background();
    ui_.begin_frame();

    int sw = ui_.screen_width();
    int sh = ui_.screen_height();
    float cx = sw * 0.5f;

    ui_.draw_text_centered("Nowy Świat", cx, 40, 3.0f, 200, 220, 255);

    float form_x = cx - 280, form_w = 560;
    float y = 110;

    // Name
    ui_.draw_text("Nazwa świata:", form_x, y, 1.5f, 200, 200, 200);
    y += 24;
    auto r1 = ui_.text_input(new_world_name_, form_x, y, form_w, 36, name_input_active_, 1.5f);
    y += 56;

    // Seed
    ui_.draw_text("Seed (pusty = losowy):", form_x, y, 1.5f, 200, 200, 200);
    y += 24;
    auto r2 = ui_.text_input(new_world_seed_, form_x, y, form_w, 36, seed_input_active_, 1.5f);
    y += 56;

    // Game mode
    ui_.draw_text("Tryb gry:", form_x, y, 1.5f, 200, 200, 200);
    y += 24;
    float btn_w = 160, btn_h = 36, gap = 8;
    const char* modes[] = {"Survival", "Creative", "Hardcore"};
    GameMode mode_vals[] = {GameMode::Survival, GameMode::Creative, GameMode::Hardcore};
    for (int i = 0; i < 3; ++i) {
        bool sel = (new_world_mode_ == mode_vals[i]);
        if (sel) ui_.draw_rect(form_x + i * (btn_w + gap), y, btn_w, btn_h, 80, 100, 140, 220);
        else ui_.draw_rect(form_x + i * (btn_w + gap), y, btn_w, btn_h, 50, 60, 80, 180);
        ui_.draw_text_centered(modes[i], form_x + i * (btn_w + gap) + btn_w * 0.5f, y + 10, 1.3f,
                                sel ? 255 : 200, sel ? 255 : 200, sel ? 255 : 200);
        // Click detection
        if (ui_.button("", form_x + i * (btn_w + gap), y, btn_w, btn_h, 1.0f)) {
            new_world_mode_ = mode_vals[i];
        }
    }
    y += 56;

    // World size
    ui_.draw_text("Rozmiar świata:", form_x, y, 1.5f, 200, 200, 200);
    y += 24;
    const char* sizes[] = {"16x16", "32x32"};
    WorldSize size_vals[] = {WorldSize::Small, WorldSize::Medium};
    for (int i = 0; i < 2; ++i) {
        bool sel = (new_world_size_ == size_vals[i]);
        if (sel) ui_.draw_rect(form_x + i * (btn_w + gap), y, btn_w, btn_h, 80, 100, 140, 220);
        else ui_.draw_rect(form_x + i * (btn_w + gap), y, btn_w, btn_h, 50, 60, 80, 180);
        ui_.draw_text_centered(sizes[i], form_x + i * (btn_w + gap) + btn_w * 0.5f, y + 10, 1.3f,
                                sel ? 255 : 200, sel ? 255 : 200, sel ? 255 : 200);
        if (ui_.button("", form_x + i * (btn_w + gap), y, btn_w, btn_h, 1.0f)) {
            new_world_size_ = size_vals[i];
        }
    }
    y += 60;

    // Create / Cancel
    if (ui_.button("Utwórz", cx - 200, static_cast<float>(sh - 64), 160, 44, 1.5f)) {
        if (new_world_name_.empty()) { name_input_active_ = true; }
        else {
            WorldMeta meta;
            meta.name = WorldManager::sanitize_name(new_world_name_);
            meta.display_name = new_world_name_;
            meta.seed = WorldManager::parse_seed(new_world_seed_);
            meta.game_mode = new_world_mode_;
            meta.world_size = new_world_size_;
            meta.created_at = static_cast<int64_t>(std::time(nullptr));
            meta.last_played = meta.created_at;
            if (WorldManager::create_world(meta)) {
                start_game(meta);
                return;
            }
        }
    }
    if (ui_.button("Anuluj", cx + 40, static_cast<float>(sh - 64), 160, 44, 1.5f)) {
        state_ = GameState::WorldSelect;
    }

    ui_.end_frame();
    finish_frame();
}

// =================================================================
// SETTINGS PANEL
// =================================================================

void Game::draw_settings_panel() {
    int sw = ui_.screen_width();
    int sh = ui_.screen_height();

    float pw = 580.0f, ph = 490.0f;
    float px = sw * 0.5f - pw * 0.5f;
    float py = sh * 0.5f - ph * 0.5f;

    ui_.draw_rect(0, 0, static_cast<float>(sw), static_cast<float>(sh), 0, 0, 0, 150);
    ui_.draw_glass_panel(px, py, pw, ph);
    ui_.draw_text_centered("Opcje", sw * 0.5f, py + 16, 2.5f, 230, 240, 255);

    // Tabs
    const char* tabs[] = {"Grafika", "Gra", "Dźwięki"};
    float tw = 160, gap = 14;
    float tx = px + (pw - (tw * 3 + gap * 2)) * 0.5f;
    float ty = py + 56;
    for (int i = 0; i < 3; ++i) {
        if (ui_.button(tabs[i], tx + i * (tw + gap), ty, tw, 34, 1.3f)) {
            settings_tab_ = i;
        }
        if (settings_tab_ == i) {
            ui_.draw_rect(tx + i * (tw + gap), ty + 30, tw, 4, 120, 175, 255, 255);
        }
    }

    float cx = px + 34;
    float cw = pw - 68;
    float cy = py + 112;

    if (settings_tab_ == 0) { // Grafika
        settings_.render_distance = static_cast<int>(settings_slider(
            ui_, "Czas renderowania", cx, cy, cw,
            static_cast<float>(settings_.render_distance), 2.0f, 10.0f, "%.0f chunków"));
        cy += 58;
        settings_.fov = settings_slider(ui_, "Pole widzenia (FOV)", cx, cy, cw, settings_.fov, 60.0f, 110.0f, "%.0f°");
        cy += 58;
        // Quality preset cycle: LOW -> MEDIUM -> HIGH -> LOW
        {
            const char* names[] = {"NISKA", "ŚREDNIA", "WYSOKA"};
            std::string label = std::string("Jakość grafiki: ") + names[settings_.quality];
            if (ui_.button(label.c_str(), cx, cy, cw, 34, 1.3f)) {
                settings_.quality = (settings_.quality + 1) % 3;
                apply_settings();
            }
            cy += 44;
        }
        settings_.shadows = settings_toggle(ui_, "Cienie dynamiczne", cx, cy, cw, settings_.shadows); cy += 38;
        settings_.particles = settings_toggle(ui_, "Cząsteczki", cx, cy, cw, settings_.particles); cy += 38;
        settings_.view_bobbing = settings_toggle(ui_, "Bujanie kamery", cx, cy, cw, settings_.view_bobbing); cy += 38;
        settings_.vsync = settings_toggle(ui_, "VSync", cx, cy, cw, settings_.vsync); cy += 38;
        settings_.fullscreen = settings_toggle(ui_, "Pełny ekran", cx, cy, cw, settings_.fullscreen);
    } else if (settings_tab_ == 1) { // Gra
        settings_.mouse_sensitivity = settings_slider(
            ui_, "Czułość myszy", cx, cy, cw, settings_.mouse_sensitivity, 0.2f, 3.0f, "x%.2f");
        cy += 66;
        ui_.draw_text("Sterowanie:", cx, cy, 1.3f, 160, 180, 210); cy += 24;
        ui_.draw_text("WASD — ruch   SPACE — skok   SHIFT — skradanie", cx, cy, 1.1f, 170, 185, 205); cy += 20;
        ui_.draw_text("CTRL — sprint   F — latanie   E — ekwipunek", cx, cy, 1.1f, 170, 185, 205); cy += 20;
        ui_.draw_text("T — czat   F3 — debug   F2 — screenshot   ESC — menu", cx, cy, 1.1f, 170, 185, 205); cy += 20;
        ui_.draw_text("Kółko / 1-9 — wybór slotu   LPM — kopanie   PPM — stawianie", cx, cy, 1.1f, 170, 185, 205);
    } else { // Dźwięki
        settings_.volume_master = settings_slider(ui_, "Głośność główna", cx, cy, cw, settings_.volume_master, 0.0f, 1.0f, "%.0f%%");
        cy += 58;
        settings_.volume_blocks = settings_slider(ui_, "Bloki i kopanie", cx, cy, cw, settings_.volume_blocks, 0.0f, 1.0f, "%.0f%%");
        cy += 58;
        settings_.volume_ambient = settings_slider(ui_, "Otoczenie", cx, cy, cw, settings_.volume_ambient, 0.0f, 1.0f, "%.0f%%");
        cy += 58;
        settings_.volume_weather = settings_slider(ui_, "Pogoda", cx, cy, cw, settings_.volume_weather, 0.0f, 1.0f, "%.0f%%");
    }

    if (ui_.button("Gotowe", sw * 0.5f - 90.0f, py + ph - 54, 180, 40, 1.6f)) {
        settings_.save();
        apply_settings();
        settings_open_ = false;
    }
}

// =================================================================
// LOADING SCREEN
// =================================================================

void Game::run_loading() {
    // Tight loop: drain chunks as fast as possible.
    // The thread pool generates chunks while we light them on main thread.
    FrameTimer ft;
    auto last_render = std::chrono::steady_clock::now();
    auto last_progress_log = std::chrono::steady_clock::now();
    MC_LOG_INFO("Loading: waiting for {} chunks...", loading_total_chunks_);

    while (running_ && !renderer_.should_close() && state_ == GameState::Loading) {
        renderer_.poll_events();
        automation_.update(*this);
        drain_gen_results();

        // Update loading count.
        if (worlds_.count(DimensionId::Overworld)) {
            loading_done_chunks_ = static_cast<int>(worlds_[DimensionId::Overworld]->loaded_count());
        }

        auto now_progress = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::milliseconds>(now_progress - last_progress_log).count() >= 1000) {
            last_progress_log = now_progress;
            MC_LOG_INFO("Loading progress: {}/{} chunks (gen in flight: {}, mesh in flight: {})",
                        loading_done_chunks_, loading_total_chunks_, gen_in_flight_.size(), mesh_in_flight_.size());
        }

        // Build dirty meshes fast (no tick budget limit).
        build_dirty_meshes(32);

        // Throttle UI render to ~30 FPS.
        auto now = std::chrono::steady_clock::now();
        auto ms_elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_render).count();
        if (ms_elapsed >= 33) {
            last_render = now;
            poll_ui_mouse(*this, ui_, renderer_.window());
            clear_screen(25, 30, 45);
            ui_.begin_frame();
            int sw = ui_.screen_width();
            int sh = ui_.screen_height();
            float cx = sw * 0.5f;

            ui_.draw_text_centered("Wczytywanie świata...", cx, sh * 0.35f, 3.0f, 200, 220, 255);
            ui_.draw_text_centered(current_world_meta_.display_name, cx, sh * 0.35f + 50, 2.0f, 160, 180, 200);

            // Progress bar
            float bar_w = 500, bar_h = 24, bar_x = cx - bar_w * 0.5f, bar_y = sh * 0.55f;
            ui_.draw_rect(bar_x, bar_y, bar_w, bar_h, 40, 50, 70, 255);
            float progress = loading_total_chunks_ > 0
                ? std::min(1.0f, static_cast<float>(loading_done_chunks_) / loading_total_chunks_) : 0.0f;
            ui_.draw_rect(bar_x, bar_y, bar_w * progress, bar_h, 80, 140, 220, 255);
            std::string pct = std::to_string(loading_done_chunks_) + " / " + std::to_string(loading_total_chunks_);
            ui_.draw_text_centered(pct, cx, bar_y + 6, 1.3f, 255, 255, 255);

            ui_.end_frame();
            finish_frame();
        }

        // Check completion.
        if (loading_done_chunks_ >= loading_total_chunks_) {
            MC_LOG_INFO("Loading complete: {}/{} chunks, entering Playing.", loading_done_chunks_, loading_total_chunks_);
            state_ = GameState::Playing;
            if (screenshot_wait_ticks_ >= 0) {
                glfwSetInputMode(renderer_.window(), GLFW_CURSOR, GLFW_CURSOR_NORMAL);
            } else {
                glfwSetInputMode(renderer_.window(), GLFW_CURSOR, GLFW_CURSOR_DISABLED);
            }
            first_mouse_ = true;
            
            if (is_new_world_ && worlds_.count(DimensionId::Overworld)) {
                World& w = *worlds_[DimensionId::Overworld];
                // Find a safe spawn point: scan the center chunk's heightmap
                // for the best surface position (highest non-water column near center).
                const Chunk* spawn_chunk = w.get_chunk({0, 0});
                if (spawn_chunk) {
                    int best_lx = 8, best_lz = 8;
                    int best_h = MIN_Y;
                    // Scan around center of chunk for good spawn
                    for (int lz = 4; lz < 12; ++lz) {
                        for (int lx = 4; lx < 12; ++lx) {
                            int h = spawn_chunk->heightmap[lz * CHUNK_SIZE + lx];
                            if (h > best_h && h > SEA_LEVEL) {
                                best_h = h;
                                best_lx = lx;
                                best_lz = lz;
                            }
                        }
                    }
                    // Fallback: if nothing above sea level, use center column
                    if (best_h <= MIN_Y) {
                        best_lx = 8;
                        best_lz = 8;
                        best_h = spawn_chunk->heightmap[best_lz * CHUNK_SIZE + best_lx];
                        if (best_h <= MIN_Y) best_h = SEA_LEVEL + 1;
                    }
                    // Ensure spawn point has 2 air blocks above (use world coords)
                    int wx = best_lx + spawn_chunk->pos.x * CHUNK_SIZE;
                    int wz = best_lz + spawn_chunk->pos.z * CHUNK_SIZE;
                    int spawn_y = best_h;
                    for (int y = best_h; y < MAX_Y - 3; ++y) {
                        BlockId b1 = w.get_block({wx, y, wz});
                        BlockId b2 = w.get_block({wx, y + 1, wz});
                        if (b1 == BLOCK_AIR && b2 == BLOCK_AIR) {
                            spawn_y = y;
                            break;
                        }
                    }
                    if (spawn_y > best_h + 8) spawn_y = best_h; // safety clamp
                    player_.pos.x = static_cast<float>(wx) + 0.5f;
                    player_.pos.z = static_cast<float>(wz) + 0.5f;
                    player_.pos.y = static_cast<float>(spawn_y) + 0.1f;
                    player_.prev_pos = player_.pos;
                    MC_LOG_INFO("Spawn at ({}, {}, {})", player_.pos.x, player_.pos.y, player_.pos.z);
                }
            }
            // Also fix prev_pos for interpolation
            player_.prev_pos = player_.pos;
            return;
        }
    }
}

// =================================================================
// GAME LOOP (Playing)
// =================================================================

void Game::run_game() {
    FixedTimestepLoop tick_loop;
    FrameTimer frame_timer;

    while (running_ && !renderer_.should_close() && (state_ == GameState::Playing || state_ == GameState::Inventory)) {
        float dt = frame_timer.delta_seconds();
        renderer_.poll_events();
        automation_.update(*this);
        process_input(dt);

        // ESC pauses
        if (keys_[GLFW_KEY_ESCAPE] &&
            glfwGetTime() - esc_toggle_time_ > 0.3) {
            esc_toggle_time_ = glfwGetTime();
            state_ = GameState::Paused;
            pause_action_ = PauseAction::None;
            esc_released_in_pause_ = false; // ignore held ESC inside pause
            glfwSetInputMode(renderer_.window(), GLFW_CURSOR, GLFW_CURSOR_NORMAL);
            return;
        }

        tick_loop.update([this] {
            if (screenshot_wait_ticks_ >= 0 && !running_) return;
            double tick_start = glfwGetTime();
            tick(); 
            double tick_end = glfwGetTime();
            double tick_ms = (tick_end - tick_start) * 1000.0;
            if (mspt_ == 0.0) mspt_ = tick_ms;
            else mspt_ = mspt_ * 0.9 + tick_ms * 0.1;
        });

        time_of_day_ = std::fmod(time_of_day_ + dt / 480.0f, 1.0f);
        renderer_.set_sky_brightness(day_brightness(time_of_day_));

        if (screenshot_wait_ticks_ < 0) {
            build_dirty_meshes(2);
        }

        render(tick_loop.alpha());

        // Deferred screenshot capture: take after render() drew the frame,
        // but before the next iteration. render() calls end_frame() which
        // swaps buffers, so we need to capture BEFORE end_frame. Actually
        // end_frame already swapped. We need to read the front buffer or
        // restructure. For now, render one more frame then capture.
        // Deferred screenshot capture: give the render thread a short grace
        // period so pending chunk meshes finish uploading before we grab the
        // frame (otherwise the shot shows holes).
        if (screenshot_ready_) {
            if (screenshot_grace_frames_ > 0) {
                --screenshot_grace_frames_;
                build_dirty_meshes(8);
            } else {
                take_screenshot(screenshot_output_.empty()
                    ? screenshot_dir() + "/bot_screenshot.bmp"
                    : screenshot_output_);
                spdlog::default_logger()->flush();
                running_ = false;
                return;
            }
        }

        FrameMark;
    }
}

// =================================================================
// PAUSE MENU
// =================================================================

void Game::run_paused() {
    while (running_ && !renderer_.should_close() && state_ == GameState::Paused) {
        renderer_.poll_events();
        automation_.update(*this);

        // ESC closes the settings overlay first; otherwise it resumes the game
        // (pause menu toggles, like in most sandbox games). The resume only
        // arms after the key has been released once, so the press that OPENED
        // the pause never immediately closes it again.
        if (glfwGetKey(renderer_.window(), GLFW_KEY_ESCAPE) == GLFW_RELEASE) {
            esc_released_in_pause_ = true;
        }
        if (esc_released_in_pause_ &&
            glfwGetKey(renderer_.window(), GLFW_KEY_ESCAPE) == GLFW_PRESS &&
            glfwGetTime() - esc_toggle_time_ > 0.3) {
            esc_toggle_time_ = glfwGetTime();
            if (settings_open_) {
                settings_open_ = false;
            } else {
                state_ = GameState::Playing;
                glfwSetInputMode(renderer_.window(), GLFW_CURSOR, GLFW_CURSOR_DISABLED);
                first_mouse_ = true;
                return;
            }
        }

        poll_ui_mouse(*this, ui_, renderer_.window());
        // Render the game world frozen behind (no present — the overlay, the
        // pause menu and this frame share one back-buffer present below).
        render(1.0f, /*present_after=*/false);

        // Dark overlay
        glDisable(GL_DEPTH_TEST);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        clear_screen_alpha(15, 20, 30, 180);
        glDisable(GL_BLEND);
        glEnable(GL_DEPTH_TEST);

        ui_.begin_frame();
        int sw = ui_.screen_width();
        int sh = ui_.screen_height();
        float cx = sw * 0.5f;

        if (settings_open_) {
            draw_settings_panel();
        } else {
            ui_.draw_text_centered("Gra wstrzymana", cx, sh * 0.22f, 5.0f, 220, 230, 255);

            float btn_w = 280, btn_h = 44, btn_y = sh * 0.32f;
            if (ui_.button("Kontynuuj", cx - btn_w * 0.5f, btn_y, btn_w, btn_h, 2.0f)) {
                state_ = GameState::Playing;
                glfwSetInputMode(renderer_.window(), GLFW_CURSOR, GLFW_CURSOR_DISABLED);
                first_mouse_ = true;
                return;
            }
            if (ui_.button("Opcje", cx - btn_w * 0.5f, btn_y + 56, btn_w, btn_h, 2.0f)) {
                settings_open_ = true;
                settings_tab_ = 0;
            }
            if (ui_.button("Zapisz grę", cx - btn_w * 0.5f, btn_y + 112, btn_w, btn_h, 2.0f)) {
                if (storage_) {
                    storage_->save_player_dat(player_);
                    for (auto& [id, w] : worlds_) storage_->save_all_dirty(*w);
                    current_world_meta_.time_played += 60;
                    WorldManager::save_meta(WorldManager::world_dir(current_world_meta_.name), current_world_meta_);
                }
            }
            if (ui_.button("Zapisz i wyjdź", cx - btn_w * 0.5f, btn_y + 168, btn_w, btn_h, 2.0f)) {
                if (storage_) {
                    storage_->save_player_dat(player_);
                    for (auto& [id, w] : worlds_) storage_->save_all_dirty(*w);
                    current_world_meta_.time_played += 60;
                    current_world_meta_.last_played = static_cast<int64_t>(std::time(nullptr));
                    WorldManager::save_meta(WorldManager::world_dir(current_world_meta_.name), current_world_meta_);
                }
                settings_open_ = false;
                return_to_menu();
                return;
            }
        }

        ui_.end_frame();
        finish_frame();
    }
}

void Game::process_input(float dt) {
    (void) dt;
    PlayerInput in;
    if (keys_[GLFW_KEY_W]) in.forward += 1.0f;
    if (keys_[GLFW_KEY_S]) in.forward -= 1.0f;
    if (keys_[GLFW_KEY_D]) in.strafe += 1.0f;
    if (keys_[GLFW_KEY_A]) in.strafe -= 1.0f;
    in.jump = keys_[GLFW_KEY_SPACE] != 0;
    in.sneak = keys_[GLFW_KEY_LEFT_SHIFT] != 0;
    in.sprint = keys_[GLFW_KEY_LEFT_CONTROL] != 0;
    last_input_ = in; // stash for tick()
}

void Game::tick() {
    ZoneScoped;
    ++current_tick_;
    Profiler::get().begin_section(ProfileSection::TotalTick);

    // Auto-screenshot for bot mode (--screenshot CLI flag).
    if (screenshot_wait_ticks_ >= 0 && current_tick_ >= screenshot_wait_ticks_) {
        if (!screenshot_ready_) {
            MC_LOG_WARN("Bot mode: taking screenshot at tick {}", current_tick_);
            screenshot_ready_ = true;
            screenshot_grace_frames_ = 20; // let pending meshes upload first
        }
        drain_gen_results();
        build_dirty_meshes(8);
        Profiler::get().end_section(ProfileSection::TotalTick);
        return;
    }

    // Skip expensive gameplay operations in bot/screenshot mode.
    if (screenshot_wait_ticks_ >= 0) {
        drain_gen_results();
        build_dirty_meshes(8);
        Profiler::get().end_section(ProfileSection::TotalTick);
        return;
    }
    for (auto& msg : chat_log_) {
        if (msg.time_left > 0.0f) {
            msg.time_left -= 0.05f; // 20 ticks per second = 0.05s per tick
        }
    }
    World& w = *worlds_[current_dimension_];

    {
        ProfileScope ps(ProfileSection::EntityTicking);
        tick_player(w, player_, last_input_);
        survival::tick_hunger(player_, {player_.sprinting, last_input_.jump && !last_jump_});
        last_jump_ = last_input_.jump;
        // HUD feedback timers (cosmetic): hurt vignette + death overlay.
        if (player_.health < prev_health_visual_ - 0.01f) hurt_flash_ = 1.0f;
        prev_health_visual_ = player_.health;
        hurt_flash_ = std::max(0.0f, hurt_flash_ - 3.0f * static_cast<float>(TICK_INTERVAL_SEC));
        death_visual_ = std::max(0.0f, death_visual_ - 0.6f * static_cast<float>(TICK_INTERVAL_SEC));
        if (player_.health <= 0.0f && player_.mode != GameMode::Creative &&
            player_.mode != GameMode::Spectator) {
            handle_player_death();
        }
        handle_clicks();
        attack_timer_ = std::max(0.0f, attack_timer_ - static_cast<float>(TICK_INTERVAL_SEC));

        // One-shot melee attack per click edge; progressive mining lives in
        // handle_clicks and only runs while the button stays held.
        if (left_clicked_edge_) {
            left_clicked_edge_ = false;
            player_.is_swinging = true;
            survival::add_exhaustion(player_, survival::EXHAUSTION_ATTACK_SWING);

            ItemStack held = player_.inventory.get_selected_item();
            if (attack_timer_ <= 0.0f) {
                attack_timer_ = mining::weapon_attack_cooldown(held.item);

                float reach = (player_.mode == GameMode::Creative) ? REACH_CREATIVE : REACH_SURVIVAL;
                Vec3 eye = player_.eye_position();
                Vec3 look = forward_from_yaw_pitch(player_.yaw, player_.pitch);
                for (auto& m : mobs_) {
                    if (!m.alive) continue;
                    Vec3 to_mob = (m.pos + Vec3(0, 0.9f, 0)) - eye;
                    float dist = std::sqrt(to_mob.x * to_mob.x + to_mob.y * to_mob.y + to_mob.z * to_mob.z);
                    if (dist > reach + 1.0f) continue;
                    to_mob.x /= dist; to_mob.y /= dist; to_mob.z /= dist;
                    // Line of sight check to prevent hitting through walls/solid blocks
                    auto wall_hit = voxel_raycast(w, eye, to_mob, dist);
                    if (wall_hit) continue;
                    float dot = look.x * to_mob.x + look.y * to_mob.y + look.z * to_mob.z;
                    if (dot > 0.8f) {
                        bool was_alive = m.alive;

                        bool crit = !player_.on_ground && player_.velocity.y < 0.0f;
                        float enchant_bonus = sharpness_bonus(
                            held.enchant_level_of(EnchantType::Sharpness));
                        float damage = calculate_attack_damage(mining::weapon_damage(held.item),
                                                               1.0f /*full swing*/, crit, enchant_bonus);
                        damage *= (1.0f - calculate_armor_reduction(damage, m.armor_points, m.armor_toughness));
                        damage *= (1.0f - calculate_protection_reduction(m.protection_epf));
                        if (player_.mode == GameMode::Creative) damage = 999.0f;
                        m.apply_damage(damage);

                        // Knockback away from the player
                        float kb = calculate_knockback(0.4f, 0.0f, player_.sprinting, m.knockback_resistance);
                        Vec3 knock_dir = Vec3(look.x, 0.3f, look.z);
                        float len = std::sqrt(knock_dir.x * knock_dir.x + knock_dir.y * knock_dir.y + knock_dir.z * knock_dir.z);
                        if (len > 0.001f) {
                            knock_dir.x /= len; knock_dir.y /= len; knock_dir.z /= len;
                            m.velocity = m.velocity + knock_dir * kb;
                        }
                        m.velocity.y += 0.4f;

                        if (was_alive && !m.alive) {
                            on_mob_killed_by_player(m);
                        }

                        // Durability per landed hit: weapons 1, tools 2, fists
                        // free. Unbreaking can skip the wear point.
                        {
                            uint16_t wear = mining::attack_durability_cost(held.item);
                            if (wear > 0 && !mining::unbreaking_blocks(
                                                held.enchant_level_of(EnchantType::Unbreaking),
                                                rng_.next_float())) {
                                if (mining::damage_tool(held, wear)) {
                                    add_chat_message("Bron sie zepsula!");
                                }
                            }
                            player_.inventory.set_slot(player_.inventory.get_selected_hotbar_slot(), held);
                        }
                        break; // one target per swing
                    }
                }
            }
        }
    }

    {
        ProfileScope ps(ProfileSection::ScheduledTicks);
        ticks_.process(w, current_tick_, rng_);
        std::vector<Vec3> plate_entities;
        plate_entities.reserve(mobs_.size() + 1);
        plate_entities.push_back(player_.pos);
        for (const auto& m : mobs_) plate_entities.push_back(m.pos);
        redstone_.tick(w, plate_entities);
    }

    // Void damage (PHASE15 §4.2)
    if (player_.pos.y < MIN_Y - 16) {
        if (current_dimension_ == DimensionId::End) {
            player_.health -= 8.0f;
            if (player_.health <= 0.0f) {
                player_.pos = Vec3(0.5f, 100.0f, 0.5f);
                player_.velocity = Vec3(0, 0, 0);
                player_.health = player_.max_health;
                player_.dimension = DimensionId::Overworld;
                current_dimension_ = DimensionId::Overworld;
            }
        } else {
            player_.pos.y = static_cast<float>(MAX_Y);
            player_.velocity = Vec3(0, 0, 0);
        }
    }
    if (current_tick_ % 20 == 0) {
        ProfileScope ps(ProfileSection::RandomTicks);
        ticks_.random_ticks(w, rng_);
    }
    if (current_tick_ % 4 == 0) {
        ProfileScope ps(ProfileSection::FluidProcessing);
        ticks_.process_fluids(w, current_tick_, rng_);
    }
    if (current_tick_ % 100 == 0) unload_distant_chunks();

    EventBus::get().publish(PlayerMoveEvent{player_.pos.x, player_.pos.y, player_.pos.z});

    {
        ProfileScope ps(ProfileSection::WeatherTick);
        weather_.tick();
        particles_.update(w);
    }

    // Weather particles
    if (settings_.particles && weather_.current_state() != WeatherState::Clear) {
        float intensity = weather_.intensity();
        int particle_count = static_cast<int>(intensity * 100);
        for (int i = 0; i < particle_count; ++i) {
            float dx = (rng_.next_float() - 0.5f) * 40.0f;
            float dz = (rng_.next_float() - 0.5f) * 40.0f;
            float dy = rng_.next_float() * 20.0f;
            Vec3 pos = player_.pos + Vec3(dx, dy + 10.0f, dz);
            Particle p;
            p.pos = pos;
            p.size = 0.1f;
            p.age = 0;
            p.max_age = 60;
            p.collision = false;
            p.type = ParticleType::BlockDust;
            if (w.get_block({(int)pos.x, (int)pos.y, (int)pos.z}) == BLOCK_AIR) {
                if (weather_.current_state() == WeatherState::Rain || weather_.current_state() == WeatherState::Thunder) {
                    p.velocity = Vec3(0, -15.0f, 0);
                    p.r = 0.2f; p.g = 0.3f; p.b = 0.8f;
                    p.size = 0.05f;
                }
                particles_.spawn(p);
            }
        }
    }
    
    // Torch ambience: flames and smoke from nearby torches.
    if (settings_.particles && current_tick_ % 4 == 0) {
        const int px = static_cast<int>(std::floor(player_.pos.x));
        const int py = static_cast<int>(std::floor(player_.pos.y));
        const int pz = static_cast<int>(std::floor(player_.pos.z));
        int torches = 0;
        for (int dy = -6; dy <= 8 && torches < 16; ++dy) {
            for (int dx = -12; dx <= 12 && torches < 16; ++dx) {
                for (int dz = -12; dz <= 12 && torches < 16; ++dz) {
                    if (w.get_block({px + dx, py + dy, pz + dz}) != BLOCK_TORCH) continue;
                    ++torches;
                    Particle p;
                    p.pos = Vec3(px + dx + 0.5f, py + dy + 0.62f, pz + dz + 0.5f);
                    p.velocity = Vec3((rng_.next_float() - 0.5f) * 0.02f,
                                      0.25f + rng_.next_float() * 0.15f,
                                      (rng_.next_float() - 0.5f) * 0.02f);
                    p.age = 0;
                    p.max_age = 14 + rng_.next_int(10);
                    p.size = 0.09f + rng_.next_float() * 0.04f;
                    p.gravity = 0.0f;
                    p.collision = false;
                    p.type = ParticleType::Flame;
                    p.r = 1.0f;
                    p.g = 0.62f + rng_.next_float() * 0.2f;
                    p.b = 0.18f;
                    particles_.spawn(p);
                    if (rng_.next_int(3) == 0) {
                        Particle s;
                        s.pos = p.pos + Vec3(0.0f, 0.15f, 0.0f);
                        s.velocity = Vec3((rng_.next_float() - 0.5f) * 0.03f,
                                          0.5f + rng_.next_float() * 0.3f,
                                          (rng_.next_float() - 0.5f) * 0.03f);
                        s.age = 0;
                        s.max_age = 24 + rng_.next_int(16);
                        s.size = 0.08f + rng_.next_float() * 0.05f;
                        s.gravity = -0.01f; // negative gravity = rising smoke
                        s.collision = false;
                        s.type = ParticleType::Smoke;
                        s.r = s.g = s.b = 0.35f;
                        s.a = 0.5f;
                        particles_.spawn(s);
                    }
                }
            }
        }
    }

    {
        ProfileScope ps(ProfileSection::MobSpawning);
        // Refresh per-mob player targeting (pointer must stay current every
        // tick; callbacks are bound once per mob lifetime).
        for (auto& m : mobs_) {
            m.target_player_pos = &player_.pos;
            if (!m.attack_player) {
                m.attack_player = [this](float damage, const Vec3& src) {
                    mob_attack_player(damage, src);
                };
            }
            if (!m.shoot_arrow) {
                m.shoot_arrow = [this](const Vec3& from, const Vec3& vel) {
                    if (projectiles_.size() >= 64) return; // hard cap
                    Projectile p;
                    p.pos = from;
                    p.velocity = vel;
                    projectiles_.push_back(p);
                    ++arrows_fired_total_; // lifetime counter (test invariant)
                };
            }
        }
        mob_spawner_.tick(w, mobs_, rng_, current_tick_, {player_.pos}, time_of_day_);
        tick_projectiles();
        tick_furnaces();
        if (current_tick_ % 100 == 0) purge_invalid_furnaces();
    }

    {
        ProfileScope ps(ProfileSection::ChunkManagement);
        drain_gen_results();
        update_chunks();
    }
    
    // Portal teleport logic
    if (portal_cooldown_ > 0.0f) {
        portal_cooldown_ -= static_cast<float>(TICK_INTERVAL_SEC);
    } else {
        BlockPos pb(static_cast<int>(std::floor(player_.pos.x)),
                    static_cast<int>(std::floor(player_.pos.y)),
                    static_cast<int>(std::floor(player_.pos.z)));
        if (w.get_block(pb) == BLOCK_NETHER_PORTAL || w.get_block({pb.x, pb.y + 1, pb.z}) == BLOCK_NETHER_PORTAL) {
            portal_cooldown_ = 5.0f;
            DimensionId next_dim = (current_dimension_ == DimensionId::Overworld) ? DimensionId::Nether : DimensionId::Overworld;
            if (next_dim == DimensionId::Nether) {
                player_.pos.x /= 8.0f;
                player_.pos.z /= 8.0f;
            } else if (current_dimension_ == DimensionId::Nether) {
                player_.pos.x *= 8.0f;
                player_.pos.z *= 8.0f;
            }
            for (auto cp : w.loaded_positions()) renderer_.remove_mesh(cp);
            gen_in_flight_.clear();
            mesh_in_flight_.clear();
            current_dimension_ = next_dim;
            player_.dimension = next_dim;
            MC_LOG_INFO("Teleported to dimension {}", static_cast<int>(next_dim));
        }
    }
    
    {
        ProfileScope ps(ProfileSection::Autosave);
        if (storage_ && current_tick_ % 6000 == 0) {
            for (auto& [id, world] : worlds_) {
                storage_->save_all_dirty(*world);
            }
            storage_->save_time_of_day(time_of_day_);
            storage_->save_player_dat(player_);
            std::vector<FurnaceSave> fsave;
            fsave.reserve(furnaces_.size());
            for (const auto& [key, f] : furnaces_) {
                FurnaceSave fs;
                fs.dim = key.dim;
                fs.x = key.x; fs.y = key.y; fs.z = key.z;
                fs.in_item = f.input.item; fs.in_count = f.input.count;
                fs.fuel_item = f.fuel.item; fs.fuel_count = f.fuel.count;
                fs.out_item = f.output.item; fs.out_count = f.output.count;
                fs.burn_left = f.burn_left; fs.burn_total = f.burn_total;
                fs.cook = f.cook_progress;
                fs.pending_xp = f.pending_xp;
                fsave.push_back(fs);
            }
            storage_->save_furnaces(fsave);
        }
    }

    Profiler::get().end_section(ProfileSection::TotalTick);
}

void Game::render(float alpha, bool present_after) {
    ZoneScoped;
    Vec3 interp_pos = player_.prev_pos + (player_.pos - player_.prev_pos) * alpha;
    camera_.position = Vec3(interp_pos.x, interp_pos.y + PLAYER_EYE_HEIGHT, interp_pos.z);
    camera_.yaw = player_.yaw;
    camera_.pitch = player_.pitch;
    
    // View bobbing (Sine Wave tutorial method)
    float walk_dist = player_.prev_walk_dist + (player_.walk_dist - player_.prev_walk_dist) * alpha;
    float bob_amp = player_.prev_bob_anim + (player_.bob_anim - player_.prev_bob_anim) * alpha;
    bob_amp *= 0.5f; // Reduce viewbobbing by half
    if (!settings_.view_bobbing) {
        walk_dist = 0.0f;
        bob_amp = 0.0f;
    }

    // Horizontal frequency vs Vertical frequency
    float freq_horizontal = 3.14159265f;
    float freq_vertical = freq_horizontal * 2.0f; // Double frequency for vertical

    camera_.view_offset.x = std::sin(walk_dist * freq_horizontal) * bob_amp * 0.15f;
    camera_.view_offset.y = std::sin(walk_dist * freq_vertical) * bob_amp * 0.15f; // Arch motion
    camera_.view_offset.z = 0.0f;
    
    // Smooth camera roll and pitch based on the same sine waves
    camera_.roll = std::sin(walk_dist * freq_horizontal) * bob_amp * 0.05f;
    camera_.bob_pitch = std::abs(std::cos(walk_dist * freq_horizontal)) * bob_amp * 0.05f - (bob_amp * 0.025f);

    camera_.aspect = std::max(static_cast<float>(renderer_.width()), 1.0f) / std::max(static_cast<float>(renderer_.height()), 1.0f);
    camera_.fov = settings_.fov;
    camera_.far_plane = static_cast<float>(render_distance_) * CHUNK_SIZE + 64.0f;

    float sky_brightness = 1.0f - std::abs(time_of_day_ - 0.5f) * 1.5f; // day/night
    if (sky_brightness > 1.0f) sky_brightness = 1.0f;
    sky_brightness -= weather_.sky_darkening();
    if (sky_brightness < 0.1f) sky_brightness = 0.1f;
    renderer_.set_sky_brightness(sky_brightness);
    // t=0.25 sunrise (horizon), t=0.5 noon (overhead), t=0 midnight (below).
    renderer_.set_sun_angle((time_of_day_ - 0.25f) * 6.2831853f);
    // Underwater post effect only when the CAMERA (eye) is inside a water
    // block — wading with the head above the surface must stay unaffected.
    bool eye_in_water = false;
    if (player_.in_water) {
        const Vec3& eye = camera_.position;
        BlockPos eye_pos{static_cast<int>(std::floor(eye.x)),
                         static_cast<int>(std::floor(eye.y)),
                         static_cast<int>(std::floor(eye.z))};
        if (worlds_.contains(current_dimension_) && worlds_.at(current_dimension_)) {
            eye_in_water = is_water(worlds_.at(current_dimension_)->get_block(eye_pos));
        }
        renderer_.set_fog_mode(1, 0.1f);
    } else {
        renderer_.set_fog_mode(0, 0.0f);
    }
    renderer_.set_underwater(eye_in_water);

    renderer_.begin_frame(camera_);
    renderer_.set_shadow_casters(&mobs_, static_cast<float>(glfwGetTime()));
    renderer_.render_opaque(camera_);
    renderer_.render_mobs(camera_, mobs_, static_cast<float>(glfwGetTime()));
    renderer_.draw_projectiles(camera_, projectiles_);

    if (state_ == GameState::Playing) {
        draw_selection_outline();
    }

    renderer_.set_held_block(player_.inventory.get_selected_item().item);
    if (settings_.particles) particles_.draw(camera_, renderer_.atlas());
    renderer_.render_transparent(camera_);

    float swing = player_.prev_swing_progress + (player_.swing_progress - player_.prev_swing_progress) * alpha;
    renderer_.set_hand_swing(swing);
    renderer_.set_hand_bobbing(walk_dist, bob_amp);
    renderer_.set_mining_overlay(mining_valid_ ? mining_pos_ : BlockPos{0, -1, 0},
                                 mining_valid_ ? mining_progress_ : 0.0f);

    renderer_.end_frame();

    if (state_ == GameState::Playing) {
        draw_hud();
    } else if (state_ == GameState::Inventory) {
        draw_inventory_ui();
    }

    // Pause draws its overlay + menu into the same back buffer and presents
    // once itself — presenting here too made the pause screen flicker between
    // two half-finished frames.
    if (present_after) finish_frame();
}

void Game::drain_gen_results() {
    // Gen results arrive pre-lit from the worker; insertion is cheap, so the
    // only real limiter is how many inserts one tick may absorb.
    int budget = screenshot_wait_ticks_ >= 0 ? 4 : 8;
    GenResult res;
    while (budget > 0 && gen_channel_.try_pop(res)) {
        gen_in_flight_.erase({res.dim, res.pos});
        Chunk* raw = res.chunk.get();
        worlds_[res.dim]->insert_chunk(std::move(res.chunk));
        // Fallback only: normally the worker pre-lit the chunk already.
        if (raw->light_dirty.load(std::memory_order_relaxed)) {
            compute_light(*raw, *worlds_[res.dim]);
        }
        --budget;
    }

    // Cap GPU uploads per call: a burst of finished chunks after a fast
    // move/teleport must not stall one frame with dozens of glBufferData.
    int uploads = 6;
    std::pair<ChunkPos, ChunkMeshData> mres;
    while (uploads > 0 && mesh_channel_.try_pop(mres)) {
        mesh_in_flight_.erase({current_dimension_, mres.first});
        renderer_.upload_mesh(mres.first, mres.second);
        recycled_meshes_.push(std::move(mres.second));
        --uploads;
    }
}

void Game::update_chunks() {
    ZoneScoped;
    ChunkPos pc = chunk_from_block(BlockPos(static_cast<int>(std::floor(player_.pos.x)), 0,
                                           static_cast<int>(std::floor(player_.pos.z))));
    int rd = render_distance_;
    World& w = *worlds_[current_dimension_];
    DimensionId dim = current_dimension_;

    // Submit generation for missing chunks within render distance and world bounds.
    for (int dz = -rd; dz <= rd; ++dz) {
        for (int dx = -rd; dx <= rd; ++dx) {
            ChunkPos cp{pc.x + dx, pc.z + dz};
            if (dx * dx + dz * dz > rd * rd) continue;
            if (!in_world_bounds(cp)) continue;
            if (w.has_chunk(cp)) continue;
            std::pair<DimensionId, ChunkPos> key{dim, cp};
            if (gen_in_flight_.contains(key)) continue;
            gen_in_flight_.insert(key);
            pool_.submit([this, cp, dim] {
                auto chunk = mc::World::chunk_pool.acquire();
                chunk->reset(cp);
                if (!storage_ || !storage_->load_chunk(*chunk, dim)) {
                    generators_[dim]->generate(*chunk);
                }
                // Pre-light on the worker before the chunk is shared with the
                // main thread. cross_chunk_writes=false keeps every write
                // inside this chunk, so no other chunk is touched here; a
                // later in-game light recompute (block edits) restores the
                // cross-chunk BFS bleed exactly like before.
                compute_light(*chunk, *worlds_[dim], /*cross_chunk_writes=*/false);
                gen_channel_.push(GenResult{dim, cp, std::move(chunk)});
            });
        }
    }
}

void Game::unload_distant_chunks() {
    ChunkPos pc = chunk_from_block(BlockPos(static_cast<int>(std::floor(player_.pos.x)), 0,
                                           static_cast<int>(std::floor(player_.pos.z))));
    int ud = render_distance_ + 2;
    World& w = *worlds_[current_dimension_];
    auto loaded = w.loaded_positions();
    for (ChunkPos cp : loaded) {
        if (pc.distance_sq(cp) > ud * ud) {
            Chunk* ch = w.get_chunk(cp);
            if (ch && ch->is_dirty() && storage_) {
                storage_->save_chunk(*ch, current_dimension_);
            }
            renderer_.remove_mesh(cp);
            w.remove_chunk(cp);
        }
    }
}

void Game::build_dirty_meshes(int budget) {
    World& w = *worlds_[current_dimension_];
    if (w.dirty_chunks_.empty()) return; // nothing to do: skip the scan + sort

    ChunkPos pc = chunk_from_block(BlockPos(static_cast<int>(std::floor(player_.pos.x)), 0,
                                           static_cast<int>(std::floor(player_.pos.z))));
    int rd = render_distance_;
    DimensionId dim = current_dimension_;

    dirty_scratch_.clear();
    for (const auto& cp : w.dirty_chunks_) {
        Chunk* chunk = w.get_chunk(cp);
        if (!chunk || !chunk->is_dirty()) {
            // Stale entry (already built, or marked by a neighbor sync whose
            // build consumed the flag) — prune so the queue stays honest.
            stale_scratch_.push_back(cp);
            continue;
        }
        dirty_scratch_.emplace_back(pc.distance_sq(cp), cp);
    }
    for (ChunkPos cp : stale_scratch_) w.dirty_chunks_.erase(cp);
    stale_scratch_.clear();
    if (dirty_scratch_.empty()) return;
    std::sort(dirty_scratch_.begin(), dirty_scratch_.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });

    int built = 0;
    for (auto& [dist, cp] : dirty_scratch_) {
        if (built >= budget) break;

        std::pair<DimensionId, ChunkPos> key{dim, cp};
        auto inflight = mesh_in_flight_.find(key);
        if (inflight != mesh_in_flight_.end()) {
            // Guard expired (its worker result was lost, e.g. in a state
            // transition) — drop it so the chunk can rebuild.
            if (current_tick_ - inflight->second < 40) continue;
            mesh_in_flight_.erase(inflight);
        }

        std::array<std::shared_ptr<Chunk>, 9> cached_chunks;
        bool neighbors_ok = true;
        for (int dz = -1; dz <= 1; ++dz) {
            for (int dx = -1; dx <= 1; ++dx) {
                ChunkPos n{cp.x + dx, cp.z + dz};
                if (!in_world_bounds(n)) continue;
                auto it = w.chunks().find(n);
                if (it != w.chunks().end()) {
                    cached_chunks[(dz + 1) * 3 + (dx + 1)] = it->second;
                } else if (pc.distance_sq(n) <= rd * rd) {
                    neighbors_ok = false;
                }
            }
        }
        if (!neighbors_ok) continue;

        Chunk* chunk = cached_chunks[1 * 3 + 1].get();
        if (!chunk) continue;
        if (chunk->light_dirty.load(std::memory_order_relaxed)) compute_light(*chunk, w);

        mesh_in_flight_.insert({key, current_tick_});

        ChunkMeshData data;
        recycled_meshes_.try_pop(data);

        pool_.submit([this, cached_chunks, cp, data = std::move(data)]() mutable {
            try {
                build_chunk_mesh(cached_chunks, cp, renderer_.atlas(), data);
            } catch (const std::exception& e) {
                MC_LOG_ERROR("[MESH] build_chunk_mesh ({},{}) threw: {}", cp.x, cp.z, e.what());
            } catch (...) {
                MC_LOG_ERROR("[MESH] build_chunk_mesh ({},{}) threw unknown exception", cp.x, cp.z);
            }
            mesh_channel_.push(std::make_pair(cp, std::move(data)));
        });

        chunk->dirty.store(false, std::memory_order_relaxed);
        w.dirty_chunks_.erase(cp);
        ++built;
    }
}

void Game::handle_clicks() {
    // Interaction is a Playing-state activity; the inventory screen owns the
    // mouse. Stale held flags must not keep mining/placing behind the UI.
    if (state_ != GameState::Playing) {
        break_held_ = false;
        place_held_ = false;
        left_clicked_edge_ = false;
        mining_valid_ = false;
        mining_progress_ = 0.0f;
        return;
    }

    break_cooldown_ -= static_cast<float>(TICK_INTERVAL_SEC);
    place_cooldown_ -= static_cast<float>(TICK_INTERVAL_SEC);

    float reach = (player_.mode == GameMode::Creative) ? REACH_CREATIVE : REACH_SURVIVAL;
    World& w = *worlds_[current_dimension_];

    // ---- Progressive mining while the left button is held ----
    if (!break_held_) {
        mining_valid_ = false;
        mining_progress_ = 0.0f;
    } else {
        auto hit = voxel_raycast(w, player_.eye_position(),
                                 forward_from_yaw_pitch(player_.yaw, player_.pitch), reach);
        BlockPos target = hit ? hit->block_pos : BlockPos{0, 0, 0};
        bool same_target = mining_valid_ && hit &&
                           target.x == mining_pos_.x && target.y == mining_pos_.y && target.z == mining_pos_.z;

        if (!hit || hit->block_pos.y < MIN_Y) {
            mining_valid_ = false;
            mining_progress_ = 0.0f;
        } else {
            BlockId block = w.get_block(target);
            if (!same_target || block == BLOCK_AIR) {
                mining_pos_ = target;
                mining_progress_ = 0.0f;
                mining_valid_ = true;
            }

            ItemStack selected = player_.inventory.get_selected_item();
            float need = mining::break_time_seconds(block, selected);
            if (need == mining::UNBREAKABLE) {
                // Bedrock & co: hold forever, break nothing.
                mining_progress_ = 0.0f;
            } else if (player_.mode == GameMode::Creative || need <= 0.0f) {
                mining_progress_ = 1.0f; // instant in creative / for foliage
            } else {
                mining_progress_ += static_cast<float>(TICK_INTERVAL_SEC) / need;
            }

            if (settings_.particles && block != BLOCK_AIR && rng_.next_int(3) == 0) {
                particles_.spawn_block_dust(target, block, renderer_.atlas());
            }

            if (mining_progress_ >= 1.0f) {
                if (interact_break(w, player_, reach)) {
                    player_.is_swinging = true;
                    redstone_.on_block_changed(w, target);
                    survival::add_exhaustion(player_, survival::EXHAUSTION_MINE_BLOCK);
                    ItemStack held = player_.inventory.get_selected_item();
                    auto drop = mining::drop_for(block, held.item);
                    if (drop) {
                        ItemStack loot = mining::resolve_drop(*drop, rng_.next_float());
                        if (!loot.is_empty()) {
                            player_.inventory.add_item_to_main(loot);
                        }
                        int xp = survival::xp_reward_for_block(block);
                        if (xp > 0) survival::gain_xp(player_, xp);
                    }
                    // Breaking a furnace spills its contents (no ground items yet).
                    if (block == BLOCK_FURNACE) {
                        FurnaceKey key = furnace_key_from_block(target);
                        auto it = furnaces_.find(key);
                        if (it != furnaces_.end()) {
                            auto& f = it->second;
                            survival::gain_xp(player_, static_cast<int>(f.pending_xp));
                            for (ItemStack* s : {&f.input, &f.fuel, &f.output}) {
                                if (!s->is_empty()) player_.inventory.add_item_to_main(*s);
                            }
                            furnaces_.erase(it);
                        }
                    }
                    // Tool wear: one use per block that actually resists
                    // (Unbreaking can skip the wear point).
                    if (mining::wear_applies(block)) {
                        uint8_t slot = player_.inventory.get_selected_hotbar_slot();
                        if (!mining::unbreaking_blocks(
                                held.enchant_level_of(EnchantType::Unbreaking),
                                rng_.next_float())) {
                            if (mining::damage_tool(held, 1)) {
                                add_chat_message("Narzedzie sie zepsulo!");
                            }
                            player_.inventory.set_slot(slot, held);
                        }
                    }
                    if (settings_.particles) {
                        particles_.spawn_block_dust(target, block, renderer_.atlas());
                    }
                }
                mining_progress_ = 0.0f;
                mining_valid_ = false; // force re-acquire next tick
            }
        }
    }

    // ---- Right button: use item / interact with blocks ----
    if (place_held_ && place_cooldown_ <= 0.0f) {
        place_cooldown_ = 0.25f;
        ItemStack held = player_.inventory.get_selected_item();

        // 0. Bow: hold to draw; the release edge fires further below.
        if (mining::is_bow(held.item)) {
            if (player_.mode == GameMode::Creative || consume_arrow_for_shot_ready()) {
                bow_charging_ = true;
                bow_charge_ += static_cast<float>(TICK_INTERVAL_SEC);
            } else if (bow_charge_ <= 0.0f) {
                add_chat_message("Brak strzal");
                place_cooldown_ = 0.5f; // throttle the hint
            }
        }

        // 1. Food first — eating never interacts with the world.
        if (!held.is_empty() && mining::food_value(held.item) &&
            (player_.food_level < 20 || player_.health < player_.max_health)) {
            if (survival::eat_from_selected(player_) > 0) {
                add_chat_message("Yum.");
            }
            return;
        }

        Vec3 eye = player_.eye_position();
        Vec3 look = forward_from_yaw_pitch(player_.yaw, player_.pitch);
        auto hit = voxel_raycast(w, eye, look, reach);

        // 2. Crafting table opens its screen (sneak bypasses to allow
        //    placing; never while drawing a bow).
        if (hit && !player_.sneaking && !bow_charging_ &&
            w.get_block(hit->block_pos) == BLOCK_CRAFTING_TABLE) {
            craft_full_grid_ = true;
            state_ = GameState::Inventory;
            glfwSetInputMode(renderer_.window(), GLFW_CURSOR, GLFW_CURSOR_NORMAL);
            return;
        }

        // 2b. Quest NPC opens its dialog.
        if (hit && !player_.sneaking && w.get_block(hit->block_pos) == BLOCK_QUEST_NPC) {
            open_npc_dialog();
            return;
        }

        // 3. Furnace opens its smelting screen.
        if (hit && !player_.sneaking && !bow_charging_ &&
            w.get_block(hit->block_pos) == BLOCK_FURNACE) {
            open_furnace_ = furnace_key_from_block(hit->block_pos);
            furnace_open_ = true;
            state_ = GameState::Inventory;
            glfwSetInputMode(renderer_.window(), GLFW_CURSOR, GLFW_CURSOR_NORMAL);
            return;
        }

        // 2c. Lever toggles its state (no screen).
        if (hit && !player_.sneaking && !bow_charging_ &&
            is_lever(w.get_block(hit->block_pos))) {
            BlockPos p = hit->block_pos;
            BlockId b = w.get_block(p);
            w.set_block(p, b == BLOCK_LEVER_OFF ? BLOCK_LEVER_ON : BLOCK_LEVER_OFF);
            redstone_.on_block_changed(w, p);
            return;
        }

        // 2d. Enchanting table opens its screen.
        if (hit && !player_.sneaking && !bow_charging_ &&
            w.get_block(hit->block_pos) == BLOCK_ENCHANTING_TABLE) {
            open_enchanting_table(hit->block_pos);
            return;
        }

        // 3. Place the held block into the world (never while drawing a bow).
        if (!bow_charging_ && held.count > 0 && held.item != ITEM_AIR && held.item < BLOCK_COUNT) {
            if (interact_place(w, player_, reach)) {
                player_.is_swinging = true;
                if (hit) {
                    BlockPos placed = hit->block_pos + offset(hit->face);
                    if (held.item == BLOCK_FURNACE) {
                        FurnaceKey key = furnace_key_from_block(placed);
                        furnaces_[key]; // create empty state on placement
                    }
                    redstone_.on_block_changed(w, placed);
                }
                held.count--;
                if (held.count == 0) held.item = ITEM_AIR;
                player_.inventory.set_slot(player_.inventory.get_selected_hotbar_slot(), held);
            }
        }
    }

    // Bow release: loose the arrow if the draw was long enough.
    if (place_released_) {
        place_released_ = false;
        if (bow_charging_) {
            bow_charging_ = false;
            if (bow_charge_ >= mining::BOW_MIN_CHARGE) {
                fire_player_arrow(bow_charge_);
            }
            bow_charge_ = 0.0f;
        }
    }
}

bool Game::consume_arrow_for_shot_ready() {
    for (int i = 0; i < 36; ++i) {
        if (player_.inventory.get_slot(i).item == ITEM_ARROW &&
            player_.inventory.get_slot(i).count > 0) {
            return true;
        }
    }
    return false;
}

void Game::fire_player_arrow(float charge) {
    ItemStack held = player_.inventory.get_selected_item();
    if (!mining::is_bow(held.item)) return;

    // Ammo + durability: free in creative.
    if (player_.mode != GameMode::Creative) {
        bool consumed = false;
        for (int i = 0; i < 36 && !consumed; ++i) {
            ItemStack s = player_.inventory.get_slot(i);
            if (s.item == ITEM_ARROW && s.count > 0) {
                s.count -= 1;
                if (s.count == 0) s.item = ITEM_AIR;
                player_.inventory.set_slot(i, s);
                consumed = true;
            }
        }
        if (!consumed) {
            add_chat_message("Brak strzal");
            return;
        }
        if (mining::damage_tool(held, 1)) {
            add_chat_message("Luk sie zepsul!");
        }
        player_.inventory.set_slot(player_.inventory.get_selected_hotbar_slot(), held);
    }

    Vec3 dir = forward_from_yaw_pitch(player_.yaw, player_.pitch);
    Projectile p;
    p.from_mob = false;
    p.pos = player_.eye_position() + dir * 0.5f;
    p.velocity = dir * mining::bow_speed(charge);
    p.damage = mining::bow_damage(charge);
    p.life = 120;
    projectiles_.push_back(p);
    ++arrows_fired_total_;
    ai_diag("[arrow] player shot spd=%.2f dmg=%.1f", mining::bow_speed(charge),
            mining::bow_damage(charge));

    player_.is_swinging = true;
    survival::add_exhaustion(player_, survival::EXHAUSTION_ATTACK_SWING);
}

void Game::on_mob_killed_by_player(const Mob& m) {
    survival::gain_xp(player_, m.xp_reward);
    if (m.type == MobType::Cow || m.type == MobType::Pig) {
        uint8_t drops = static_cast<uint8_t>(1 + rng_.next_int(2));
        ItemStack meat(ITEM_RAW_MEAT, drops);
        player_.inventory.add_item_to_main(meat);
    } else if (static_cast<uint8_t>(m.type) >= kCustomMobBase) {
        // Custom species drop whatever their .mob.json declares.
        const MobSpec* spec = MobRegistry::instance().by_id(static_cast<uint8_t>(m.type));
        if (spec && !spec->drop_item.empty() && spec->drop_max > 0) {
            int lo = spec->drop_min, hi = spec->drop_max;
            int count = lo >= hi ? lo : lo + rng_.next_int(hi - lo + 1);
            ItemId drop_id = ItemRegistry::id_from_name(spec->drop_item);
            if (drop_id != ITEM_AIR && count > 0) {
                ItemStack drop(drop_id, static_cast<uint8_t>(count));
                player_.inventory.add_item_to_main(drop);
            }
        }
    }
    // Single kill-quest hook: melee, arrows and future sources funnel here.
    if (quest::on_kill(player_.quest, m.type)) {
        add_chat_message("Zadanie gotowe do oddania!");
    }
}

const Mob* Game::nearest_alive_mob() const {
    const Mob* best = nullptr;
    float best_d2 = 1e9f;
    for (const auto& m : mobs_) {
        if (!m.alive) continue;
        float dx = m.pos.x - player_.pos.x;
        float dy = m.pos.y - player_.pos.y;
        float dz = m.pos.z - player_.pos.z;
        float d2 = dx * dx + dy * dy + dz * dz;
        if (d2 < best_d2) { best_d2 = d2; best = &m; }
    }
    return best;
}

void Game::mob_attack_player(float damage, const Vec3& source_pos) {
    if (player_.invulnerable || player_.mode == GameMode::Creative ||
        player_.mode == GameMode::Spectator) {
        return;
    }
    // No player armor slots exist yet — raw damage applies.
    player_.health -= damage;

    // Knockback away from the attacker.
    Vec3 dir = player_.pos - source_pos;
    dir.y = 0.0f;
    float len = std::sqrt(dir.x * dir.x + dir.z * dir.z);
    if (len > 0.001f) {
        dir.x /= len; dir.y /= len; dir.z /= len;
        player_.velocity += dir * 0.45f;
        player_.velocity.y += 0.35f;
    }

    if (settings_.particles) {
        Particle p;
        p.pos = player_.pos + Vec3(0, 1.4f, 0);
        p.velocity = Vec3(0, 1.0f, 0);
        p.size = 0.12f;
        p.age = 0;
        p.max_age = 20;
        p.collision = false;
        p.type = ParticleType::BlockDust;
        p.r = 0.8f; p.g = 0.1f; p.b = 0.1f;
        particles_.spawn(p);
    }
}

void Game::handle_player_death() {
    add_chat_message("Gracz zginął!");
    death_visual_ = 1.0f; // death overlay feedback (respawn itself stays instant)
    player_.health = player_.max_health;
    player_.food_level = 20;
    player_.food_saturation = 5.0f;
    player_.food_exhaustion = 0.0f;
    player_.velocity = Vec3(0, 0, 0);
    // Respawn at home position if set, otherwise drop from above spawn.
    player_.pos = player_.home_pos;
    if (player_.pos.y <= static_cast<float>(MIN_Y)) {
        player_.pos = Vec3(0.5f, static_cast<float>(SEA_LEVEL + 12), 0.5f);
    }
    player_.prev_pos = player_.pos;
}

// ---------------------------------------------------------------------------
// Furnaces (block entities)

Game::FurnaceKey Game::furnace_key_from_block(const BlockPos& p) const {
    return {static_cast<int>(current_dimension_), p.x, p.y, p.z};
}

void Game::tick_furnaces() {
    // Only furnaces in loaded chunks keep smelting (matches the mob rule).
    for (auto& [key, f] : furnaces_) {
        auto wit = worlds_.find(static_cast<DimensionId>(key.dim));
        if (wit == worlds_.end() || !wit->second) continue;
        if (!wit->second->has_chunk(chunk_from_block({key.x, key.y, key.z}))) continue;
        smelting::tick_furnace(f);
        // XP is granted when the player collects the output, not here.
    }
}

void Game::purge_invalid_furnaces() {
    for (auto it = furnaces_.begin(); it != furnaces_.end();) {
        auto wit = worlds_.find(static_cast<DimensionId>(it->first.dim));
        if (wit == worlds_.end() || !wit->second) {
            it = furnaces_.erase(it);
            continue;
        }
        World& w = *wit->second;
        if (w.get_block({it->first.x, it->first.y, it->first.z}) != BLOCK_FURNACE) {
            it = furnaces_.erase(it); // block replaced via /setblock etc.
        } else {
            ++it;
        }
    }
}

void Game::tick_projectiles() {
    World& w = *worlds_[current_dimension_];
    for (auto it = projectiles_.begin(); it != projectiles_.end();) {
        Projectile& p = *it;
        Vec3 prev = p.pos; // start-of-tick sample (point-blank hits)
        p.velocity.y -= 0.045f; // gravity per tick
        Vec3 next = prev + p.velocity;

        // Sub-sweep: sample the whole tick's travel every <=0.5 blocks and
        // resolve the FIRST event along the path (wall vs mob vs player) so
        // nothing can be tunneled through and walls don't eat arrows that
        // already reached a target.
        Vec3 seg = next - prev;
        float seg_len = std::sqrt(seg.x * seg.x + seg.y * seg.y + seg.z * seg.z);
        int steps = std::max(1, static_cast<int>(std::ceil(seg_len / 0.5f)));
        auto solid_at = [&](const Vec3& v) {
            return is_solid(w.get_block({static_cast<int>(std::floor(v.x)),
                                         static_cast<int>(std::floor(v.y)),
                                         static_cast<int>(std::floor(v.z))}));
        };
        auto near_player = [&](const Vec3& v) {
            Vec3 c = player_.pos + Vec3(0, 0.9f, 0);
            Vec3 d = v - c;
            return d.x * d.x + d.y * d.y + d.z * d.z < 0.81f; // 0.9 hitbox sphere
        };

        // Preselect the closest mob intersecting the sweep.
        Mob* target = nullptr;
        float best_cd2 = 1e9f;
        if (!p.from_mob) {
            for (auto& m : mobs_) {
                if (!m.alive) continue;
                Vec3 c = m.pos + Vec3(0, 0.9f, 0);
                bool hit = false;
                for (int k = 0; k <= steps; ++k) {
                    Vec3 sample = prev + seg * (static_cast<float>(k) / steps);
                    Vec3 d = sample - c;
                    float dd = d.x * d.x + d.y * d.y + d.z * d.z;
                    // 0.9 radius covers the mob's 0.6x1.8 hitbox even on a
                    // point-blank shot (arrow spawns ~0.75 from its center).
                    if (dd < 0.81f) { hit = true; break; }
                }
                if (!hit) continue;
                float dcx = c.x - prev.x, dcy = c.y - prev.y, dcz = c.z - prev.z;
                float cd = dcx * dcx + dcy * dcy + dcz * dcz;
                if (cd < best_cd2) { best_cd2 = cd; target = &m; }
            }
        }

        bool consumed = false;
        for (int k = 0; k <= steps && !consumed; ++k) {
            Vec3 sample = prev + seg * (static_cast<float>(k) / steps);

            // 1) Mob hit (closest one along the path).
            if (target && !p.from_mob) {
                Vec3 c = target->pos + Vec3(0, 0.9f, 0);
                Vec3 d = sample - c;
                if (d.x * d.x + d.y * d.y + d.z * d.z < 0.81f) {
                    bool was_alive = target->alive;
                    target->apply_damage(p.damage);
                    float kb = calculate_knockback(0.25f, 0.0f, false, target->knockback_resistance);
                    Vec3 knock(p.velocity.x, 0.0f, p.velocity.z);
                    float len = std::sqrt(knock.x * knock.x + knock.z * knock.z);
                    if (len > 0.001f) {
                        knock = Vec3(knock.x / len * kb, 0.35f, knock.z / len * kb);
                        target->velocity = target->velocity + knock;
                    }
                    ++arrow_hits_total_;
                    ai_diag("[arrow] hit mob_type=%d dmg=%.1f hp_after=%.1f\n",
                            static_cast<int>(target->type), p.damage, target->health);
                    if (was_alive && !target->alive) on_mob_killed_by_player(*target);
                    it = projectiles_.erase(it);
                    consumed = true;
                }
            }

            // 2) Player hit (enemy arrows only).
            if (!consumed && p.from_mob && player_.mode != GameMode::Creative &&
                player_.mode != GameMode::Spectator && near_player(sample)) {
                mob_attack_player(3.0f, p.pos);
                it = projectiles_.erase(it);
                consumed = true;
            }

            // 3) Wall: only if no entity was hit at an earlier sample.
            if (!consumed && solid_at(sample)) {
                ai_diag("[arrow] wall despawn at (%.1f, %.1f, %.1f)\n",
                        prev.x, prev.y, prev.z);
                it = projectiles_.erase(it);
                consumed = true;
            }
        }
        if (consumed) {
            continue;
        }
        p.pos = next;
        ++it;
    }
}

// ---------------------------------------------------------------------------
// GLFW callbacks
void Game::key_callback(GLFWwindow* w, int key, int scancode, int action, int mods) {
    (void) scancode;
    (void) mods;
    auto* g = static_cast<Game*>(glfwGetWindowUserPointer(w));
    g->ui_.set_key(key, scancode, action, mods);
    // Record the key state FIRST — the ESC/chat handlers below return early
    // and must not swallow the state the run loop polls (pause toggle).
    if (key >= 0 && key < 350) g->keys_[key] = (action != GLFW_RELEASE);

    if (action == GLFW_PRESS) {
        MC_LOG_TRACE("Key pressed: {}", key);
    }

    if (g->chat_active_) {
        if (action == GLFW_PRESS || action == GLFW_REPEAT) {
            if (key == GLFW_KEY_ESCAPE) {
                g->chat_active_ = false;
            } else if (key == GLFW_KEY_ENTER) {
                if (!g->chat_input_.empty()) {
                    g->add_chat_message("<Player> " + g->chat_input_);
                    g->execute_command(g->chat_input_);
                    g->chat_input_.clear();
                }
                g->chat_active_ = false;
            } else if (key == GLFW_KEY_BACKSPACE) {
                if (!g->chat_input_.empty()) g->chat_input_.pop_back();
            }
        }
        return;
    }

    if (action == GLFW_PRESS && g->state_ == GameState::Playing && (key == GLFW_KEY_K || key == GLFW_KEY_T || key == GLFW_KEY_SLASH)) {
        g->chat_active_ = true;
        g->just_opened_chat_ = true;
        g->chat_input_.clear();
        if (key == GLFW_KEY_SLASH) g->chat_input_ += "/";
        for (int i = 0; i < 350; ++i) g->keys_[i] = false;
        return;
    }

    if (action == GLFW_PRESS || action == GLFW_REPEAT) {
        if (key == GLFW_KEY_ESCAPE) {
            if (g->state_ == GameState::Inventory) {
                g->close_inventory();
                return;
            }
            // ESC in Playing toggles the pause menu (run_game / run_paused
            // handle it); menu screens do their own back navigation.
            // NEVER set the window-should-close flag here — that turned every
            // Escape during gameplay into a silent game exit.
            return;
        }
        if (key == GLFW_KEY_E && action == GLFW_PRESS) {
            if (g->state_ == GameState::Playing) {
                g->state_ = GameState::Inventory;
                g->craft_full_grid_ = false; // plain inventory crafting
                glfwSetInputMode(w, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
            } else if (g->state_ == GameState::Inventory) {
                g->close_inventory();
            }
        }
        if (key == GLFW_KEY_F) g->player_.flying = !g->player_.flying;
        if (key == GLFW_KEY_F3 && action == GLFW_PRESS) g->debug_hud_ = !g->debug_hud_;
        if (key == GLFW_KEY_V) {
            g->wireframe_ = !g->wireframe_;
            g->renderer_.set_wireframe(g->wireframe_);
        }
        if (key >= GLFW_KEY_1 && key <= GLFW_KEY_9) g->player_.inventory.set_selected_hotbar_slot(key - GLFW_KEY_1);
        if (key == GLFW_KEY_F3) g->debug_hud_ = !g->debug_hud_;
        if (key == GLFW_KEY_F2 || key == GLFW_KEY_P) {
            std::string dir = screenshot_dir();
            auto now = std::time(nullptr);
            char buf[64];
            std::strftime(buf, sizeof(buf), "screenshot_%Y%m%d_%H%M%S.bmp", std::gmtime(&now));
            g->take_screenshot(dir + "/" + buf);
        }
    }
}

void Game::char_callback(GLFWwindow* w, unsigned int codepoint) {
    auto* g = static_cast<Game*>(glfwGetWindowUserPointer(w));
    if (g->chat_active_) {
        if (g->just_opened_chat_) {
            g->just_opened_chat_ = false;
            return;
        }
        if (codepoint < 128 && codepoint >= 32) {
            g->chat_input_ += static_cast<char>(codepoint);
        }
        return;
    }
    if (codepoint < 128) {
        g->ui_.set_char(static_cast<char>(codepoint));
    }
}

void Game::cursor_callback(GLFWwindow* w, double x, double y) {
    auto* g = static_cast<Game*>(glfwGetWindowUserPointer(w));
    if (g->state_ != GameState::Playing) return;
    if (g->first_mouse_) {
        g->last_mouse_x_ = x;
        g->last_mouse_y_ = y;
        g->first_mouse_ = false;
        return;
    }
    float dx = static_cast<float>(x - g->last_mouse_x_);
    float dy = static_cast<float>(y - g->last_mouse_y_);
    g->last_mouse_x_ = x;
    g->last_mouse_y_ = y;
    const float sens = 0.0025f * g->settings_.mouse_sensitivity;
    g->player_.yaw += dx * sens;
    g->player_.pitch += dy * sens;
    g->player_.pitch = clampf(g->player_.pitch, -1.553f, 1.553f); // ~89 deg
}

void Game::mouse_callback(GLFWwindow* w, int button, int action, int mods) {
    (void) mods;
    auto* g = static_cast<Game*>(glfwGetWindowUserPointer(w));
    // Releases must always clear held state, even outside Playing (e.g. the
    // inventory screen opened mid-hold); presses only count while playing.
    bool release = (action == GLFW_RELEASE);
    if (!release && (g->state_ != GameState::Playing || g->screenshot_wait_ticks_ >= 0)) return;

    // All world interaction runs once per tick in handle_clicks(); the
    // callbacks only track button state and click edges.
    if (button == GLFW_MOUSE_BUTTON_LEFT) {
        if (action == GLFW_PRESS) {
            g->break_held_ = true;
            g->left_clicked_edge_ = true;
        } else if (release) {
            g->break_held_ = false;
        }
    } else if (button == GLFW_MOUSE_BUTTON_RIGHT) {
        if (action == GLFW_PRESS) {
            g->place_held_ = true;
        } else if (release) {
            g->place_held_ = false;
            g->place_released_ = true;
        }
    }
}

void Game::scroll_callback(GLFWwindow* w, double xoffset, double yoffset) {
    (void) xoffset;
    auto* g = static_cast<Game*>(glfwGetWindowUserPointer(w));
    if (g->state_ != GameState::Playing) return;

    int new_slot = g->player_.inventory.get_selected_hotbar_slot() - static_cast<int>(yoffset);
    if (new_slot < 0) new_slot = 8;
    else if (new_slot > 8) new_slot = 0;

    g->player_.inventory.set_selected_hotbar_slot(new_slot);
}

void Game::resize_callback(GLFWwindow* w, int width, int height) {
    auto* g = static_cast<Game*>(glfwGetWindowUserPointer(w));
    g->camera_.aspect = static_cast<float>(width) / static_cast<float>(height);
    g->ui_.resize(width, height);
    glViewport(0, 0, width, height);
}

void Game::auto_play() {
    in_memory_world_ = true;
    WorldMeta bot_meta;
    bot_meta.name = "bot_runtime";
    bot_meta.display_name = "Bot Runtime";
    bot_meta.seed = 12345;
    bot_meta.game_mode = GameMode::Creative;
    bot_meta.world_size = WorldSize::Small;
    start_game(bot_meta);
}

void Game::request_screenshot(int wait_ticks, std::string output_path) {
    screenshot_wait_ticks_ = wait_ticks;
    screenshot_output_ = std::move(output_path);
    debug_hud_ = true;
    auto_play();
}

std::string Game::screenshot_dir() {
    const char* appdata = getenv("APPDATA");
    std::string base = appdata ? std::string(appdata) + "/.minekampf" : ".minekampf";
    std::string dir = base + "/screenshots";
#ifdef _WIN32
    _mkdir(base.c_str());
    _mkdir(dir.c_str());
#else
    mkdir(base.c_str(), 0755);
    mkdir(dir.c_str(), 0755);
#endif
    return dir;
}

void Game::enable_automation(uint16_t port) {
    automation_.start(port);
}

std::string Game::automation_state_json() const {
    std::ostringstream o;
    o << "{\"status\":\"ok\",\"state\":\"";
    switch (state_) {
        case GameState::MainMenu:    o << "main_menu"; break;
        case GameState::WorldSelect: o << "world_select"; break;
        case GameState::CreateWorld: o << "create_world"; break;
        case GameState::Loading:     o << "loading"; break;
        case GameState::Playing:     o << "playing"; break;
        case GameState::Paused:      o << "paused"; break;
        case GameState::Inventory:   o << "inventory"; break;
    }
    o << "\",\"tick\":" << current_tick_;
    auto it = worlds_.find(current_dimension_);
    if (it != worlds_.end() && it->second) {
        const World& w = *it->second;
        o << ",\"pos\":[" << player_.pos.x << "," << player_.pos.y << "," << player_.pos.z << "]";
        o << ",\"yaw\":" << player_.yaw << ",\"pitch\":" << player_.pitch;
        o << ",\"health\":" << player_.health << ",\"food\":" << player_.food_level;
        o << ",\"flying\":" << (player_.flying ? "true" : "false");
        o << ",\"dimension\":" << static_cast<int>(current_dimension_);
        o << ",\"chunks\":" << w.loaded_count();
        o << ",\"meshes\":" << renderer_.debug_mesh_count();
        o << ",\"mobs\":" << mobs_.size();
        int burning = 0;
        long long out_items = 0;
        for (const auto& [fk, f] : furnaces_) {
            if (f.burning()) ++burning;
            out_items += f.output.count;
        }
        o << ",\"furnaces\":" << furnaces_.size();
        o << ",\"burning\":" << burning;
        o << ",\"smelted\":" << out_items;
        o << ",\"arrows\":" << projectiles_.size();
        o << ",\"arrows_fired\":" << arrows_fired_total_;
        o << ",\"arrow_hits\":" << arrow_hits_total_;
        o << ",\"quest_id\":" << player_.quest.quest_id;
        o << ",\"quest_state\":" << static_cast<int>(player_.quest.state);
        o << ",\"quest_progress\":" << player_.quest.progress;
        const auto& modrep = ModManager::last_report();
        o << ",\"mod_files\":" << modrep.files_scanned;
        o << ",\"mod_added\":" << modrep.recipes_added;
        o << ",\"mod_overridden\":" << modrep.recipes_overridden;
        o << ",\"quests_added\":" << modrep.quests_added;
        o << ",\"quests_overridden\":" << modrep.quests_overridden;
        o << ",\"quest_titles\":[";
        {
            bool first_q = true;
            for (const auto& q : quest::catalog()) {
                if (!first_q) o << ",";
                first_q = false;
                o << "\"" << json_escape(q.title) << "\"";
            }
        }
        o << "]";
        o << ",\"mspt\":" << mspt_ << ",\"time_of_day\":" << time_of_day_;
    }
    o << "}";
    return o.str();
}

std::string Game::automation_perf_json(bool reset) {
    std::ostringstream o;
    const auto& prof = Profiler::get();
    auto& ls = light_stats();
    o << "{\"status\":\"ok\",\"mspt\":" << mspt_;
    o << ",\"tick\":" << current_tick_;
    o << ",\"sections_us\":{";
    bool first = true;
    for (int s = 0; s < static_cast<int>(ProfileSection::Count); ++s) {
        auto sec = static_cast<ProfileSection>(s);
        float us = prof.timing_us(sec);
        if (us <= 0.0f) continue;
        if (!first) o << ",";
        first = false;
        o << "\"" << Profiler::name(sec) << "\":" << us;
    }
    o << "}";
    o << ",\"chunks\":" << (worlds_.count(current_dimension_) ? worlds_[current_dimension_]->loaded_count() : 0);
    o << ",\"dirty\":" << (worlds_.count(current_dimension_) ? worlds_[current_dimension_]->dirty_chunks_.size() : 0);
    o << ",\"gen_in_flight\":" << gen_in_flight_.size();
    o << ",\"mesh_in_flight\":" << mesh_in_flight_.size();
    o << ",\"mobs\":" << mobs_.size();
    o << ",\"particles\":" << particles_.count();
    o << ",\"light_computes\":" << ls.computes.load(std::memory_order_relaxed);
    o << ",\"light_total_ms\":" << (static_cast<double>(ls.total_us.load(std::memory_order_relaxed)) / 1000.0);
    o << "}";
    if (reset) ls.reset();
    return o.str();
}

std::string Game::automation_exec(const std::string& line) {
    if (!worlds_.contains(current_dimension_) || !worlds_.at(current_dimension_)) {
        return "{\"status\":\"error\",\"msg\":\"no world loaded\"}";
    }
    execute_command(line);
    if (!chat_log_.empty()) {
        return "{\"status\":\"ok\",\"msg\":\"" + json_escape(chat_log_.back().text) + "\"}";
    }
    return "{\"status\":\"ok\"}";
}

void Game::automation_look(float yaw_rad, float pitch_rad) {
    player_.yaw = yaw_rad;
    player_.pitch = clampf(pitch_rad, -1.553f, 1.553f);
}

void Game::queue_frame_capture(std::string path) {
    capture_path_ = std::move(path);
    capture_pending_ = true;
}

void Game::take_screenshot(const std::string& path) {
    if (!worlds_.empty()) {
        MC_LOG_INFO("Bot mode: loaded chunks: {}, meshes: {}", worlds_[current_dimension_]->loaded_count(), renderer_.debug_mesh_count());
    }
    renderer_.save_screen_bmp(path);
    MC_LOG_INFO("Screenshot saved: {}", path);
}

void Game::draw_selection_outline() {
    float reach = (player_.mode == GameMode::Creative) ? REACH_CREATIVE : REACH_SURVIVAL;
    Vec3 eye = player_.eye_position();
    Vec3 dir = forward_from_yaw_pitch(player_.yaw, player_.pitch);
    auto hit = voxel_raycast(*worlds_[current_dimension_], eye, dir, reach);
    if (hit.has_value()) {
        renderer_.draw_selection_box(camera_, hit->block_pos);
    }
}

void Game::draw_hud() {
    ui_.begin_frame();
    int sw = ui_.screen_width();
    int sh = ui_.screen_height();
    float cx = sw * 0.5f;
    float cy = sh * 0.5f;

    if (debug_hud_) {
        float fps = (mspt_ > 0.0) ? static_cast<float>(1000.0 / mspt_) : 0.0f;
        char buf[128];
        float dy = 16.0f;
        float y = 10.0f;
        ui_.draw_rect(5.0f, 5.0f, 500.0f, 8.0f * dy + 10.0f, 0, 0, 0, 110); // overlay bg
        ui_.draw_text("Minekampf Debug Overlay (F3)", 10.0f, y, 1.0f, 255, 255, 255); y += dy;
        snprintf(buf, sizeof(buf), "FPS: %d | MSPT: %.2f ms", static_cast<int>(fps), mspt_);
        ui_.draw_text(buf, 10.0f, y, 1.0f, 255, 255, 255); y += dy;
        snprintf(buf, sizeof(buf), "Pos: %.2f, %.2f, %.2f", player_.pos.x, player_.pos.y, player_.pos.z);
        ui_.draw_text(buf, 10.0f, y, 1.0f, 255, 255, 255); y += dy;
        ChunkPos cp = chunk_from_block(BlockPos(static_cast<int>(std::floor(player_.pos.x)), 0, static_cast<int>(std::floor(player_.pos.z))));
        snprintf(buf, sizeof(buf), "ChunkPos: (%d, %d)", cp.x, cp.z);
        ui_.draw_text(buf, 10.0f, y, 1.0f, 255, 255, 255); y += dy;
        snprintf(buf, sizeof(buf), "Yaw: %.1f, Pitch: %.1f", player_.yaw * 57.2958f, player_.pitch * 57.2958f);
        ui_.draw_text(buf, 10.0f, y, 1.0f, 255, 255, 255); y += dy;
        snprintf(buf, sizeof(buf), "Chunks Loaded: %zu (Dirty: %zu)", worlds_[current_dimension_]->loaded_count(), worlds_[current_dimension_]->dirty_chunks_.size());
        ui_.draw_text(buf, 10.0f, y, 1.0f, 255, 255, 255); y += dy;
        snprintf(buf, sizeof(buf), "GPU Meshes: %zu | Active Mobs: %zu", renderer_.debug_mesh_count(), mobs_.size());
        ui_.draw_text(buf, 10.0f, y, 1.0f, 255, 255, 255); y += dy;
        snprintf(buf, sizeof(buf), "Dimension: %d | TimeOfDay: %.2f", static_cast<int>(current_dimension_), time_of_day_);
        ui_.draw_text(buf, 10.0f, y, 1.0f, 255, 255, 255);
    }


    // 1. Status sprites (hearts / hunger) — survival presentation only.
    bool survival_hud = player_.mode == GameMode::Survival;
    if (survival_hud) {
        float heart_y = sh - 68.0f;
        float heart_x_start = cx - 184.0f;
        for (int i = 0; i < 10; ++i) {
            float hx = heart_x_start + i * 17.0f;
            float hp = player_.health - i * 2.0f;
            int cell = hp >= 2.0f ? kSpriteHeartFull : hp >= 1.0f ? kSpriteHeartHalf
                                                                  : kSpriteHeartEmpty;
            draw_ui_sprite(ui_, item_icons_, cell, hx, heart_y, 15);
        }

        float food_y = sh - 68.0f;
        float food_x_start = cx + 22.0f;
        for (int i = 0; i < 10; ++i) {
            float fx = food_x_start + (9 - i) * 17.0f; // MC fills hunger right-to-left
            float fp = player_.food_level - i * 2.0f;
            int cell = fp >= 2.0f ? kSpriteHungerFull : fp >= 1.0f ? kSpriteHungerHalf
                                                                   : kSpriteHungerEmpty;
            draw_ui_sprite(ui_, item_icons_, cell, fx, food_y, 15);
        }
    }

    // Bow draw bar under the crosshair (green at full draw).
    if (bow_charge_ > 0.0f) {
        float frac = mining::bow_charge_capped(bow_charge_);
        float pw = 100.0f, ph = 6.0f;
        ui_.draw_rect(cx - pw * 0.5f - 2, cy + 24, pw + 4, ph + 4, 0, 0, 0, 160);
        uint8_t cr = frac >= 1.0f ? 90 : 235;
        uint8_t cg = frac >= 1.0f ? 230 : 220;
        ui_.draw_rect(cx - pw * 0.5f, cy + 26, pw * frac, ph, cr, cg, 120, 255);
    }

    // Mining progress bar under the crosshair.
    if (mining_valid_ && mining_progress_ > 0.0f && mining_progress_ < 1.0f) {
        float pw = 120.0f, ph = 6.0f;
        ui_.draw_rect(cx - pw * 0.5f - 2, cy + 24, pw + 4, ph + 4, 0, 0, 0, 160);
        ui_.draw_rect(cx - pw * 0.5f, cy + 26, pw * std::clamp(mining_progress_, 0.0f, 1.0f), ph,
                      230, 220, 120, 255);
    }

    // 4. Hotbar UI (9 slots)
    float hb_w = 9 * 40.0f + 8.0f;
    float hb_h = 48.0f;
    float hb_x = cx - hb_w * 0.5f;
    float hb_y = sh - 48.0f;
    ui_.draw_hotbar_bg(hb_x, hb_y, hb_w, hb_h);

    // XP bar squeezed between the stat bars and the hotbar.
    float xp_bar_y = sh - 57.0f;
    ui_.draw_rect(hb_x + 8, xp_bar_y, hb_w - 16, 5, 0, 0, 0, 200);
    ui_.draw_rect(hb_x + 9, xp_bar_y + 1,
                  (hb_w - 18) * std::clamp(player_.xp_progress, 0.0f, 1.0f), 3, ui::kXpR, ui::kXpG,
                  ui::kXpB, 255);
    if (player_.xp_level > 0) {
        ui_.draw_text_centered(std::to_string(player_.xp_level), cx, sh - 74.0f, 1.3f,
                               ui::kXpR, ui::kXpG, ui::kXpB, 255);
    }

    for (int i = 0; i < 9; ++i) {
        float sx = hb_x + 4.0f + i * 40.0f;
        float sy = hb_y + 4.0f;

        // Slot inset (dark, MC-style).
        ui_.draw_rect(sx, sy, 40, 40, 0, 0, 0, 110);

        int sel = player_.inventory.get_selected_hotbar_slot();
        if (i == sel) {
            // Selection frame: 2px white outline around the slot.
            ui_.draw_rect(sx - 2, sy - 2, 44, 2, 255, 255, 255, 230);
            ui_.draw_rect(sx - 2, sy + 40, 44, 2, 255, 255, 255, 230);
            ui_.draw_rect(sx - 2, sy, 2, 40, 255, 255, 255, 230);
            ui_.draw_rect(sx + 40, sy, 2, 40, 255, 255, 255, 230);
        }

        ItemStack stack = player_.inventory.get_slot(i);
        ItemId id = stack.item;
        if (id != ITEM_AIR && stack.count > 0) {
            draw_item_icon(ui_, item_icons_, id, sx + 4, sy + 4, 32);

            if (stack.count > 1) {
                ui_.draw_text(std::to_string(stack.count), sx + 25, sy + 27, 1.0f, 40, 40, 40, 200);
                ui_.draw_text(std::to_string(stack.count), sx + 24, sy + 26, 1.0f, 255, 255, 255, 255);
            }
            draw_durability_bar(ui_, stack, sx + 7, sy + 36, 26);
        }
    }

    int sel = player_.inventory.get_selected_hotbar_slot();
    ItemStack held = player_.inventory.get_slot(sel);
    if (held.item != ITEM_AIR && held.count > 0) {
        std::string name = std::string(item_label(held.item));
        std::replace(name.begin(), name.end(), '_', ' ');
        name[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(name[0])));
        if (held.count > 1) {
            name += " (" + std::to_string(held.count) + ")";
        }
        ui_.draw_text_centered(name, cx, hb_y - 30.0f, 1.2f, 255, 255, 255, 200);
    }

    // Quest tracker (top-right) on a translucent dark backing.
    if (player_.quest.quest_id >= 0) {
        std::string tracker = quest::progress_text(player_.quest);
        if (!tracker.empty()) {
            float tw = ui_.text_width(tracker, 1.1f);
            ui_.draw_rect(sw - tw - 20.0f, 6.0f, tw + 14.0f, 22.0f, 0, 0, 0, 110);
            ui_.draw_text(tracker, static_cast<float>(sw) - tw - 12.0f,
                          12.0f, 1.1f, ui::kGoldR, ui::kGoldG, ui::kGoldB, 240);
        }
    }

    // Draw Chat (each line gets a translucent backing strip, MC-style)
    float chat_y = sh - 100.0f;
    if (chat_active_) {
        ui_.draw_rect(10.0f, sh - 40.0f, sw - 20.0f, 30.0f, 0, 0, 0, 150);
        ui_.draw_text("> " + chat_input_ + "_", 15.0f, sh - 32.0f, 1.2f, 255, 255, 255, 255);
        chat_y = sh - 50.0f;
    }

    for (auto it = chat_log_.rbegin(); it != chat_log_.rend(); ++it) {
        if (it->time_left <= 0.0f && !chat_active_) continue;
        uint8_t alpha = chat_active_ ? 255 : static_cast<uint8_t>(std::max(0.0f, std::min(it->time_left * 255.0f, 255.0f)));
        uint8_t bg_a = static_cast<uint8_t>(alpha * 0.45f);
        chat_y -= 20.0f;
        float line_w = ui_.text_width(it->text, 1.2f);
        ui_.draw_rect(10.0f, chat_y - 3.0f, line_w + 12.0f, 19.0f, 0, 0, 0, bg_a);
        ui_.draw_text(it->text, 15.0f, chat_y, 1.2f, 255, 255, 255, alpha);
    }

    // Draw Player List (TAB)
    if (keys_[GLFW_KEY_TAB]) {
        float tab_w = 260.0f;
        float tab_h = 36.0f;
        float tab_x = cx - tab_w * 0.5f;
        float tab_y = 20.0f;
        ui_.draw_glass_panel(tab_x, tab_y, tab_w, tab_h);
        ui_.draw_text_centered("Player (0ms)", cx, tab_y + 10.0f, 1.2f, 255, 255, 255, 255);
    }

    // Hurt / death vignettes — drawn last so they overlay the whole HUD.
    if (hurt_flash_ > 0.0f || death_visual_ > 0.0f) {
        float strength = std::max(hurt_flash_, death_visual_);
        uint8_t va = static_cast<uint8_t>(std::min(1.0f, strength) * 130.0f);
        // Soft border vignette (two nested edge bands per side).
        float band1 = 26.0f, band2 = 58.0f;
        ui_.draw_rect(0, 0, sw, band1, 190, 0, 0, va);
        ui_.draw_rect(0, sh - band1, sw, band1, 190, 0, 0, va);
        ui_.draw_rect(0, 0, band1, sh, 190, 0, 0, va);
        ui_.draw_rect(sw - band1, 0, band1, sh, 190, 0, 0, va);
        uint8_t va2 = static_cast<uint8_t>(va * 0.5f);
        ui_.draw_rect(0, 0, sw, band2, 150, 0, 0, va2);
        ui_.draw_rect(0, sh - band2, sw, band2, 150, 0, 0, va2);
        ui_.draw_rect(0, 0, band2, sh, 150, 0, 0, va2);
        ui_.draw_rect(sw - band2, 0, band2, sh, 150, 0, 0, va2);

        if (death_visual_ > 0.0f) {
            float pulse = 0.75f + 0.25f * std::sin(death_visual_ * 12.0f);
            uint8_t ta = static_cast<uint8_t>(std::min(1.0f, death_visual_ * 1.6f) * 255.0f * pulse);
            ui_.draw_rect(0, cy - 44.0f, sw, 88.0f, 0, 0, 0, static_cast<uint8_t>(ta * 0.35f));
            ui_.draw_text_centered("Zginąłeś!", cx, cy - 34.0f, 3.0f, 235, 40, 40, ta);
            ui_.draw_text_centered("Odrodzono na punkcie startowym", cx, cy + 18.0f, 1.2f,
                                   230, 230, 230, static_cast<uint8_t>(ta * 0.8f));
        }
    }

    ui_.end_frame();
}// ---------------------------------------------------------------------------
// Crafting logic
//
// craft_grid_ is ALWAYS a 3x3 (row-major, stride 3). The plain-inventory mode
// exposes only its top-left 2x2 subgrid, so RecipeManager sees one canonical
// grid format for both sizes.
Recipe Game::preview_craft() const {
    CraftingGrid grid;
    int n = craft_full_grid_ ? 3 : 2;
    for (int y = 0; y < n; ++y)
        for (int x = 0; x < n; ++x)
            grid.items[y * 3 + x] = craft_grid_[y * 3 + x];
    auto match = RecipeManager::find_matching_recipe(grid);
    return match.value_or(Recipe{});
}

bool Game::take_craft_result() {
    Recipe match = preview_craft();
    if (match.output.is_empty()) return false;

    // Output goes onto the cursor when possible.
    if (!cursor_stack_.is_empty()) {
        if (!cursor_stack_.can_stack_with(match.output)) return false;
        int space = cursor_stack_.max_stack_size() - cursor_stack_.count;
        if (space < match.output.count) return false;
        cursor_stack_.count += match.output.count;
    } else {
        cursor_stack_ = match.output;
    }

    // Consume exactly one item per occupied ingredient slot.
    for (auto& s : craft_grid_) {
        if (!s.is_empty()) {
            s.count -= 1;
            if (s.count == 0) s.item = ITEM_AIR;
        }
    }
    add_chat_message("Crafted " + std::string(item_label(match.output.item)));
    return true;
}

bool Game::ui_shift_held(GLFWwindow* w) const {
    return glfwGetKey(w, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS ||
           glfwGetKey(w, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS;
}

bool Game::stash_into_range(ItemStack& s, int begin, int end) {
    bool moved_any = false;
    // Pass 1: top up matching stacks.
    for (int i = begin; i < end && !s.is_empty(); ++i) {
        ItemStack slot = player_.inventory.get_slot(i);
        if (slot.is_empty() || !slot.can_stack_with(s)) continue;
        int space = slot.max_stack_size() - slot.count;
        int take = std::min(space, static_cast<int>(s.count));
        if (take <= 0) continue;
        slot.count += static_cast<uint8_t>(take);
        s.count -= static_cast<uint8_t>(take);
        if (s.count == 0) s.item = ITEM_AIR;
        player_.inventory.set_slot(i, slot);
        moved_any = true;
    }
    // Pass 2: fill the first empty slot.
    for (int i = begin; i < end && !s.is_empty(); ++i) {
        if (player_.inventory.get_slot(i).is_empty()) {
            player_.inventory.set_slot(i, s);
            s = ItemStack();
            moved_any = true;
            break;
        }
    }
    return moved_any;
}

int Game::craft_all_to_inventory() {
    int crafted = 0;
    while (crafted < 64) {
        if (!take_craft_result()) break; // fills/stays on the cursor
        player_.inventory.add_item_to_main(cursor_stack_);
        if (!cursor_stack_.is_empty()) break; // inventory full: keep remainder
        ++crafted;
    }
    return crafted;
}

void Game::quick_move_inventory_slot(int slot_idx) {
    ItemStack s = player_.inventory.get_slot(slot_idx);
    if (s.is_empty()) return;
    const bool from_hotbar = slot_idx < 9;
    const int begin = from_hotbar ? 9 : 0;
    const int end = from_hotbar ? 36 : 9;
    stash_into_range(s, begin, end);
    player_.inventory.set_slot(slot_idx, s);
}

void Game::close_inventory() {
    // Return crafting-area and cursor items to the inventory; anything that
    // no longer fits is dropped (and honestly reported) rather than duped.
    auto stash = [&](ItemStack& s) {
        if (s.is_empty()) return;
        player_.inventory.add_item_to_main(s);
        if (!s.is_empty()) {
            add_chat_message("Ekwipunek pelny - " + std::to_string(s.count) + " x " +
                             item_label(s.item) + " utracone");
            s = ItemStack();
        }
    };
    for (auto& s : craft_grid_) stash(s);
    stash(cursor_stack_);
    furnace_open_ = false;
    npc_open_ = false;
    enchanting_open_ = false;
    state_ = GameState::Playing;
    glfwSetInputMode(renderer_.window(), GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    first_mouse_ = true;
}

static const char* enchant_label(EnchantType t) {
    switch (t) {
        case EnchantType::Sharpness: return "Ostrosc";
        case EnchantType::Efficiency: return "Efektywnosc";
        case EnchantType::Unbreaking: return "Wytrzymalosc";
        case EnchantType::Protection: return "Ochrona";
    }
    return "?";
}

void Game::open_enchanting_table(const BlockPos& pos) {
    enchanting_pos_ = pos;
    enchanting_open_ = true;
    state_ = GameState::Inventory;
    glfwSetInputMode(renderer_.window(), GLFW_CURSOR, GLFW_CURSOR_NORMAL);
}

int Game::count_nearby_bookshelves(const BlockPos& table) const {
    const World& w = *worlds_.at(current_dimension_);
    int count = 0;
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -2; dx <= 2; ++dx) {
            for (int dz = -2; dz <= 2; ++dz) {
                if (dx == 0 && dz == 0 && dy == 0) continue;
                if (w.get_block({table.x + dx, table.y + dy, table.z + dz}) == BLOCK_BOOKSHELF) {
                    ++count;
                }
            }
        }
    }
    return std::min(count, 15);
}

void Game::draw_enchanting_area(float panel_x, float panel_y, float slot_size) {
    ItemStack held = player_.inventory.get_selected_item();

    ui_.draw_text("Stol enchantingu", panel_x, panel_y - 18.0f, 1.2f, 200, 200, 200);

    std::string shelves_note =
        "Biblioteczki obok: " + std::to_string(count_nearby_bookshelves(enchanting_pos_));
    ui_.draw_text(shelves_note, panel_x, panel_y, 1.0f, 150, 150, 150);

    if (held.is_empty()) {
        ui_.draw_text("Wez przedmiot do reki (hotbar).", panel_x, panel_y + 16.0f, 1.0f, 255, 210, 120);
        return;
    }
    if (!EnchantingSystem::is_enchantable(held.item)) {
        ui_.draw_text("Ten przedmiot nie moze byc zaczarowany.", panel_x, panel_y + 16.0f, 1.0f, 255, 120, 120);
        return;
    }

    // Offers are rolled per redraw for the HELD item (kind follows the item,
    // levels are deterministic per world/table) — switching hotbar slots
    // re-rolls consistently instead of showing mismatched offers.
    {
        const BlockPos& table = enchanting_pos_;
        uint64_t h = static_cast<uint64_t>(static_cast<uint32_t>(table.x)) * 73856093ull ^
                     static_cast<uint64_t>(static_cast<uint32_t>(table.y)) * 19349663ull ^
                     static_cast<uint64_t>(static_cast<uint32_t>(table.z)) * 83492791ull;
        int shelves = count_nearby_bookshelves(table);
        EnchantingSystem::roll_offers(static_cast<uint32_t>(current_world_meta_.seed),
                                      h, shelves, held.item, enchant_offers_);
    }

    EnchantType wanted = EnchantingSystem::enchant_for_item(held.item);
    uint8_t current = held.enchant_level_of(wanted);

    std::string cur_line = std::string("Obecnie: ") + enchant_label(wanted) + " " +
                           std::to_string(static_cast<int>(current));
    ui_.draw_text(cur_line, panel_x, panel_y + 32.0f, 1.0f, 150, 150, 150);

    float btn_w = 300.0f;
    float btn_h = 30.0f;
    for (int i = 0; i < 3; ++i) {
        const EnchantOffer& offer = enchant_offers_[i];
        float y = panel_y + 52.0f + static_cast<float>(i) * (btn_h + 8.0f);

        std::string label = std::string(enchant_label(offer.type)) + " " +
                            std::to_string(static_cast<int>(offer.level)) +
                            "  (" + std::to_string(static_cast<int>(offer.xp_cost)) + " poziomow)";
        bool affordable = EnchantingSystem::can_apply(offer, held, player_.xp_level);
        if (!affordable) {
            ui_.draw_rect(panel_x, y, btn_w, btn_h, 30, 30, 30, 160);
            ui_.draw_text(label, panel_x + 8, y + 8, 1.1f, 120, 120, 120);
            continue;
        }
        if (ui_.button(label, panel_x, y, btn_w, btn_h, 1.1f)) {
            uint8_t slot = player_.inventory.get_selected_hotbar_slot();
            ItemStack enchanted = EnchantingSystem::apply(offer, held);
            survival::spend_xp_levels(player_, offer.xp_cost);
            player_.inventory.set_slot(slot, enchanted);
            add_chat_message(std::string("Zaczarowano: ") + enchant_label(offer.type) + " " +
                             std::to_string(static_cast<int>(offer.level)));
            held = enchanted;
        }
    }
}

void Game::draw_furnace_area(float panel_x, float panel_y, float slot_size) {
    GLFWwindow* w = renderer_.window();
    double mx, my;
    glfwGetCursorPos(w, &mx, &my);
    bool left_btn = glfwGetMouseButton(w, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
    bool right_btn = glfwGetMouseButton(w, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;
    static thread_local bool prev_left = false;
    static thread_local bool prev_right = false;
    bool left_clicked = left_btn && !prev_left;
    bool right_clicked = right_btn && !prev_right;
    prev_left = left_btn;
    prev_right = right_btn;

    auto it = furnaces_.find(open_furnace_);
    if (it == furnaces_.end()) {
        ui_.draw_text("Piec znikl (blok usuniety)", panel_x, panel_y, 1.2f, 255, 120, 120);
        return;
    }
    smelting::FurnaceState& f = it->second;

    ui_.draw_text("Piec", panel_x, panel_y - 18.0f, 1.2f, 200, 200, 200);

    auto move_cursor_into = [&](ItemStack& slot_ref, bool right) {
        ItemStack& cursor = cursor_stack_;
        if (cursor.is_empty()) {
            cursor = slot_ref.split(right ? 1 : slot_ref.count);
        } else if (slot_ref.is_empty()) {
            slot_ref = cursor.split(right ? 1 : cursor.count);
        } else if (slot_ref.can_stack_with(cursor)) {
            uint8_t space = slot_ref.max_stack_size() - slot_ref.count;
            uint8_t take = std::min<uint8_t>(space, right ? 1u : cursor.count);
            slot_ref.count += take;
            cursor.count -= take;
            if (cursor.count == 0) cursor = ItemStack();
        } else {
            std::swap(slot_ref, cursor);
        }
    };

    auto draw_slot = [&](ItemStack& ref, float sx, float sy, bool is_output) {
        ui_.draw_rect(sx, sy, slot_size, slot_size, 40, 40, 40, 200);
        bool hover = mx >= sx && mx <= sx + slot_size && my >= sy && my <= sy + slot_size;
        if (hover) {
            ui_.draw_rect(sx, sy, slot_size, slot_size, 255, 255, 255, 100);
            if (is_output) {
                // Collecting output pays out the accumulated smelting XP —
                // only when at least one item actually moved out.
                if (left_clicked || right_clicked) {
                    if (!f.output.is_empty()) {
                        bool moved = false;
                        if (ui_shift_held(w)) {
                            // Shift-click: straight into the inventory.
                            ItemStack out = f.output;
                            player_.inventory.add_item_to_main(out);
                            if (out.count < f.output.count) moved = true;
                            f.output = out;
                        } else if (cursor_stack_.is_empty()) {
                            cursor_stack_ = f.output;
                            f.output = ItemStack();
                            moved = true;
                        } else if (cursor_stack_.can_stack_with(f.output)) {
                            uint8_t space = cursor_stack_.max_stack_size() - cursor_stack_.count;
                            uint8_t take = std::min<uint8_t>(space, f.output.count);
                            if (take > 0) {
                                cursor_stack_.count += take;
                                f.output.count -= take;
                                if (f.output.count == 0) f.output = ItemStack();
                                moved = true;
                            }
                        }
                        if (moved) {
                            survival::gain_xp(player_, static_cast<int>(f.pending_xp));
                            f.pending_xp = 0.0f;
                        }
                    }
                }
            } else if (left_clicked && ui_shift_held(w)) {
                // Shift-click: stash the slot straight into the inventory.
                ItemStack s = ref;
                ref = ItemStack();
                player_.inventory.add_item_to_main(s);
                if (!s.is_empty()) ref = s; // inventory full: keep remainder
            } else if (left_clicked || right_clicked) {
                move_cursor_into(ref, right_clicked);
            }
        }
        const ItemStack& shown = is_output ? f.output : ref;
        if (!shown.is_empty()) {
            draw_item_icon(ui_, item_icons_, shown.item, sx + 4, sy + 4, slot_size - 8);
            if (shown.count > 1)
                ui_.draw_text(std::to_string(shown.count), sx + slot_size - 15, sy + slot_size - 15,
                              1.0f, 255, 255, 255);
            draw_durability_bar(ui_, shown, sx + 6, sy + slot_size - 9, slot_size - 12);
        }
    };

    float in_x = panel_x + 20.0f;
    float out_x = panel_x + 150.0f;
    float in_y = panel_y;
    float fuel_y = panel_y + slot_size + 12.0f;

    draw_slot(f.input, in_x, in_y, false);

    // Flame indicator: height = remaining burn fraction.
    float flame_x = in_x + slot_size * 0.5f - 6.0f;
    float flame_h = slot_size * 0.7f;
    float frac = (f.burn_total > 0 && f.burning())
                     ? std::clamp(static_cast<float>(f.burn_left) / f.burn_total, 0.0f, 1.0f)
                     : 0.0f;
    ui_.draw_rect(flame_x, fuel_y - flame_h - 10.0f, 12.0f, flame_h, 50, 45, 40, 220);
    ui_.draw_rect(flame_x, fuel_y - 10.0f - flame_h * frac, 12.0f, flame_h * frac, 255, 140, 40, 255);

    draw_slot(f.fuel, in_x, fuel_y, false);

    // Arrow: cook progress fill.
    float arrow_y = panel_y + slot_size * 0.5f;
    float arrow_w = 70.0f;
    ui_.draw_rect(in_x + slot_size + 10.0f, arrow_y, arrow_w, 8.0f, 50, 55, 65, 220);
    float cook_frac = std::clamp(static_cast<float>(f.cook_progress) / smelting::COOK_TICKS, 0.0f, 1.0f);
    ui_.draw_rect(in_x + slot_size + 10.0f, arrow_y, arrow_w * cook_frac, 8.0f, 235, 230, 160, 255);

    draw_slot(f.output, out_x, in_y + 14.0f, true);
    ui_.draw_text("XP za odebranie wytopu", panel_x, fuel_y + slot_size + 10.0f, 1.0f, 150, 160, 180);
}

void Game::draw_crafting_area(float panel_x, float panel_y, float slot_size) {
    GLFWwindow* w = renderer_.window();
    double mx, my;
    glfwGetCursorPos(w, &mx, &my);
    bool left_btn = glfwGetMouseButton(w, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
    bool right_btn = glfwGetMouseButton(w, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;
    static thread_local bool prev_left = false;
    static thread_local bool prev_right = false;
    bool left_clicked = left_btn && !prev_left;
    bool right_clicked = right_btn && !prev_right;
    prev_left = left_btn;
    prev_right = right_btn;

    const char* title = craft_full_grid_ ? "Crafting Table (PPM na stole otwiera)" : "Crafting (2x2)";
    ui_.draw_text(title, panel_x, panel_y - 18.0f, 1.1f, 170, 185, 205);

    auto move_cursor_into = [&](ItemStack& slot_ref, bool right) -> void {
        ItemStack& cursor = cursor_stack_;
        if (cursor.is_empty()) {
            cursor = slot_ref.split(right ? 1 : slot_ref.count);
        } else if (slot_ref.is_empty()) {
            slot_ref = cursor.split(right ? 1 : cursor.count);
        } else if (slot_ref.can_stack_with(cursor)) {
            uint8_t space = slot_ref.max_stack_size() - slot_ref.count;
            uint8_t take = std::min<uint8_t>(space, right ? 1u : cursor.count);
            slot_ref.count += take;
            cursor.count -= take;
            if (cursor.count == 0) cursor = ItemStack();
        } else {
            std::swap(slot_ref, cursor);
        }
    };

    auto draw_slot = [&](ItemStack& ref, float sx, float sy) {
        ui_.draw_rect(sx, sy, slot_size, slot_size, 40, 40, 40, 200);
        bool hover = mx >= sx && mx <= sx + slot_size && my >= sy && my <= sy + slot_size;
        if (!hover) {
            // fall through to icon drawing
        } else {
            ui_.draw_rect(sx, sy, slot_size, slot_size, 255, 255, 255, 100);
            if (left_clicked && ui_shift_held(w)) {
                // Shift-click: send the grid slot back to the inventory.
                ItemStack s = ref;
                ref = ItemStack();
                player_.inventory.add_item_to_main(s);
                if (!s.is_empty()) ref = s; // inventory full: keep remainder
            } else if (left_clicked || right_clicked) {
                move_cursor_into(ref, right_clicked);
            }
        }
        if (!ref.is_empty()) {
            draw_item_icon(ui_, item_icons_, ref.item, sx + 4, sy + 4, slot_size - 8);
            if (ref.count > 1)
                ui_.draw_text(std::to_string(ref.count), sx + slot_size - 15, sy + slot_size - 15,
                              1.0f, 255, 255, 255);
            draw_durability_bar(ui_, ref, sx + 6, sy + slot_size - 9, slot_size - 12);
        }
    };

    int n = craft_full_grid_ ? 3 : 2;
    for (int row = 0; row < n; ++row)
        for (int col = 0; col < n; ++col)
            draw_slot(craft_grid_[row * 3 + col],
                      panel_x + col * (slot_size + 4.0f),
                      panel_y + row * (slot_size + 4.0f));

    // Arrow + highlighted result tile.
    float arrow_x = panel_x + static_cast<float>(n) * (slot_size + 4.0f) + 8.0f;
    float result_y = panel_y + ((static_cast<float>(n) - 1.0f) * (slot_size + 4.0f)) * 0.5f;
    draw_arrow_right(ui_, arrow_x, result_y + slot_size * 0.2f, 26.0f, 16.0f);
    float res_x = arrow_x + 34.0f;
    float res_s = slot_size + 6.0f;
    ui_.draw_rect(res_x, result_y, res_s, res_s, 25, 55, 45, 220);

    Recipe preview = preview_craft();
    bool hover_result = mx >= res_x && mx <= res_x + res_s && my >= result_y && my <= result_y + res_s;
    if (!preview.output.is_empty()) {
        draw_item_icon(ui_, item_icons_, preview.output.item, res_x + 4, result_y + 4, res_s - 8);
        if (preview.output.count > 1)
            ui_.draw_text(std::to_string(preview.output.count),
                          res_x + res_s - 13, result_y + res_s - 13, 1.0f, 255, 255, 255);
        if (hover_result && left_clicked) {
            if (ui_shift_held(w)) {
                int made = craft_all_to_inventory();
                if (made > 0) add_chat_message("Wytworzono x" + std::to_string(made));
            } else {
                take_craft_result();
            }
        }
    }
}

void Game::open_npc_dialog() {
    npc_open_ = true;
    state_ = GameState::Inventory;
    glfwSetInputMode(renderer_.window(), GLFW_CURSOR, GLFW_CURSOR_NORMAL);
}

void Game::draw_npc_dialog(float panel_x, float panel_y, float slot_size) {
    (void)slot_size;
    const quest::QuestDef* def = quest::active_def(player_.quest);
    int cur_id = (def ? def->id : (player_.quest.quest_id >= 0 ? player_.quest.quest_id : 0));

    ui_.draw_text("Wiesniak:", panel_x, panel_y - 18.0f, 1.3f, 255, 220, 140);

    if (def == nullptr) {
        // NotStarted or Completed: offer the first non-completed quest.
        const quest::QuestDef* offer = nullptr;
        for (const auto& q : quest::catalog()) {
            if (!(player_.quest.quest_id == q.id &&
                  player_.quest.state == quest::State::Completed)) {
                offer = &q;
                break;
            }
        }
        if (offer == nullptr) {
            ui_.draw_text("Dziekuje za pomoc! Wroc gdy bedziesz mniejszy.",
                          panel_x, panel_y + 6.0f, 1.2f, 220, 220, 220);
            return;
        }
        cur_id = offer->id;
        // Multiline description
        std::string desc(offer->description);
        float ly = panel_y + 6.0f;
        size_t pos = 0;
        while (pos != std::string::npos) {
            size_t nl = desc.find('\n', pos);
            ui_.draw_text(desc.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos),
                          panel_x, ly, 1.2f, 200, 215, 235);
            ly += 18.0f;
            pos = (nl == std::string::npos) ? std::string::npos : nl + 1;
        }
        if (ui_.button("Przyjmij", panel_x, ly + 10.0f, 150.0f, 32.0f, 1.2f)) {
            if (quest::accept(player_.quest, *offer)) {
                add_chat_message(std::string("Przyjeto zadanie: ") + offer->title);
            }
        }
        return;
    }

    // Active / ReadyToTurnIn
    ui_.draw_text(def->title, panel_x, panel_y + 6.0f, 1.4f, 255, 230, 150);
    ui_.draw_text(quest::progress_text(player_.quest), panel_x, panel_y + 28.0f, 1.2f,
                  200, 215, 235);
    if (player_.quest.state == quest::State::ReadyToTurnIn ||
        (def->goal_type == quest::GoalType::Collect &&
         quest::can_turn_in(player_.quest, *def, player_.inventory))) {
        if (ui_.button("Oddaj zadanie", panel_x, panel_y + 58.0f, 170.0f, 34.0f, 1.2f)) {
            if (quest::turn_in(player_.quest, *def, player_)) {
                add_chat_message("Nagroda odebrana: " + std::to_string(def->xp_reward) + " XP");
            }
        }
    } else {
        ui_.draw_text("Wroc, gdy skonczysz.", panel_x, panel_y + 58.0f, 1.1f, 160, 175, 195);
    }
    (void)cur_id;
}

void Game::draw_inventory_ui() {
    GLFWwindow* w = renderer_.window();
    poll_ui_mouse(*this, ui_, w);

    ui_.begin_frame();
    int sw = ui_.screen_width();
    int sh = ui_.screen_height();

    // Dark transparent overlay
    ui_.draw_rect(0, 0, sw, sh, 0, 0, 0, 150);

    float cx = sw * 0.5f;
    float cy = sh * 0.5f;

    float slot_size = 36.0f;
    float padding = 4.0f;
    float step = slot_size + padding;
    float inv_w = 9 * step;
    float inv_h = 3 * step + step + 16.0f;

    float start_x = cx - inv_w * 0.5f;
    float start_y = cy - inv_h * 0.5f;

    ui_.draw_glass_panel(start_x - 8, start_y - 40, inv_w + 16, inv_h + 56);
    ui_.draw_text("Inventory", start_x, start_y - 34, 1.3f, 200, 200, 200);

    if (npc_open_) draw_npc_dialog(start_x, start_y - 120.0f, slot_size);
    else if (furnace_open_) draw_furnace_area(start_x, start_y - 76.0f, slot_size);
    else if (enchanting_open_) draw_enchanting_area(start_x, start_y - 76.0f, slot_size);
    else draw_crafting_area(start_x, start_y - 110.0f, slot_size);

    double mx, my;
    glfwGetCursorPos(w, &mx, &my);

    auto draw_item = [&](const ItemStack& stack, float x, float y) {
        if (stack.is_empty()) return;
        ItemId id = stack.item;
        draw_item_icon(ui_, item_icons_, id, x + 4, y + 4, slot_size - 8);
        if (stack.count > 1) {
            ui_.draw_text(std::to_string(stack.count), x + slot_size - 14, y + slot_size - 14,
                          1.0f, 40, 40, 40, 200);
            ui_.draw_text(std::to_string(stack.count), x + slot_size - 15, y + slot_size - 15, 1.0f, 255, 255, 255);
        }
        draw_durability_bar(ui_, stack, x + 6, y + slot_size - 9, slot_size - 12);

        // Hover tooltip: item name on a dark plate next to the cursor.
        if (mx >= x && mx <= x + slot_size && my >= y && my <= y + slot_size) {
            std::string name = std::string(item_label(stack.item));
            std::replace(name.begin(), name.end(), '_', ' ');
            if (!name.empty()) {
                name[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(name[0])));
            }
            float tw = ui_.text_width(name, 1.0f);
            float tx = std::min(static_cast<float>(mx) + 14.0f, static_cast<float>(sw) - tw - 16.0f);
            float ty = my - 22.0f;
            ui_.draw_rect(tx - 4, ty - 3, tw + 10, 17, 14, 12, 20, 225);
            ui_.draw_rect(tx - 4, ty - 3, tw + 10, 1, 90, 80, 110, 255);
            ui_.draw_rect(tx - 4, ty + 13, tw + 10, 1, 90, 80, 110, 255);
            ui_.draw_text(name, tx, ty, 1.0f, ui::kGoldR, ui::kGoldG, ui::kGoldB);
        }
    };

    static thread_local bool prev_left = false;
    static thread_local bool prev_right = false;
    bool left_btn = glfwGetMouseButton(w, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
    bool right_btn = glfwGetMouseButton(w, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;
    bool left_clicked = left_btn && !prev_left;
    bool right_clicked = right_btn && !prev_right;
    prev_left = left_btn;
    prev_right = right_btn;

    auto handle_slot = [&](int slot_idx, float sx, float sy) {
        ui_.draw_rect(sx, sy, slot_size, slot_size, 40, 40, 40, 200);
        bool hover = (mx >= sx && mx <= sx + slot_size && my >= sy && my <= sy + slot_size);
        if (hover) {
            ui_.draw_rect(sx, sy, slot_size, slot_size, 255, 255, 255, 100);
            
            ItemStack slot_stack = player_.inventory.get_slot(slot_idx);
            if (left_clicked && ui_shift_held(w)) {
                quick_move_inventory_slot(slot_idx);
            } else if (left_clicked) {
                if (cursor_stack_.is_empty()) {
                    cursor_stack_ = slot_stack;
                    player_.inventory.set_slot(slot_idx, ItemStack());
                } else {
                    if (slot_stack.is_empty()) {
                        player_.inventory.set_slot(slot_idx, cursor_stack_);
                        cursor_stack_ = ItemStack();
                    } else if (slot_stack.can_stack_with(cursor_stack_)) {
                        int space = slot_stack.max_stack_size() - slot_stack.count;
                        int to_add = std::min(space, (int)cursor_stack_.count);
                        slot_stack.count += to_add;
                        cursor_stack_.count -= to_add;
                        if (cursor_stack_.count == 0) cursor_stack_ = ItemStack();
                        player_.inventory.set_slot(slot_idx, slot_stack);
                    } else {
                        // swap
                        ItemStack temp = slot_stack;
                        player_.inventory.set_slot(slot_idx, cursor_stack_);
                        cursor_stack_ = temp;
                    }
                }
            } else if (right_clicked) {
                if (cursor_stack_.is_empty()) {
                    if (!slot_stack.is_empty()) {
                        int half = slot_stack.count / 2;
                        int take = slot_stack.count - half; // take ceil
                        cursor_stack_ = slot_stack;
                        cursor_stack_.count = take;
                        slot_stack.count -= take;
                        if (slot_stack.count == 0) slot_stack = ItemStack();
                        player_.inventory.set_slot(slot_idx, slot_stack);
                    }
                } else {
                    if (slot_stack.is_empty()) {
                        ItemStack one = cursor_stack_;
                        one.count = 1;
                        player_.inventory.set_slot(slot_idx, one);
                        cursor_stack_.count--;
                        if (cursor_stack_.count == 0) cursor_stack_ = ItemStack();
                    } else if (slot_stack.can_stack_with(cursor_stack_) && slot_stack.count < slot_stack.max_stack_size()) {
                        slot_stack.count++;
                        cursor_stack_.count--;
                        if (cursor_stack_.count == 0) cursor_stack_ = ItemStack();
                        player_.inventory.set_slot(slot_idx, slot_stack);
                    }
                }
            }
        }
        draw_item(player_.inventory.get_slot(slot_idx), sx, sy);
    };

    // Main inventory: slots 9-35
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 9; ++col) {
            int slot = 9 + row * 9 + col;
            float sx = start_x + col * step;
            float sy = start_y + row * step;
            handle_slot(slot, sx, sy);
        }
    }

    // Hotbar: slots 0-8
    for (int col = 0; col < 9; ++col) {
        int slot = col;
        float sx = start_x + col * step;
        float sy = start_y + 3 * step + 16.0f; // Gap between main and hotbar
        handle_slot(slot, sx, sy);
    }

    if (!cursor_stack_.is_empty()) {
        draw_item(cursor_stack_, mx - slot_size * 0.5f, my - slot_size * 0.5f);
    }

    ui_.end_frame();
}
void Game::add_chat_message(const std::string& msg) {
    chat_log_.push_back({msg, 10.0f});
    if (chat_log_.size() > 20) {
        chat_log_.erase(chat_log_.begin());
    }
}

void Game::execute_command(const std::string& cmd) {
    if (cmd.empty() || cmd[0] != '/') {
        // Not a command, just a chat message
        return;
    }
    std::string command = cmd.substr(1);
    std::stringstream ss(command);
    std::string arg;
    ss >> arg;

    if (arg == "kill") {
        player_.health = 0.0f;
        add_chat_message("Player killed.");
    } else if (arg == "tp") {
        float x, y, z;
        if (ss >> x >> y >> z) {
            player_.pos = Vec3(x, y, z);
            player_.velocity = Vec3(0, 0, 0);
            add_chat_message("Teleported to " + std::to_string(x) + " " + std::to_string(y) + " " + std::to_string(z));
        } else {
            add_chat_message("Usage: /tp <x> <y> <z>");
        }
    } else if (arg == "sethome") {
        player_.home_pos = player_.pos;
        add_chat_message("Home set to current location.");
    } else if (arg == "home") {
        player_.pos = player_.home_pos;
        player_.velocity = Vec3(0, 0, 0);
        add_chat_message("Teleported home.");
    } else if (arg == "time") {
        float t;
        if (ss >> t) {
            time_of_day_ = t;
            add_chat_message("Time set to " + std::to_string(t));
        } else {
            add_chat_message("Usage: /time <value>");
        }
    } else if (arg == "give") {
        std::string item_name;
        int count = 1;
        if (ss >> item_name) {
            ss >> count;
            if (count < 1) count = 1;
            ItemId iid = item_id_by_name(item_name);
            if (iid != ITEM_AIR) {
                ItemStack stack(iid, static_cast<uint8_t>(std::min(count, 64)));
                player_.inventory.add_item_to_main(stack);
                add_chat_message("Gave " + std::to_string(count) + " x " + item_name);
            } else {
                add_chat_message("Unknown item: " + item_name);
            }
        } else {
            add_chat_message("Usage: /give <item> [count]");
        }
    } else if (arg == "gamemode") {
        std::string m;
        ss >> m;
        if (m == "survival" || m == "s" || m == "0") player_.mode = GameMode::Survival;
        else if (m == "creative" || m == "c" || m == "1") player_.mode = GameMode::Creative;
        else if (m == "hardcore" || m == "h") player_.mode = GameMode::Hardcore;
        else if (m == "spectator" || m == "sp" || m == "3") player_.mode = GameMode::Spectator;
        else { add_chat_message("Usage: /gamemode <survival|creative|hardcore|spectator>"); return; }
        add_chat_message("Game mode set to " + m);
    } else if (arg == "setblock") {
        int x, y, z;
        std::string block_name;
        if (ss >> x >> y >> z >> block_name) {
            BlockId bid = block_id_by_name(block_name);
            if (bid != BLOCK_AIR || block_name == "air") {
                World& w = *worlds_[current_dimension_];
                w.set_block({x, y, z}, bid);
                if (bid == BLOCK_FURNACE) {
                    furnaces_[furnace_key_from_block({x, y, z})];
                }
                add_chat_message("Set " + block_name + " at " + std::to_string(x) + " " + std::to_string(y) + " " + std::to_string(z));
            } else {
                add_chat_message("Unknown block: " + block_name);
            }
        } else {
            add_chat_message("Usage: /setblock <x> <y> <z> <block>");
        }
    } else if (arg == "goto") {
        std::string block_name;
        ss >> block_name;
        BlockId bid = block_id_by_name(block_name);
        if (bid == BLOCK_AIR && block_name != "air") {
            add_chat_message("Unknown block: " + block_name);
            return;
        }
        World& w = *worlds_[current_dimension_];
        int px = static_cast<int>(std::floor(player_.pos.x));
        int pz = static_cast<int>(std::floor(player_.pos.z));
        bool found = false;
        for (int r = 0; r <= 48 && !found; r += 4) {
            for (int dz = -r; dz <= r && !found; dz += 4) {
                for (int dx = -r; dx <= r && !found; dx += 4) {
                    int wx = px + dx, wz = pz + dz;
                    for (int y = MAX_Y - 2; y >= SEA_LEVEL - 12 && y >= MIN_Y; --y) {
                        // Surface blocks only: something walkable/swimmable with air above.
                        if (w.get_block({wx, y, wz}) == bid && w.get_block({wx, y + 1, wz}) == BLOCK_AIR) {
                            player_.pos = Vec3(wx + 0.5f, y + 3.0f, wz + 0.5f);
                            player_.velocity = Vec3(0, 0, 0);
                            add_chat_message("Found " + block_name + " at " + std::to_string(wx) + " " + std::to_string(y) + " " + std::to_string(wz));
                            found = true;
                            break;
                        }
                    }
                }
            }
        }
        if (!found) add_chat_message("No " + block_name + " found nearby (searched 48 blocks).");
    } else if (arg == "fly") {
        player_.flying = !player_.flying;
        add_chat_message(player_.flying ? "Flying enabled" : "Flying disabled");
    } else if (arg == "spawnmob") {
        int n = 1;
        ss >> n;
        if (n < 1) n = 1;
        if (n > 10) n = 10;
        std::string type_name;
        ss >> type_name;
        std::optional<MobType> forced;
        MobCategory spawn_cat = MobCategory::Monster;
        // Registry lookup covers built-ins plus custom Blockbench species.
        if (const MobSpec* spec = MobRegistry::instance().find(type_name)) {
            forced = static_cast<MobType>(spec->id);
            spawn_cat = spec->hostile ? MobCategory::Monster : MobCategory::Creature;
        }
        float ring_min = 6.0f;
        if (float dist; ss >> dist) ring_min = std::clamp(dist, 1.0f, 20.0f);
        World& w = *worlds_[current_dimension_];
        int spawned = 0;
        // Ring 6-12 blocks out; drop from the sky to the first solid ground
        // with headroom (deterministic test hook, mirrors /summon).
        for (int attempt = 0; attempt < n * 24 && spawned < n; ++attempt) {
            float ang = rng_.next_float() * 6.2831853f;
            float dist = ring_min + rng_.next_float() * 3.0f;
            int bx = static_cast<int>(std::floor(player_.pos.x + std::cos(ang) * dist));
            int bz = static_cast<int>(std::floor(player_.pos.z + std::sin(ang) * dist));
            // Reject water columns outright: the "first solid from the sky"
            // here is the riverbed, and spawned mobs drown out of play.
            bool water_column = false;
            for (int y = MAX_Y - 3; y > MIN_Y + 1; --y) {
                BlockId probe = w.get_block({bx, y, bz});
                if (probe == BLOCK_WATER || probe == BLOCK_LAVA ||
                    (probe >= BLOCK_WATER_FLOW_1 && probe <= BLOCK_LAVA_FLOW_3)) {
                    water_column = true;
                    break;
                }
                if (is_solid(probe)) break;
            }
            if (water_column) continue;
            for (int y = MAX_Y - 3; y > MIN_Y + 1; --y) {
                BlockId ground = w.get_block({bx, y, bz});
                // Skip tree canopies: spawning on leaves strands the mob with
                // no walkable neighbors (pathfinding rejects foliage).
                bool canopy = ground == BLOCK_OAK_LEAVES || ground == BLOCK_SPRUCE_LEAVES ||
                              ground == BLOCK_BIRCH_LEAVES;
                if (canopy) continue;
                if (is_solid(ground) && ground != BLOCK_BEDROCK &&
                    w.get_block({bx, y + 1, bz}) == BLOCK_AIR &&
                    w.get_block({bx, y + 2, bz}) == BLOCK_AIR) {
                    mobs_.push_back(mob_spawner_.spawn_forced(
                        spawn_cat, BlockPos(bx, y + 1, bz), rng_, forced));
                    ++spawned;
                    break;
                }
            }
        }
        add_chat_message("Spawned " + std::to_string(spawned) + " mobs");
    } else if (arg == "fillfurnace") {
        // Dev/test hook: load the nearest furnace with fuel + smeltable ore.
        FurnaceKey best{};
        float best_d2 = 64.0f; // 8 block radius
        bool found = false;
        for (const auto& [key, f] : furnaces_) {
            if (key.dim != static_cast<int>(current_dimension_)) continue;
            float dx = key.x + 0.5f - player_.pos.x;
            float dy = key.y + 0.5f - player_.pos.y;
            float dz = key.z + 0.5f - player_.pos.z;
            float d2 = dx * dx + dy * dy + dz * dz;
            if (d2 < best_d2) { best_d2 = d2; best = key; found = true; }
        }
        if (found) {
            auto& f = furnaces_[best];
            f.fuel = ItemStack(ITEM_COAL, 8);
            f.input = ItemStack(static_cast<ItemId>(BLOCK_IRON_ORE), 4);
            add_chat_message("Filled furnace at " + std::to_string(best.x) + " " +
                             std::to_string(best.y) + " " + std::to_string(best.z));
        } else {
            add_chat_message("No furnace within 8 blocks");
        }
    } else if (arg == "recipes") {
        const auto& all = RecipeManager::get_all_recipes();
        std::string summary = std::to_string(all.size()) + " recipes:";
        for (const auto& r : all) {
            summary += " " + std::string(item_label(r.output.item)) + "x" +
                       std::to_string(r.output.count);
        }
        add_chat_message(summary);
    } else if (arg == "killmobs") {
        // Dev/test hook: despawn every mob (clean arena for scenarios).
        size_t n = mobs_.size();
        mobs_.clear();
        projectiles_.clear();
        add_chat_message("Despawned " + std::to_string(n) + " mobs");
    } else if (arg == "quest") {
        std::string sub;
        ss >> sub;
        int id = 0;
        if (!(ss >> id)) id = 0;
        const quest::QuestDef* def = quest::by_id(id);
        if (sub == "accept") {
            if (!def) { add_chat_message("Unknown quest id"); }
            else if (quest::accept(player_.quest, *def)) {
                add_chat_message(std::string("Przyjeto zadanie: ") + def->title);
            } else {
                add_chat_message("Zadanie niedostepne (ukonczone lub w toku)");
            }
        } else if (sub == "status") {
            add_chat_message(quest::progress_text(player_.quest));
        } else if (sub == "turnin") {
            const quest::QuestDef* ad = quest::active_def(player_.quest);
            if (!ad) {
                add_chat_message("Brak aktywnego zadania");
            } else if (quest::turn_in(player_.quest, *ad, player_)) {
                add_chat_message("Nagroda odebrana: " + std::to_string(ad->xp_reward) + " XP");
            } else {
                add_chat_message("Wymagania niespelnione");
            }
        } else {
            add_chat_message("Usage: /quest accept [id] | status | turnin");
        }
    } else if (arg == "tpvillage") {
        StructureGenerator sg(current_world_meta_.seed);
        auto gen_it = generators_.find(DimensionId::Overworld);
        if (gen_it == generators_.end() || !gen_it->second) {
            add_chat_message("No overworld generator");
        } else {
            int pcx = static_cast<int>(std::floor(player_.pos.x / CHUNK_SIZE)) / 32;
            int pcz = static_cast<int>(std::floor(player_.pos.z / CHUNK_SIZE)) / 32;
            for (int r = 0; r <= 4; ++r) {
                bool found = false;
                for (int dx = -r; dx <= r && !found; ++dx) {
                    for (int dz = -r; dz <= r && !found; ++dz) {
                        if (std::max(std::abs(dx), std::abs(dz)) != r) continue;
                        int vcx, vcz;
                        if (!sg.get_village_in_region(pcx + dx, pcz + dz, vcx, vcz)) continue;
                        int vx = vcx * CHUNK_SIZE + 8;
                        int vz = vcz * CHUNK_SIZE + 8;
                        Biome biome;
                        int vy = gen_it->second->terrain_height(vx, vz, biome);
                        if (vy <= SEA_LEVEL || vy >= MAX_Y - 20) continue;
                        player_.pos = Vec3(vx + 0.5f, static_cast<float>(vy + 2), vz + 0.5f);
                        player_.prev_pos = player_.pos;
                        player_.velocity = Vec3(0, 0, 0);
                        add_chat_message("Teleported to village at " + std::to_string(vx) +
                                         " " + std::to_string(vy) + " " + std::to_string(vz));
                        found = true;
                    }
                }
                if (found) break;
                if (r == 4) add_chat_message("No valid village within 4 regions");
            }
        }
    } else if (arg == "probe") {
        int x, y, z;
        if (ss >> x >> y >> z) {
            World& w = *worlds_[current_dimension_];
            BlockId b = w.get_block({x, y, z});
            add_chat_message(std::string("Block at ") + std::to_string(x) + " " +
                             std::to_string(y) + " " + std::to_string(z) + " = " +
                             BLOCK_PROPERTIES_TABLE[b < BLOCK_COUNT ? b : BLOCK_AIR].name.data());
        } else {
            add_chat_message("Usage: /probe <x> <y> <z>");
        }
    } else if (arg == "tpdungeon") {
        // Spiral outward over dungeon regions; the nearest room wins.
        StructureGenerator sg(current_world_meta_.seed);
        int pcx = static_cast<int>(std::floor(player_.pos.x / CHUNK_SIZE)) / 24;
        int pcz = static_cast<int>(std::floor(player_.pos.z / CHUNK_SIZE)) / 24;
        double best_d2 = 1e18;
        int bx = 0, by = 0, bz = 0;
        bool found = false;
        for (int r = 0; r <= 6 && !found; ++r) {
            for (int dx = -r; dx <= r; ++dx) {
                for (int dz = -r; dz <= r; ++dz) {
                    if (std::max(std::abs(dx), std::abs(dz)) != r) continue;
                    int dcx, dcz, dfy;
                    if (!sg.get_dungeon_in_region(pcx + dx, pcz + dz, dcx, dcz, dfy)) continue;
                    double wx = dcx * CHUNK_SIZE + 8.5;
                    double wz = dcz * CHUNK_SIZE + 8.5;
                    double ddx = wx - player_.pos.x;
                    double ddz = wz - player_.pos.z;
                    double d2 = ddx * ddx + ddz * ddz;
                    if (d2 < best_d2) {
                        best_d2 = d2;
                        bx = dcx * CHUNK_SIZE + 8;
                        by = dfy;
                        bz = dcz * CHUNK_SIZE + 8;
                        found = true;
                    }
                }
            }
        }
        if (found) {
            player_.pos = Vec3(bx + 0.5f, static_cast<float>(by), bz + 0.5f);
            player_.prev_pos = player_.pos;
            player_.velocity = Vec3(0, 0, 0);
            add_chat_message("Teleported to dungeon at " + std::to_string(bx) + " " +
                             std::to_string(by) + " " + std::to_string(bz));
        } else {
            add_chat_message("No dungeon found within 6 regions");
        }
    } else if (arg == "select") {
        std::string name;
        ss >> name;
        ItemId id = item_id_by_name(name);
        if (id == ITEM_AIR) {
            add_chat_message("Unknown item: " + name);
        } else {
            bool found = false;
            for (int i = 0; i < 9; ++i) {
                if (player_.inventory.get_slot(i).item == id) {
                    player_.inventory.set_selected_hotbar_slot(static_cast<uint8_t>(i));
                    found = true;
                    break;
                }
            }
            // Main inventory fallback: swap into the selected hotbar slot.
            if (!found) {
                for (int i = 9; i < 36; ++i) {
                    if (player_.inventory.get_slot(i).item == id) {
                        uint8_t sel = player_.inventory.get_selected_hotbar_slot();
                        ItemStack held = player_.inventory.get_slot(sel);
                        ItemStack from = player_.inventory.get_slot(i);
                        player_.inventory.set_slot(sel, from);
                        player_.inventory.set_slot(i, held);
                        found = true;
                        break;
                    }
                }
            }
            add_chat_message(found ? ("Selected " + name) : ("Not in inventory: " + name));
        }
    } else if (arg == "aimnearest") {
        std::string filter;
        ss >> filter;
        std::optional<MobType> want;
        if (filter == "zombie") want = MobType::Zombie;
        else if (filter == "skeleton") want = MobType::Skeleton;
        else if (filter == "cow") want = MobType::Cow;
        else if (filter == "pig") want = MobType::Pig;
        const Mob* m = nullptr;
        {
            float best_d2 = 1e9f;
            for (const auto& mob : mobs_) {
                if (!mob.alive) continue;
                if (want && mob.type != *want) continue;
                float dx = mob.pos.x - player_.pos.x;
                float dy = mob.pos.y - player_.pos.y;
                float dz = mob.pos.z - player_.pos.z;
                float d2 = dx * dx + dy * dy + dz * dz;
                if (d2 < best_d2) { best_d2 = d2; m = &mob; }
            }
        }
        if (!m) {
            add_chat_message("No mobs nearby");
        } else {
            Vec3 eye = player_.eye_position();
            Vec3 target = m->pos + Vec3(0, 0.9f, 0);
            Vec3 d = target - eye;
            float len = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
            // Gravity lead: aim above the target by the predicted arrow drop
            // (full-draw speed) so distant shots converge on the hitbox.
            float dist = (len > 0.001f) ? len : 0.001f;
            target.y += mining::arrow_drop(dist, mining::bow_speed(1.0f));
            d = target - eye;
            player_.yaw = std::atan2(-d.x, d.z);
            float len2 = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
            player_.pitch = (len2 > 0.001f) ? -std::asin(d.y / len2) : 0.0f;
            add_chat_message("Aimed at mob at distance " +
                             std::to_string(std::sqrt(d.x * d.x + d.z * d.z)));
        }
    } else if (arg == "shoot") {
        float charge = 1.0f;
        ss >> charge;
        if (charge < 0.1f) charge = 0.1f;
        if (charge > 1.0f) charge = 1.0f;
        ItemStack held = player_.inventory.get_selected_item();
        if (!mining::is_bow(held.item)) {
            add_chat_message("Hold a bow first (/give bow)");
        } else {
            fire_player_arrow(charge);
            add_chat_message("Fired arrow (charge=" + std::to_string(charge) + ")");
        }
    } else if (arg == "help") {
        add_chat_message("Commands: /tp /time /give /gamemode /setblock /goto /fly /sethome /home /kill /spawnmob /fillfurnace /aimnearest /shoot /select /probe /tpdungeon /quest /tpvillage /killmobs /recipes /quality");
    } else if (arg == "quality") {
        std::string preset;
        if (ss >> preset) {
            if (preset == "low") {
                renderer_.set_quality(Renderer::QualityPreset::Low);
                add_chat_message("Quality preset: LOW (2048 shadows @ 72m, POM off, SSR off, clouds off)");
            } else if (preset == "medium" || preset == "med") {
                renderer_.set_quality(Renderer::QualityPreset::Medium);
                add_chat_message("Quality preset: MEDIUM (2048 shadows @ 96m, POM 12m, SSR on, soft shadows)");
            } else if (preset == "high") {
                renderer_.set_quality(Renderer::QualityPreset::High);
                add_chat_message("Quality preset: HIGH (4096 shadows @ 128m, POM 20m, SSR on, soft shadows)");
            } else {
                add_chat_message("Unknown preset: " + preset + " (low / medium / high)");
            }
        } else {
            const char* q = renderer_.quality() == Renderer::QualityPreset::Low ? "low"
                          : renderer_.quality() == Renderer::QualityPreset::Medium ? "medium" : "high";
            add_chat_message(std::string("Quality preset: ") + q + " (usage: /quality low|medium|high)");
        }
    } else {
        add_chat_message("Unknown command: " + arg);
    }
}

} // namespace mc
