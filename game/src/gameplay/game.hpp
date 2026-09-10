#pragma once

#include <chrono>
#include <map>
#include <string>
#include <tuple>
#include <unordered_set>
#include <vector>

#include "core/game_loop.hpp"
#include "core/random.hpp"
#include "core/settings.hpp"
#include "core/thread_pool.hpp"
#include "core/automation_server.hpp"
#include "audio/audio_engine.hpp"
#include "audio/sound_events.hpp"
#include "gameplay/player.hpp"
#include "gameplay/crafting.hpp"
#include "gameplay/mining.hpp"
#include "gameplay/smelting.hpp"
#include "gameplay/entity.hpp"
#include "gameplay/remote_player.hpp"
#include "gameplay/tick_system.hpp"
#include "generation/world_generator.hpp"
#include "gameplay/entity.hpp"
#include "renderer/camera.hpp"
#include "renderer/renderer.hpp"
#include "renderer/particle_system.hpp"
#include "renderer/item_icons.hpp"
#include "renderer/ui.hpp"
#include "world/block.hpp"
#include "world/world.hpp"
#include "save/level_storage.hpp"
#include "save/world_manager.hpp"
#include "gameplay/weather.hpp"
#include "gameplay/spawning.hpp"
#include "gameplay/redstone.hpp"
#include "gameplay/enchanting.hpp"

namespace mc {

class ServerSession;
class ClientSession;

enum class GameState {
    MainMenu,
    WorldSelect,
    CreateWorld,
    MultiplayerMenu,
    MultiplayerConnect,
    Loading,
    Playing,
    Paused,
    Inventory,
};

enum class PauseAction {
    None,
    Continue,
    Save,
    SaveAndQuit,
};

struct GenResult {
    DimensionId dim;
    ChunkPos pos;
    std::shared_ptr<Chunk> chunk;
};

// Top-level game application (PHASE1 §1, PHASE5). Orchestrates the world,
// generator, renderer, thread pool, player, and tick system. Runs the
// dual-loop: fixed 20Hz server ticks + variable-rate rendering.
class Game {
public:
    Game();
    ~Game();
    bool init();
    void shutdown();
    void run();

    // Bot / screenshot modes: run for `wait_ticks`, take screenshot, exit.
    void request_screenshot(int wait_ticks, std::string output_path);
    // Auto-start an in-memory world (for automated testing); keeps running.
    void auto_play();
    static std::string screenshot_dir();

    // Automation API (in-engine TCP server for AI testers/scripts).
    void enable_automation(uint16_t port);
    [[nodiscard]] std::string automation_state_json() const;
    [[nodiscard]] std::string automation_perf_json(bool reset);
    [[nodiscard]] std::string automation_exec(const std::string& line);
    void automation_look(float yaw_rad, float pitch_rad);
    void automation_open_settings() { settings_open_ = true; }
    // Capture the current frame to `path` before it is presented.
    void queue_frame_capture(std::string path);

    // World management flow.
    bool start_game(const WorldMeta& meta);
    void return_to_menu();

    // -- Multiplayer (listen server on the host, remote world on a client;
    //    implementation lives in network/server_session.{hpp,cpp} and
    //    network/client_session.{hpp,cpp}, which call this narrow surface) --
    bool mp_host_start(uint16_t port);
    void mp_host_stop();
    // CLI/menu entry: arm hosting so the server comes up with the next
    // world load (start_game), never serving the menu panorama world.
    void mp_host_after_load(uint16_t port) { hosting_intent_ = true; mp_host_port_ = port; }
    bool mp_client_join(const std::string& host, uint16_t port, const std::string& username);
    void mp_client_leave();
    [[nodiscard]] bool is_multiplayer_host() const { return server_session_ != nullptr; }
    [[nodiscard]] bool is_multiplayer_client() const { return client_session_ != nullptr; }
    [[nodiscard]] bool is_multiplayer() const { return server_session_ || client_session_; }

