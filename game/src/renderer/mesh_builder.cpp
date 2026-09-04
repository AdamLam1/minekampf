#include "renderer/mesh_builder.hpp"

#include <array>

#include "core/config.hpp"
#include "core/profiler.hpp"
#include "world/block.hpp"

namespace mc {

namespace {
// Per-face geometry: 4 corner offsets (CCW from outside) and matching (cu,cv).
struct FaceCorner {
    int8_t dx, dy, dz;
    float u, v; // (cu, cv) in [0,1]
};
struct FaceDef {
    Direction dir;
    BlockPos normal;        // face normal offset
    BlockPos u_unit;        // in-plane u positive direction
    BlockPos v_unit;        // in-plane v positive direction
    FaceCorner corners[4];
};

// clang-format off
constexpr std::array<FaceDef, 6> FACES = {{
    // Down (-Y)
    {Direction::Down, {0,-1,0}, {1,0,0}, {0,0,1},
        {{0,0,0, 0,0}, {1,0,0, 1,0}, {1,0,1, 1,1}, {0,0,1, 0,1}}},
    // Up (+Y)
    {Direction::Up, {0,1,0}, {1,0,0}, {0,0,1},
        {{0,1,0, 0,0}, {0,1,1, 0,1}, {1,1,1, 1,1}, {1,1,0, 1,0}}},
    // North (-Z)
    {Direction::North, {0,0,-1}, {1,0,0}, {0,1,0},
        {{0,0,0, 0,1}, {0,1,0, 0,0}, {1,1,0, 1,0}, {1,0,0, 1,1}}},
    // South (+Z)
    {Direction::South, {0,0,1}, {1,0,0}, {0,1,0},
        {{0,0,1, 0,1}, {1,0,1, 1,1}, {1,1,1, 1,0}, {0,1,1, 0,0}}},
    // West (-X)
    {Direction::West, {-1,0,0}, {0,0,1}, {0,1,0},
        {{0,0,0, 0,1}, {0,0,1, 1,1}, {0,1,1, 1,0}, {0,1,0, 0,0}}},
    // East (+X)
    {Direction::East, {1,0,0}, {0,0,1}, {0,1,0},
        {{1,0,0, 0,1}, {1,1,0, 0,0}, {1,1,1, 1,0}, {1,0,1, 1,1}}},
}};
// clang-format on

[[nodiscard]] bool should_render(BlockId b, BlockId n) {
    if (b == BLOCK_AIR) return false;
    if (is_opaque(n)) return false;
    // Same fluid block id = same surface level: cull the interior face.
    // DIFFERENT fluid levels (BLOCK_WATER next to water_flow_N) keep their
    // connecting face — otherwise level steps leave see-through gaps where
    // the whole water body disappears from above.
    if (is_water(b) && is_water(n) && b == n) return false;
    if (is_lava(b) && is_lava(n) && b == n) return false;

    // Prevent culling adjacent leaves to make trees look dense (Fancy mode)
    if (n == b && tile_for_face(b, Direction::Up) == Tile::Leaves) return true;

    if (n == b && (is_transparent(b) || is_liquid(b))) return false;
    return true;
}

// Fast block lookup cached in a 3x3 array of chunk pointers.
// Eliminates hash-map lookup overhead (Golden Rule #4, Mojang systems programming).
[[nodiscard]] inline BlockId get_block_cached(const std::array<std::shared_ptr<Chunk>, 9>& cached_chunks, ChunkPos pos, BlockPos bp) {
    if (bp.y < MIN_Y || bp.y >= MAX_Y) return BLOCK_AIR;
    int cx = bp.x >> 4;
    int cz = bp.z >> 4;
    int rdx = cx - pos.x;
    int rdz = cz - pos.z;
    if (rdx >= -1 && rdx <= 1 && rdz >= -1 && rdz <= 1) {
        const Chunk* c = cached_chunks[(rdz + 1) * 3 + (rdx + 1)].get();
        if (c) {
            int lx = bp.x & 15;
            int lz = bp.z & 15;
            int sy = bp.y >> 4;
            return c->sections[sy].get(lx, bp.y & 15, lz);
        }
    }
    return BLOCK_AIR;
}

[[nodiscard]] uint8_t ao_corner(const std::array<std::shared_ptr<Chunk>, 9>& cached_chunks, ChunkPos pos, BlockPos p, const FaceDef& f, int cu, int cv) {
    const int su = cu ? 1 : -1;
    const int sv = cv ? 1 : -1;
    BlockPos n = p + f.normal;
    BlockPos s1 = n + BlockPos{f.u_unit.x * su, f.u_unit.y * su, f.u_unit.z * su};
    BlockPos s2 = n + BlockPos{f.v_unit.x * sv, f.v_unit.y * sv, f.v_unit.z * sv};
    BlockPos c = s1 + BlockPos{f.v_unit.x * sv, f.v_unit.y * sv, f.v_unit.z * sv};
    int o1 = is_opaque(get_block_cached(cached_chunks, pos, s1)) ? 1 : 0;
    int o2 = is_opaque(get_block_cached(cached_chunks, pos, s2)) ? 1 : 0;
    int oc = is_opaque(get_block_cached(cached_chunks, pos, c)) ? 1 : 0;
    if (o1 && o2) return 0;
    return static_cast<uint8_t>(3 - (o1 + o2 + oc));
}

[[nodiscard]] uint8_t alpha_for(BlockId b) {
    if (!is_transparent(b)) return 255;
    if (is_water(b)) return 120;
    if (b == BLOCK_GLASS) return 80;
    if (b == BLOCK_ICE) return 160;
    return 255;
}

void emit_cross(std::vector<Vertex>& verts, std::vector<uint32_t>& idx, BlockPos p,
                Tile tile, uint8_t bl, uint8_t sl, const TextureAtlas& atlas, BlockId b) {
    const float fx = static_cast<float>(p.x);
    const float fy = static_cast<float>(p.y);
    const float fz = static_cast<float>(p.z);

    // V convention must match emit_quad/FACES: v DECREASES toward the top of
    // the block. Authoring paints sprites top-at-low-py, and the atlas upload
    // flips rows, so this mapping stands sprites upright (flame up, blades up).
    float u0 = 0.0f, v0 = 0.0f;
    float u1 = 1.0f, v1 = 0.0f;
    float u2 = 1.0f, v2 = 1.0f;
    float u3 = 0.0f, v3 = 1.0f;
    float w = static_cast<float>(tile);

    uint8_t alpha = alpha_for(b);
    uint8_t r = 255, g = 255, b_col = 255;
    if (is_water(b)) {
        r = 61; g = 168; b_col = 255;
    }

    auto push = [&](float vx, float vy, float vz, float u, float v) {
        Vertex vt{};
        vt.x = fx + vx; vt.y = fy + vy; vt.z = fz + vz;
        vt.u = u; vt.v = v; vt.w = w;
        vt.bl = bl; vt.sl = sl; vt.ao = 3; vt.face = 30;
        vt.r = r; vt.g = g; vt.b = b_col; vt.a = alpha;
        verts.push_back(vt);
    };

    // Quad 1: Diagonal (0,0) to (1,1)
    {
        uint32_t base = static_cast<uint32_t>(verts.size());
        push(0.0f, 0.0f, 0.0f, u0, v0);
        push(1.0f, 0.0f, 1.0f, u1, v1);
        push(1.0f, 1.0f, 1.0f, u2, v2);
        push(0.0f, 1.0f, 0.0f, u3, v3);
        // Double-sided
        idx.push_back(base + 0); idx.push_back(base + 1); idx.push_back(base + 2);
        idx.push_back(base + 0); idx.push_back(base + 2); idx.push_back(base + 3);
        idx.push_back(base + 0); idx.push_back(base + 2); idx.push_back(base + 1);
        idx.push_back(base + 0); idx.push_back(base + 3); idx.push_back(base + 2);
    }

    // Quad 2: Diagonal (1,0) to (0,1)
    {
        uint32_t base = static_cast<uint32_t>(verts.size());
        push(1.0f, 0.0f, 0.0f, u0, v0);
        push(0.0f, 0.0f, 1.0f, u1, v1);
        push(0.0f, 1.0f, 1.0f, u2, v2);
        push(1.0f, 1.0f, 0.0f, u3, v3);
        // Double-sided
        idx.push_back(base + 0); idx.push_back(base + 1); idx.push_back(base + 2);
        idx.push_back(base + 0); idx.push_back(base + 2); idx.push_back(base + 3);
        idx.push_back(base + 0); idx.push_back(base + 2); idx.push_back(base + 1);
        idx.push_back(base + 0); idx.push_back(base + 3); idx.push_back(base + 2);
    }
}

void emit_quad(std::vector<Vertex>& verts, std::vector<uint32_t>& idx, const FaceDef& f, BlockPos p,
               Tile tile, uint8_t bl, uint8_t sl, const uint8_t (&ao)[4], const TextureAtlas& atlas, BlockId b, bool is_fluid_top) {
    const uint32_t base = static_cast<uint32_t>(verts.size());
    
    uint8_t face_val = static_cast<uint8_t>(f.dir);
    if (is_water(b)) face_val += 10;
    else if (is_lava(b)) face_val += 20;

    for (int k = 0; k < 4; ++k) {
        const auto& c = f.corners[k];
        
        Vertex vert{};
        vert.x = static_cast<float>(p.x + c.dx);
        vert.y = static_cast<float>(p.y + c.dy);
        vert.z = static_cast<float>(p.z + c.dz);
        
        // Fluid top face adjustment — level-based height
        if (is_fluid_top && c.dy == 1) {
            if (is_water(b)) {
                int lev = fluid_level(b);
                vert.y -= 0.0625f + static_cast<float>(lev) * 0.0625f;
            } else if (is_lava(b)) {
                int lev = fluid_level(b);
                vert.y -= 0.0625f + static_cast<float>(lev) * 0.0625f;
            }
        }

        float u = c.u;
        float v = c.v;
        if (tile == Tile::Leaves) {
            int hash = (p.x * 73856093) ^ (p.y * 19349663) ^ (p.z * 83492791);
            int rot = (hash >> 8) & 3;
            if (rot == 1) { u = 1.0f - c.v; v = c.u; }
            else if (rot == 2) { u = 1.0f - c.u; v = 1.0f - c.v; }
            else if (rot == 3) { u = c.v; v = 1.0f - c.u; }
        }

        vert.u = u;
        vert.v = v;
        vert.w = static_cast<float>(tile);
        vert.bl = bl;
        vert.sl = sl;
        vert.ao = ao[k];
        vert.face = face_val;
        vert.r = 255; vert.g = 255; vert.b = 255;
        vert.a = alpha_for(b);
        verts.push_back(vert);
    }
    if (ao[0] + ao[2] > ao[1] + ao[3]) {
        idx.push_back(base + 1);
        idx.push_back(base + 2);
        idx.push_back(base + 3);
        idx.push_back(base + 1);
        idx.push_back(base + 3);
        idx.push_back(base + 0);
    } else {
        idx.push_back(base + 0);
        idx.push_back(base + 1);
        idx.push_back(base + 2);
        idx.push_back(base + 0);
        idx.push_back(base + 2);
        idx.push_back(base + 3);
    }
}

// Greedy meshing: merge adjacent identical faces into larger quads (PHASE3 §2).
// Correctly scales vertex positions and maps texture coordinates across the merged quad.
void emit_greedy_quads(std::vector<Vertex>& verts, std::vector<uint32_t>& idx,
                       Direction face_dir, BlockPos p, int w, int h,
                       Tile tile, uint8_t bl, uint8_t sl, const uint8_t (&ao)[4],
                       uint8_t a, const TextureAtlas& atlas) {
    const auto& f = FACES[static_cast<int>(face_dir)];
    const uint32_t base = static_cast<uint32_t>(verts.size());

    uint8_t face_val = static_cast<uint8_t>(face_dir);

    for (int k = 0; k < 4; ++k) {
        const auto& c = f.corners[k];
        
        float tex_u = c.u * static_cast<float>(w);
        float tex_v = c.v * static_cast<float>(h);

        // Decompose original corner offset into local u and v axis projections
        int u_part = c.dx * f.u_unit.x + c.dy * f.u_unit.y + c.dz * f.u_unit.z;
        int v_part = c.dx * f.v_unit.x + c.dy * f.v_unit.y + c.dz * f.v_unit.z;

        Vertex vert{};
        // Scale only the components corresponding to the local face axes (u_unit and v_unit)
        vert.x = static_cast<float>(p.x) + static_cast<float>(c.dx) + static_cast<float>(f.u_unit.x * u_part * (w - 1)) + static_cast<float>(f.v_unit.x * v_part * (h - 1));
        vert.y = static_cast<float>(p.y) + static_cast<float>(c.dy) + static_cast<float>(f.u_unit.y * u_part * (w - 1)) + static_cast<float>(f.v_unit.y * v_part * (h - 1));
        vert.z = static_cast<float>(p.z) + static_cast<float>(c.dz) + static_cast<float>(f.u_unit.z * u_part * (w - 1)) + static_cast<float>(f.v_unit.z * v_part * (h - 1));

        vert.u = tex_u;
        vert.v = tex_v;
        vert.w = static_cast<float>(tile);
        vert.bl = bl;
        vert.sl = sl;
        vert.ao = ao[k];
        vert.face = face_val;
        vert.r = 255; vert.g = 255; vert.b = 255;
        vert.a = a;
        verts.push_back(vert);
    }
    if (ao[0] + ao[2] > ao[1] + ao[3]) {
        idx.push_back(base + 1);
        idx.push_back(base + 2);
        idx.push_back(base + 3);
        idx.push_back(base + 1);
        idx.push_back(base + 3);
        idx.push_back(base + 0);
    } else {
        idx.push_back(base + 0);
        idx.push_back(base + 1);
        idx.push_back(base + 2);
        idx.push_back(base + 0);
        idx.push_back(base + 2);
        idx.push_back(base + 3);
    }
}

struct FaceInfo {
    BlockId b = BLOCK_AIR;
    Tile tile = Tile::Air;
    uint8_t bl = 0;
    uint8_t sl = 15;
    uint8_t ao[4] = {3, 3, 3, 3};
    bool needs_mesh = false;
};

// Greedy-mesh a single face direction for a chunk.
void greedy_pass_direction(std::vector<Vertex>& verts, std::vector<uint32_t>& idx,
                           const Chunk* chunk, const std::array<std::shared_ptr<Chunk>, 9>& cached_chunks, ChunkPos pos,
                           Direction face_dir, const TextureAtlas& atlas, int max_h) {
    const auto& f = FACES[static_cast<int>(face_dir)];
    BlockPos normal = f.normal;

    // Determine depth axis and sweep range
    int depth_min, depth_max;
    if (normal.y != 0) {
        depth_min = MIN_Y;
        depth_max = max_h - 1;
        if (depth_max < depth_min) return;
    }
    else {
        depth_min = 0;
        depth_max = CHUNK_SIZE - 1;
    }

    int base_x = pos.x * CHUNK_SIZE;
    int base_z = pos.z * CHUNK_SIZE;

    // 2D grid dimensions (cap vertical dimension at max height to skip empty sky sections)
    int dim_u, dim_v;
    if (normal.y != 0) { dim_u = CHUNK_SIZE; dim_v = CHUNK_SIZE; }
    else if (normal.z != 0) { dim_u = CHUNK_SIZE; dim_v = std::min(WORLD_HEIGHT, max_h - MIN_Y); }
    else { dim_u = CHUNK_SIZE; dim_v = std::min(WORLD_HEIGHT, max_h - MIN_Y); }

    if (dim_v <= 0) return;

    // Thread-local cache containers to avoid heap allocation churn per direction pass (Golden Rule #37)
    thread_local std::vector<FaceInfo> grid;
    thread_local std::vector<bool> visited;
    grid.resize(dim_u * dim_v);
    visited.resize(dim_u * dim_v);

    auto get_world_pos = [&](int depth, int u, int v) -> BlockPos {
        int wx, wy, wz;
        if (normal.y != 0) { wx = base_x + u; wy = depth; wz = base_z + v; }
        else if (normal.z != 0) { wx = base_x + u; wy = MIN_Y + v; wz = base_z + depth; }
        else { wx = base_x + depth; wy = MIN_Y + v; wz = base_z + u; }
        return BlockPos(wx, wy, wz);
    };

    for (int depth = depth_min; depth <= depth_max; ++depth) {
        std::fill(grid.begin(), grid.end(), FaceInfo{});
        std::fill(visited.begin(), visited.end(), false);

        bool has_any_face = false;

        // 1. Populate the 2D grid of faces for this slice
        for (int v = 0; v < dim_v; ++v) {
            for (int u = 0; u < dim_u; ++u) {
                BlockPos bp = get_world_pos(depth, u, v);
                BlockId b = get_block_cached(cached_chunks, pos, bp);
                if (b == BLOCK_AIR) continue;
                BlockPos np = bp + normal;
                BlockId n = get_block_cached(cached_chunks, pos, np);

                if (!should_render(b, n)) continue;
                // Only greedy-merge opaque, non-liquid blocks
                if (is_transparent(b) || is_liquid(b)) continue;

                Tile tile = tile_for_face(b, face_dir);
                if (tile == Tile::Air) continue;

                uint8_t face_bl = 0, face_sl = 15;
                if (np.y >= MIN_Y && np.y < MAX_Y) {
                    int cx = np.x >> 4;
                    int cz = np.z >> 4;
                    int rdx = cx - pos.x;
                    int rdz = cz - pos.z;
                    const Chunk* nc = nullptr;
                    if (rdx >= -1 && rdx <= 1 && rdz >= -1 && rdz <= 1) {
                        nc = cached_chunks[(rdz + 1) * 3 + (rdx + 1)].get();
                    }
                    if (nc) {
                        int face_sy = np.y >> 4;
                        face_bl = nc->light[face_sy].get_block_light(np.x & 15, np.y & 15, np.z & 15);
                        face_sl = nc->light[face_sy].get_sky_light(np.x & 15, np.y & 15, np.z & 15);
                    }
                } else if (np.y < MIN_Y) {
                    face_sl = 0;
                }

                FaceInfo& info = grid[v * dim_u + u];
                info.b = b;
                info.tile = tile;
                info.bl = face_bl;
                info.sl = face_sl;
                info.needs_mesh = true;
                for (int k = 0; k < 4; ++k) {
                    int cu = static_cast<int>(f.corners[k].u);
                    int cv = static_cast<int>(f.corners[k].v);
                    info.ao[k] = ao_corner(cached_chunks, pos, bp, f, cu, cv);
                }
                has_any_face = true;
            }
        }

        if (!has_any_face) continue;

        // 2. Sweep the 2D grid to build and emit greedy quads
        for (int v = 0; v < dim_v; ++v) {
            for (int u = 0; u < dim_u; ++u) {
                int idx_2d = v * dim_u + u;
                if (visited[idx_2d]) continue;
                const FaceInfo& start_info = grid[idx_2d];
                if (!start_info.needs_mesh) continue;

                // Find width (how far we can extend along u)
                int w = 1;
                while (u + w < dim_u) {
                    int next_idx = v * dim_u + (u + w);
                    if (visited[next_idx]) break;
                    const FaceInfo& next_info = grid[next_idx];
                    if (!next_info.needs_mesh) break;
                    
                    if (next_info.b != start_info.b ||
                        next_info.tile != start_info.tile ||
                        next_info.bl != start_info.bl ||
                        next_info.sl != start_info.sl ||
                        next_info.ao[0] != start_info.ao[0] ||
                        next_info.ao[1] != start_info.ao[1] ||
                        next_info.ao[2] != start_info.ao[2] ||
                        next_info.ao[3] != start_info.ao[3]) {
                        break;
                    }
                    w++;
                }

                // Find height (how far we can extend along v)
                int h = 1;
                bool ok = true;
                while (v + h < dim_v) {
                    for (int k = 0; k < w; ++k) {
                        int next_idx = (v + h) * dim_u + (u + k);
                        if (visited[next_idx]) { ok = false; break; }
                        const FaceInfo& next_info = grid[next_idx];
                        if (!next_info.needs_mesh) { ok = false; break; }
                        if (next_info.b != start_info.b ||
                            next_info.tile != start_info.tile ||
                            next_info.bl != start_info.bl ||
                            next_info.sl != start_info.sl ||
                            next_info.ao[0] != start_info.ao[0] ||
                            next_info.ao[1] != start_info.ao[1] ||
                            next_info.ao[2] != start_info.ao[2] ||
                            next_info.ao[3] != start_info.ao[3]) {
                            ok = false;
                            break;
                        }
                    }
                    if (!ok) break;
                    h++;
                }

                // Mark cells as visited
                for (int dy = 0; dy < h; ++dy) {
                    for (int dx = 0; dx < w; ++dx) {
                        visited[(v + dy) * dim_u + (u + dx)] = true;
                    }
                }

                // Emit the merged quad!
                BlockPos bp = get_world_pos(depth, u, v);
                uint8_t alpha = alpha_for(start_info.b);
                emit_greedy_quads(verts, idx, face_dir, bp, w, h, start_info.tile, start_info.bl, start_info.sl, start_info.ao, alpha, atlas);
            }
        }
    }
}

} // anonymous namespace

void build_chunk_mesh(const std::array<std::shared_ptr<Chunk>, 9>& cached_chunks, ChunkPos pos, const TextureAtlas& atlas, ChunkMeshData& data) {
    ZoneScoped;
    data.pos = pos;
    data.opaque_verts.clear();
    data.opaque_idx.clear();
    data.trans_verts.clear();
    data.trans_idx.clear();

    const Chunk* chunk = cached_chunks[1 * 3 + 1].get();
    if (!chunk) return;

    const int base_x = pos.x * CHUNK_SIZE;
    const int base_z = pos.z * CHUNK_SIZE;

    int max_h = MIN_Y;
    for (const auto& hm : chunk->heightmap)
        if (hm > max_h) max_h = hm;
    // The heightmap intentionally ignores water; the water surface (SEA_LEVEL)
    // sits above the solid-terrain top in deep columns, so extend the sweep —
    // otherwise deep water loses its top/side faces (see-through to the bed).
    max_h = std::max(max_h, SEA_LEVEL + 1);
    if (max_h <= MIN_Y) return;

    // Pre-reserve capacities to reduce vector allocation overheads (Golden Rule #37)
    // Using reserve on already-reserved vectors does not allocate, just guarantees capacity
    data.opaque_verts.reserve(1024);
    data.opaque_idx.reserve(1536);
    data.trans_verts.reserve(256);
    data.trans_idx.reserve(384);

    // Greedy meshing for all 6 faces of opaque blocks — PHASE3 §2
    for (int d = 0; d < 6; ++d) {
        greedy_pass_direction(data.opaque_verts, data.opaque_idx, chunk, cached_chunks, pos, static_cast<Direction>(d), atlas, max_h);
    }

    // Per-block pass for transparent blocks and non-full-cube
    int max_section = section_index(max_h - 1);
    for (int sy = 0; sy <= max_section; ++sy) {
        if (chunk->sections[sy].is_all_air()) continue;

        int section_base_y = MIN_Y + sy * SECTION_SIZE;
        for (int ly = 0; ly < SECTION_SIZE; ++ly) {
            int world_y = section_base_y + ly;
            if (world_y > max_h) break;
            for (int lz = 0; lz < CHUNK_SIZE; ++lz) {
                for (int lx = 0; lx < CHUNK_SIZE; ++lx) {
                    BlockPos p{base_x + lx, world_y, base_z + lz};
                    BlockId b = chunk->get_block(lx, world_y, lz);
                    if (b == BLOCK_AIR) continue;
                    if (!is_transparent(b) && !is_liquid(b) && is_full_cube(b)) continue; // handled by greedy

                    bool is_cutout = (alpha_for(b) == 255);
                    auto& verts = is_cutout ? data.opaque_verts : data.trans_verts;
                    auto& idx = is_cutout ? data.opaque_idx : data.trans_idx;

                    bool is_leaf = tile_for_face(b, Direction::Up) == Tile::Leaves;
                    if (!is_full_cube(b) && is_transparent(b) && !is_liquid(b) && !is_leaf) {
                        // Cross-shaped plants (grass, flowers, torches).
                        // NOTE: liquids and leaves are full-scale cubes even
                        // though full_cube=false — they must fall through to
                        // the face loop below, otherwise they render as
                        // X-crosses.
                        Tile tile = tile_for_face(b, Direction::Up);
                        int sec = section_index(world_y);
                        uint8_t bl = chunk->light[sec].get_block_light(lx, local_y(world_y), lz);
                        uint8_t sl = chunk->light[sec].get_sky_light(lx, local_y(world_y), lz);
                        emit_cross(verts, idx, p, tile, bl, sl, atlas, b);
                        continue;
                    }

                    for (const auto& f : FACES) {
                        BlockPos np = p + f.normal;
                        BlockId n = get_block_cached(cached_chunks, pos, np);
                        if (!should_render(b, n)) continue;

                        uint8_t face_bl = 0, face_sl = 15;
                        if (np.y >= MIN_Y && np.y < MAX_Y) {
                            int cx = np.x >> 4;
                            int cz = np.z >> 4;
                            int rdx = cx - pos.x;
                            int rdz = cz - pos.z;
                            const Chunk* nc = nullptr;
                            if (rdx >= -1 && rdx <= 1 && rdz >= -1 && rdz <= 1) {
                                nc = cached_chunks[(rdz + 1) * 3 + (rdx + 1)].get();
                            }
                            if (nc) {
                                int face_sy = np.y >> 4;
                                face_bl = nc->light[face_sy].get_block_light(np.x & 15, np.y & 15, np.z & 15);
                                face_sl = nc->light[face_sy].get_sky_light(np.x & 15, np.y & 15, np.z & 15);
                            }
                        } else if (np.y < MIN_Y) face_sl = 0;

                        Tile tile = tile_for_face(b, f.dir);
                        uint8_t ao[4] = {3, 3, 3, 3};
                        for (int k = 0; k < 4; ++k) {
                            int cu = static_cast<int>(f.corners[k].u);
                            int cv = static_cast<int>(f.corners[k].v);
                            ao[k] = ao_corner(cached_chunks, pos, p, f, cu, cv);
                        }

                        bool is_fluid_top = false;
                        if (is_fluid(b)) {
                            is_fluid_top = !is_fluid(get_block_cached(cached_chunks, pos, BlockPos{p.x, p.y + 1, p.z}));
                        }

                        emit_quad(verts, idx, f, p, tile, face_bl, face_sl, ao, atlas, b, is_fluid_top);
                    }
                }
            }
        }
    }
}

} // namespace mc