    // Session → Game surface (main thread only).
    World* mp_world();
    [[nodiscard]] const Player& mp_host_player() const { return player_; }
    [[nodiscard]] float mp_time_of_day() const { return time_of_day_; }
    void mp_set_time_of_day(float t) { time_of_day_ = t; }
    [[nodiscard]] GameMode mp_game_mode() const { return player_.mode; }
    bool mp_apply_remote_break(const BlockPos& pos, const Vec3& player_pos, std::string* err);
    bool mp_apply_remote_place(const BlockPos& pos, BlockId block, const Vec3& player_pos,
                               std::string* err);
    void mp_chat_from_remote(const std::string& from, const std::string& text);
    void mp_client_disconnected(const std::string& reason);
    // Client-side packet applications (client_session.cpp).
    void mp_client_accepted(const net::LoginAcceptedPacket& acc);
    void mp_client_chunk(const net::ChunkDataPacket& pkt);
    void mp_client_blocks(const net::BlockUpdatesPacket& pkt);
    void mp_client_spawn_player(const net::SpawnPlayerPacket& pkt);
    void mp_client_despawn_player(const net::DespawnPlayerPacket& pkt);
    void mp_client_states(const net::PlayerStatesPacket& pkt);
    // Chat line submitted locally (chat UI / automation): routes to the
    // network session when connected, otherwise the single-player path.
    void submit_chat_line(std::string text);

private:
    void process_input(float dt);
    void tick();          // one server tick (20 Hz)
    void render(float alpha, bool present_after = true);
    void finish_frame();  // deferred frame capture + present
    void generate_initial_chunks();
    void update_chunks();
    void drain_gen_results();
    void build_dirty_meshes(int budget);
    void unload_distant_chunks();
    void handle_clicks();
    void take_screenshot(const std::string& path);

    // -- State machine --
    void run_loading();        // tight loop: drain chunks, show progress
    void run_game();           // the normal game loop
    void run_paused();         // pause menu

    // Settings (ESC menu -> Opcje; also from the main menu).
    void apply_settings();
    void draw_settings_panel();
    Settings settings_;
    bool settings_open_ = false;
    int settings_tab_ = 0; // 0=Grafika 1=Gra 2=Dźwięki
    std::unique_ptr<AudioEngine> audio_;
    SoundEvents sfx_;
    float last_step_dist_ = 0.0f;
    bool prev_on_ground_ = false;
    float fps_ema_ = 0.0f;      // smoothed frames-per-second (perf tooling)
    float frame_ms_ema_ = 0.0f; // smoothed frame time in ms
    float frame_ms_max_ = 0.0f; // worst frame in the last ~1 s (jitter metric)
    int frame_count_ = 0;
    std::chrono::steady_clock::time_point last_frame_start_ =
        std::chrono::steady_clock::now();

    // Menu backdrop: a small in-memory world rendered as a rotating panorama.
    void prepare_menu_world();
    void drain_menu_chunks();
    bool menu_world_active_ = false;

    void draw_main_menu();
    void draw_world_select();
    void draw_create_world();
    void draw_pause_menu();
    void draw_menu_background();


    // -- Game world lifecycle --
    void shutdown_world();
    void enter_dimension(DimensionId dim);

    // GLFW callbacks (bridge to instance via window user pointer).
    static void key_callback(GLFWwindow* w, int key, int scancode, int action, int mods);
    static void char_callback(GLFWwindow* w, unsigned int codepoint);
    static void cursor_callback(GLFWwindow* w, double x, double y);
    static void mouse_callback(GLFWwindow* w, int button, int action, int mods);
    static void scroll_callback(GLFWwindow* w, double xoffset, double yoffset);
    static void resize_callback(GLFWwindow* w, int width, int height);

    void draw_hud();
    void draw_selection_outline();
    void draw_inventory_ui();

    Renderer renderer_;
    UIRenderer ui_;
    ItemIcons item_icons_;
    AutomationServer automation_;
    GameState state_ = GameState::MainMenu;

    // Selected world meta for the active game session.
    WorldMeta current_world_meta_;
    std::vector<WorldMeta> world_list_;
    int world_sel_index_ = 0;
    int world_delete_index_ = -1; // -1 = no delete modal

    // CreateWorld screen state
    std::string new_world_name_;
    std::string new_world_seed_;
    GameMode new_world_mode_ = GameMode::Survival;
    WorldSize new_world_size_ = WorldSize::Small;
    bool name_input_active_ = false;
    bool seed_input_active_ = false;

    // Loading state
    int loading_total_chunks_ = 0;
    int loading_done_chunks_ = 0;

    // Pause state
    PauseAction pause_action_ = PauseAction::None;

    std::unordered_map<DimensionId, std::unique_ptr<World>> worlds_;
    std::unordered_map<DimensionId, std::unique_ptr<WorldGenerator>> generators_;
    DimensionId current_dimension_ = DimensionId::Overworld;
    std::unique_ptr<LevelStorage> storage_;
    ThreadPool pool_;
    Player player_;
    Camera camera_;
    TickSystem ticks_;
    ParticleSystem particles_;
    WeatherSystem weather_;
    MobSpawner mob_spawner_;
    RedstoneSystem redstone_;
    std::vector<Mob> mobs_;
    Rng rng_{0x12345678ULL};
    PlayerInput last_input_{};

    Channel<GenResult> gen_channel_;
    Channel<std::pair<ChunkPos, ChunkMeshData>> mesh_channel_;
    Channel<ChunkMeshData> recycled_meshes_;

    struct ChunkRequestHash {
        std::size_t operator()(const std::pair<DimensionId, ChunkPos>& p) const {
            return std::hash<int>()(static_cast<int>(p.first)) ^ 
                   (std::hash<int>()(p.second.x) << 1) ^ 
                   (std::hash<int>()(p.second.z) << 2);
        }
    };
    std::unordered_set<std::pair<DimensionId, ChunkPos>, ChunkRequestHash> gen_in_flight_;
    // Key -> game tick when the build was submitted. A healthy build finishes
    // in well under a second; if a guard outlives that (e.g. its worker result
    // was lost in a state transition), it must not wedge the chunk's remeshing
    // forever — build_dirty_meshes force-expires stale guards.
    std::unordered_map<std::pair<DimensionId, ChunkPos>, long long, ChunkRequestHash> mesh_in_flight_;
    // Reused per-frame scratch for build_dirty_meshes (no realloc + no sort
    // when nothing is dirty).
    std::vector<std::pair<int, ChunkPos>> dirty_scratch_;
    std::vector<ChunkPos> stale_scratch_;
    BlockId hotbar_[10] = {BLOCK_STONE, BLOCK_GRASS, BLOCK_DIRT, BLOCK_NETHERRACK, BLOCK_OBSIDIAN,
                           BLOCK_OAK_LOG, BLOCK_GLASS, BLOCK_GLOWSTONE, BLOCK_REDSTONE_WIRE, BLOCK_REDSTONE_TORCH};
    int hotbar_sel_ = 0;
    
    ItemStack cursor_stack_;

    int64_t current_tick_ = 0;
    int render_distance_ = DEFAULT_RENDER_DISTANCE;
    double mspt_ = 0.0;

    bool keys_[350]{};
    bool break_held_ = false;
    bool place_held_ = false;
    float break_cooldown_ = 0.0f;
    float place_cooldown_ = 0.0f;
    float portal_cooldown_ = 0.0f;

    double last_mouse_x_ = 0.0;
    double last_mouse_y_ = 0.0;
    bool first_mouse_ = true;

    float time_of_day_ = 0.25f; // 0..1 (0 = midnight, 0.5 = noon)
    bool wireframe_ = false;
    bool debug_hud_ = false;
    bool running_ = false;
    bool is_new_world_ = false;

    // Screenshot / bot mode
    int screenshot_wait_ticks_ = -1;
    std::string screenshot_output_;
    bool screenshot_ready_ = false;
    int screenshot_grace_frames_ = 0;
    bool in_memory_world_ = false; // bot runs in a throwaway world (no saves)

    // Deferred per-frame capture (automation API)
    bool capture_pending_ = false;
    std::string capture_path_;

    // Chat system
    struct ChatMessage {
        std::string text;
        float time_left = 10.0f;
    };
    std::vector<ChatMessage> chat_log_;
    std::string chat_input_;
    bool chat_active_ = false;
    bool just_opened_chat_ = false;

    // -- Multiplayer state (see network/ sessions) --
    std::unique_ptr<ServerSession> server_session_;
    std::unique_ptr<ClientSession> client_session_;
    std::vector<RemotePlayer> remote_players_; // everyone but the local player
    int32_t my_player_id_ = 0;                 // 0 = host; client id from login
    bool hosting_intent_ = false;              // WorldSelect → host after load
    uint16_t mp_host_port_ = net::MP_DEFAULT_PORT;
    ChunkPos last_requested_center_{INT32_MIN, INT32_MIN};
    std::vector<Mob> mob_render_list_;         // mobs_ + remote players (render scratch)
    // Join screen fields
    std::string mp_join_host_;
    std::string mp_join_port_ = "25590";
    std::string mp_join_name_;
    int mp_join_active_field_ = 0; // 0=host 1=port 2=nick
    bool mp_return_to_menu_pending_ = false; // deferred world teardown (mid-tick disconnects)

    void draw_multiplayer_menu();
    void draw_multiplayer_connect();
    void tick_multiplayer_host();
    void tick_multiplayer_client();
    void refresh_remote_players_host();
    [[nodiscard]] const Vec3* mp_nearest_player_pos(const Vec3& from) const;

    // -- Survival mining state (progressive block breaking) --
    BlockPos mining_pos_{0, 0, 0};
    bool mining_valid_ = false;      // progress belongs to mining_pos_
    float mining_progress_ = 0.0f;   // 0..1 within current target
    float attack_timer_ = 0.0f;      // weapon swing recovery seconds
    bool left_clicked_edge_ = false; // one-shot attack trigger
    bool last_jump_ = false;         // for hunger jump-exhaustion edges

    bool esc_released_in_pause_ = false; // ESC toggle: require key release first
    double esc_toggle_time_ = 0.0;       // shared debounce for pause open/close

    // -- HUD visual feedback (purely cosmetic, no gameplay effect) --
    float hurt_flash_ = 0.0f;        // red vignette after taking damage, 1..0
    float death_visual_ = 0.0f;      // death overlay timer after a lethal hit
    float prev_health_visual_ = 20.0f;

    // Mob-vs-player combat
    void mob_attack_player(float damage, const Vec3& source_pos);
    void handle_player_death();

    // -- Crafting --
    ItemStack craft_grid_[9];
    bool craft_full_grid_ = false; // true = 3x3 table, false = 2x2 inventory

    [[nodiscard]] mc::Recipe preview_craft() const; // empty output == no match
    bool take_craft_result();                       // consume + give to cursor
    // Shift-click helpers: craft-all into the inventory; stash a stack into
    // the slot range [begin, end); quick-move inside the inventory screen.
    int craft_all_to_inventory();
    bool stash_into_range(ItemStack& s, int begin, int end);
    void quick_move_inventory_slot(int slot_idx);
    bool ui_shift_held(GLFWwindow* w) const;
    void close_inventory();                         // return grid+cursor items
    void draw_crafting_area(float panel_x, float panel_y, float slot_size);

    // -- Furnaces (block entities, persist via furnaces.dat) --
    struct FurnaceKey {
        int dim = 0;
        int x = 0, y = 0, z = 0;
        bool operator<(const FurnaceKey& o) const {
            return std::tie(dim, x, y, z) < std::tie(o.dim, o.x, o.y, o.z);
        }
    };
    std::map<FurnaceKey, smelting::FurnaceState> furnaces_;
    bool furnace_open_ = false;
    FurnaceKey open_furnace_{};

    void tick_furnaces();
    void purge_invalid_furnaces();
    void draw_furnace_area(float panel_x, float panel_y, float slot_size);
    FurnaceKey furnace_key_from_block(const BlockPos& p) const;

    // -- Projectiles (skeleton arrows + player bow) --
    std::vector<Projectile> projectiles_;
    uint32_t arrows_fired_total_ = 0; // lifetime counter for test invariants
    uint32_t arrow_hits_total_ = 0;   // player arrows that hit a mob
    void tick_projectiles();

    // -- Quest NPC dialog --
    bool npc_open_ = false;
    void draw_npc_dialog(float panel_x, float panel_y, float slot_size);
    void open_npc_dialog();

    // -- Enchanting table (block entity-less: offers rolled per table pos) --
    bool enchanting_open_ = false;
    BlockPos enchanting_pos_{0, 0, 0};
    EnchantOffer enchant_offers_[3];
    void open_enchanting_table(const BlockPos& pos);
    int count_nearby_bookshelves(const BlockPos& table) const;
    void draw_enchanting_area(float panel_x, float panel_y, float slot_size);

    // -- Player bow --
    bool bow_charging_ = false;
    float bow_charge_ = 0.0f;   // seconds of held right button
    bool place_released_ = false;
    bool consume_arrow_for_shot_ready(); // any arrow in main inventory?
    void fire_player_arrow(float charge);
    void on_mob_killed_by_player(const Mob& m);
    const Mob* nearest_alive_mob() const;

    void add_chat_message(const std::string& msg);
    void execute_command(const std::string& cmd);
};

} // namespace mc
