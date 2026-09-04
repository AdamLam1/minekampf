#include "save/level_storage.hpp"
#include "save/safe_file.hpp"
#include <cstring>
#include <stdexcept>

#include <filesystem>
#include <fstream>
#include <iostream>

namespace mc {

LevelStorage::LevelStorage(const std::string& world_dir) : m_world_dir(world_dir) {
    std::filesystem::path p(world_dir);
    if (!std::filesystem::exists(p)) {
        std::filesystem::create_directories(p);
    }
    std::filesystem::path region_p = p / "region";
    if (!std::filesystem::exists(region_p)) {
        std::filesystem::create_directories(region_p);
    }
}

void LevelStorage::save_level_dat(const World& world) {
    std::lock_guard<std::mutex> lock(m_mutex);
    NbtTag data = NbtTag::Compound();
    auto& data_comp = *std::get<std::unique_ptr<NbtCompound>>(data.value);

    NbtTag version = NbtTag::Compound();
    auto& ver_comp = *std::get<std::unique_ptr<NbtCompound>>(version.value);
    ver_comp["Id"] = NbtTag((int32_t)3463);
    ver_comp["Name"] = NbtTag("1.20.4");
    data_comp["version"] = std::move(version);

    data_comp["LevelName"] = NbtTag("Minekampf World");
    data_comp["RandomSeed"] = NbtTag((int64_t)world.seed);
    data_comp["GameType"] = NbtTag((int32_t)0); // Survival
    data_comp["Difficulty"] = NbtTag((int8_t)2); // Normal
    // Add other fields as necessary...

    NbtTag root = NbtTag::Compound();
    auto& root_comp = *std::get<std::unique_ptr<NbtCompound>>(root.value);
    root_comp["Data"] = std::move(data);

    std::vector<uint8_t> bytes = NbtSerializer::serialize("", root);
    
    std::filesystem::path p = std::filesystem::path(m_world_dir) / "level.dat";
    savefs::write_atomic(p, bytes.data(), bytes.size());
}

bool LevelStorage::load_level_dat(World& world) {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::filesystem::path p = std::filesystem::path(m_world_dir) / "level.dat";
    std::vector<uint8_t> bytes;
    if (!savefs::read_with_fallback_validated(p, bytes, [](const std::vector<uint8_t>& b) {
            try {
                auto [n, r] = NbtSerializer::deserialize(b);
                return r.type == NbtTagType::Compound;
            } catch (...) { return false; }
        })) return false;

    try {
    auto [name, root] = NbtSerializer::deserialize(bytes);
    if (root.type == NbtTagType::Compound) {
        const auto& root_comp = *std::get<std::unique_ptr<NbtCompound>>(root.value);
        if (root_comp.contains("Data") && root_comp.at("Data").type == NbtTagType::Compound) {
            const auto& data = *std::get<std::unique_ptr<NbtCompound>>(root_comp.at("Data").value);
            if (data.contains("RandomSeed")) {
                world.seed = (uint64_t)std::get<int64_t>(data.at("RandomSeed").value);
                return true;
            }
        }
    }
    } catch (const std::exception&) {
        return false; // both copies unusable: caller regenerates the world
    }
    return false;
}

std::string LevelStorage::get_dimension_dir(DimensionId dim) const {
    switch (dim) {
        case DimensionId::Nether: return "DIM-1";
        case DimensionId::End: return "DIM1";
        case DimensionId::Overworld:
        default: return "";
    }
}

RegionFile* LevelStorage::get_or_open_region(ChunkPos cp, DimensionId dim) {
    int rx = cp.x >> 5;
    int rz = cp.z >> 5;
    // Combine dimension and region coords for cache key
    uint64_t key = ((uint64_t)static_cast<uint8_t>(dim) << 56) | ((uint64_t)(uint32_t)rx << 32) | (uint32_t)rz;

    if (!m_region_cache.contains(key)) {
        std::filesystem::path dim_p = std::filesystem::path(m_world_dir);
        std::string dim_str = get_dimension_dir(dim);
        if (!dim_str.empty()) dim_p /= dim_str;
        
        std::filesystem::path rp = dim_p / "region";
        if (!std::filesystem::exists(rp)) std::filesystem::create_directories(rp);
        
        rp /= ("r." + std::to_string(rx) + "." + std::to_string(rz) + ".mca");
        m_region_cache[key] = std::make_unique<RegionFile>(rp.string());
    }
    return m_region_cache[key].get();
}

NbtTag LevelStorage::chunk_to_nbt(const Chunk& chunk) {
    NbtTag nbt = NbtTag::Compound();
    auto& comp = *std::get<std::unique_ptr<NbtCompound>>(nbt.value);

    comp["xPos"] = NbtTag((int32_t)chunk.pos.x);
    comp["zPos"] = NbtTag((int32_t)chunk.pos.z);
    comp["Status"] = NbtTag("full");

    std::vector<int32_t> hm(CHUNK_SIZE * CHUNK_SIZE);
    for (int i = 0; i < CHUNK_SIZE * CHUNK_SIZE; ++i) {
        hm[i] = chunk.heightmap[i];
    }
    comp["Heightmap"] = NbtTag(std::move(hm));

    NbtTag sections = NbtTag::List(NbtTagType::Compound);
    auto& sec_list = *std::get<std::unique_ptr<NbtList>>(sections.value);
    
    for (int y = 0; y < SECTIONS_PER_CHUNK; ++y) {
        if (chunk.sections[y].is_all_air()) continue;
        
        NbtTag sec = NbtTag::Compound();
        auto& sec_comp = *std::get<std::unique_ptr<NbtCompound>>(sec.value);
        sec_comp["Y"] = NbtTag((int8_t)y);
        
        std::vector<int8_t> blocks(4096);
        for (int i = 0; i < 4096; ++i) {
            blocks[i] = static_cast<int8_t>(chunk.sections[y].get_linear(i));
        }
        sec_comp["Blocks"] = NbtTag(std::move(blocks));
        sec_list.push_back(std::move(sec));
    }
    comp["Sections"] = std::move(sections);

    return nbt;
}

void LevelStorage::nbt_to_chunk(const NbtTag& nbt, Chunk& chunk) {
    if (nbt.type != NbtTagType::Compound) return;
    const auto& comp = *std::get<std::unique_ptr<NbtCompound>>(nbt.value);

    if (comp.contains("Heightmap") && comp.at("Heightmap").type == NbtTagType::IntArray) {
        const auto& hm = std::get<std::vector<int32_t>>(comp.at("Heightmap").value);
        for (size_t i = 0; i < std::min(hm.size(), (size_t)(CHUNK_SIZE * CHUNK_SIZE)); ++i) {
            chunk.heightmap[i] = hm[i];
        }
    }

    if (comp.contains("Sections") && comp.at("Sections").type == NbtTagType::List) {
        const auto& sec_list = *std::get<std::unique_ptr<NbtList>>(comp.at("Sections").value);
        for (const auto& sec_tag : sec_list) {
            if (sec_tag.type != NbtTagType::Compound) continue;
            const auto& sec_comp = *std::get<std::unique_ptr<NbtCompound>>(sec_tag.value);
            
            if (!sec_comp.contains("Y") || !sec_comp.contains("Blocks")) continue;
            
            int8_t y = 0;
            if (sec_comp.at("Y").type == NbtTagType::Byte) y = std::get<int8_t>(sec_comp.at("Y").value);
            
            if (y >= 0 && y < SECTIONS_PER_CHUNK) {
                const auto& blocks_tag = sec_comp.at("Blocks");
                if (blocks_tag.type == NbtTagType::ByteArray) {
                    const auto& blocks = std::get<std::vector<int8_t>>(blocks_tag.value);
                    for (size_t i = 0; i < std::min(blocks.size(), (size_t)4096); ++i) {
                        chunk.sections[y].set_linear(static_cast<int>(i), static_cast<BlockId>(static_cast<uint8_t>(blocks[i])));
                    }
                }
            }
        }
    }
}

void LevelStorage::save_chunk(const Chunk& chunk, DimensionId dim) {
    if (!chunk.save_dirty.load(std::memory_order_relaxed)) return;

    std::lock_guard<std::mutex> lock(m_mutex);
    NbtTag nbt = chunk_to_nbt(chunk);
    RegionFile* region = get_or_open_region(chunk.pos, dim);
    if (region) region->write_chunk(chunk.pos.x, chunk.pos.z, nbt);
    
    // Cast away constness to clear dirty flag
    const_cast<Chunk&>(chunk).save_dirty.store(false, std::memory_order_relaxed);
}

bool LevelStorage::load_chunk(Chunk& chunk, DimensionId dim) {
    std::lock_guard<std::mutex> lock(m_mutex);
    RegionFile* region = get_or_open_region(chunk.pos, dim);
    if (!region) return false;
    auto nbt_opt = region->read_chunk(chunk.pos.x, chunk.pos.z);
    
    if (nbt_opt.has_value()) {
        const auto& comp = *std::get<std::unique_ptr<NbtCompound>>(nbt_opt.value().value);
        if (!comp.contains("Sections")) return false; // Force regeneration of old empty chunks
        nbt_to_chunk(nbt_opt.value(), chunk);
        chunk.save_dirty.store(false, std::memory_order_relaxed);
        return true;
    }
    return false;
}

void LevelStorage::save_all_dirty(World& world) {
    // This is called from the main thread, so we don't need to lock the world.
    // The individual save_chunk calls will lock the storage mutex.
    const auto& chunks = world.chunks();
    for (const auto& [pos, chunk_ptr] : chunks) {
        if (chunk_ptr->save_dirty.load(std::memory_order_relaxed)) {
            save_chunk(*chunk_ptr, world.dimension_id);
        }
    }
    save_level_dat(world);
}

void LevelStorage::save_player_dat(const Player& player) {
    std::lock_guard<std::mutex> lock(m_mutex);
    NbtTag root = NbtTag::Compound();
    auto& comp = *std::get<std::unique_ptr<NbtCompound>>(root.value);
    
    NbtTag pos = NbtTag::List(NbtTagType::Double);
    auto& pos_list = *std::get<std::unique_ptr<NbtList>>(pos.value);
    pos_list.push_back(NbtTag((double)player.pos.x));
    pos_list.push_back(NbtTag((double)player.pos.y));
    pos_list.push_back(NbtTag((double)player.pos.z));
    comp["Pos"] = std::move(pos);

    NbtTag mot = NbtTag::List(NbtTagType::Double);
    auto& mot_list = *std::get<std::unique_ptr<NbtList>>(mot.value);
    mot_list.push_back(NbtTag((double)player.velocity.x));
    mot_list.push_back(NbtTag((double)player.velocity.y));
    mot_list.push_back(NbtTag((double)player.velocity.z));
    comp["Motion"] = std::move(mot);
    
    NbtTag rot = NbtTag::List(NbtTagType::Float);
    auto& rot_list = *std::get<std::unique_ptr<NbtList>>(rot.value);
    rot_list.push_back(NbtTag((float)player.yaw));
    rot_list.push_back(NbtTag((float)player.pitch));
    comp["Rotation"] = std::move(rot);

    comp["Dimension"] = NbtTag(static_cast<int32_t>(player.dimension));
    comp["Health"] = NbtTag(player.health);
    comp["foodLevel"] = NbtTag(static_cast<int32_t>(player.food_level));
    comp["foodSaturationLevel"] = NbtTag(player.food_saturation);
    comp["foodExhaustionLevel"] = NbtTag(player.food_exhaustion);
    comp["XpLevel"] = NbtTag(static_cast<int32_t>(player.xp_level));
    comp["XpP"] = NbtTag(player.xp_progress);
    comp["XpTotal"] = NbtTag(static_cast<int32_t>(player.xp_total));
    comp["SleepTimer"] = NbtTag(static_cast<int32_t>(player.time_since_rest));
    comp["QuestId"] = NbtTag(static_cast<int32_t>(player.quest.quest_id));
    comp["QuestState"] = NbtTag(static_cast<int32_t>(static_cast<uint8_t>(player.quest.state)));
    comp["QuestProgress"] = NbtTag(static_cast<int32_t>(player.quest.progress));
    comp["playerGameType"] = NbtTag(static_cast<int32_t>(player.mode));
    comp["Invulnerable"] = NbtTag(static_cast<int8_t>(player.invulnerable ? 1 : 0));

    // Abilities compound
    NbtTag abilities = NbtTag::Compound();
    auto& ab = *std::get<std::unique_ptr<NbtCompound>>(abilities.value);
    ab["flying"] = NbtTag(static_cast<int8_t>(player.flying ? 1 : 0));
    ab["flySpeed"] = NbtTag(player.fly_speed);
    ab["walkSpeed"] = NbtTag(player.walk_speed);
    ab["mayfly"] = NbtTag(static_cast<int8_t>(player.may_fly ? 1 : 0));
    comp["abilities"] = std::move(abilities);

    NbtTag inv_items = NbtTag::List(NbtTagType::Int);
    auto& hb_items = *std::get<std::unique_ptr<NbtList>>(inv_items.value);
    NbtTag inv_counts = NbtTag::List(NbtTagType::Int);
    auto& hb_counts = *std::get<std::unique_ptr<NbtList>>(inv_counts.value);
    for (size_t i = 0; i < player.inventory.size(); ++i) {
        hb_items.push_back(NbtTag(static_cast<int32_t>(player.inventory.get_slot(i).item)));
        hb_counts.push_back(NbtTag(static_cast<int32_t>(player.inventory.get_slot(i).count)));
    }
    comp["InvItems"] = std::move(inv_items);
    comp["InvCounts"] = std::move(inv_counts);

    NbtTag inv_enchants = NbtTag::List(NbtTagType::Int);
    auto& hb_enchants = *std::get<std::unique_ptr<NbtList>>(inv_enchants.value);
    for (size_t i = 0; i < player.inventory.size(); ++i) {
        hb_enchants.push_back(NbtTag(static_cast<int32_t>(player.inventory.get_slot(i).enchant_levels)));
    }
    comp["InvEnchants"] = std::move(inv_enchants);

    std::vector<uint8_t> bytes = NbtSerializer::serialize("", root);
    std::filesystem::path p = std::filesystem::path(m_world_dir) / "player.dat";
    savefs::write_atomic(p, bytes.data(), bytes.size());
}

bool LevelStorage::load_player_dat(Player& player) {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::filesystem::path p = std::filesystem::path(m_world_dir) / "player.dat";
    std::vector<uint8_t> bytes;
    if (!savefs::read_with_fallback_validated(p, bytes, [](const std::vector<uint8_t>& b) {
            try {
                auto [n, r] = NbtSerializer::deserialize(b);
                return r.type == NbtTagType::Compound;
            } catch (...) { return false; }
        })) return false;

    try {
    auto [name, root] = NbtSerializer::deserialize(bytes);
    if (root.type == NbtTagType::Compound) {
        const auto& comp = *std::get<std::unique_ptr<NbtCompound>>(root.value);

        if (comp.contains("Pos") && comp.at("Pos").type == NbtTagType::List) {
            const auto& ps = *std::get<std::unique_ptr<NbtList>>(comp.at("Pos").value);
            if (ps.size() >= 3) {
                player.pos.x = (float)std::get<double>(ps[0].value);
                player.pos.y = (float)std::get<double>(ps[1].value);
                player.pos.z = (float)std::get<double>(ps[2].value);
                player.prev_pos = player.pos;
            }
        }

        if (comp.contains("Motion") && comp.at("Motion").type == NbtTagType::List) {
            const auto& ms = *std::get<std::unique_ptr<NbtList>>(comp.at("Motion").value);
            if (ms.size() >= 3) {
                player.velocity.x = (float)std::get<double>(ms[0].value);
                player.velocity.y = (float)std::get<double>(ms[1].value);
                player.velocity.z = (float)std::get<double>(ms[2].value);
            }
        }

        if (comp.contains("Rotation") && comp.at("Rotation").type == NbtTagType::List) {
            const auto& rs = *std::get<std::unique_ptr<NbtList>>(comp.at("Rotation").value);
            if (rs.size() >= 2) {
                player.yaw = std::get<float>(rs[0].value);
                player.pitch = std::get<float>(rs[1].value);
            }
        }

        if (comp.contains("Dimension") && comp.at("Dimension").type == NbtTagType::Int)
            player.dimension = static_cast<DimensionId>(std::get<int32_t>(comp.at("Dimension").value));

        if (comp.contains("Health") && comp.at("Health").type == NbtTagType::Float)
            player.health = std::get<float>(comp.at("Health").value);

        if (comp.contains("QuestId") && comp.at("QuestId").type == NbtTagType::Int)
            player.quest.quest_id = std::get<int32_t>(comp.at("QuestId").value);
        if (comp.contains("QuestState") && comp.at("QuestState").type == NbtTagType::Int) {
            int32_t qs = std::get<int32_t>(comp.at("QuestState").value);
            if (qs >= 0 && qs <= 3) player.quest.state = static_cast<quest::State>(qs);
        }
        if (comp.contains("QuestProgress") && comp.at("QuestProgress").type == NbtTagType::Int)
            player.quest.progress = std::get<int32_t>(comp.at("QuestProgress").value);

        if (comp.contains("foodLevel") && comp.at("foodLevel").type == NbtTagType::Int)
            player.food_level = std::get<int32_t>(comp.at("foodLevel").value);

        if (comp.contains("foodSaturationLevel") && comp.at("foodSaturationLevel").type == NbtTagType::Float)
            player.food_saturation = std::get<float>(comp.at("foodSaturationLevel").value);

        if (comp.contains("foodExhaustionLevel") && comp.at("foodExhaustionLevel").type == NbtTagType::Float)
            player.food_exhaustion = std::get<float>(comp.at("foodExhaustionLevel").value);

        if (comp.contains("XpLevel") && comp.at("XpLevel").type == NbtTagType::Int)
            player.xp_level = std::get<int32_t>(comp.at("XpLevel").value);

        if (comp.contains("XpP") && comp.at("XpP").type == NbtTagType::Float)
            player.xp_progress = std::get<float>(comp.at("XpP").value);

        if (comp.contains("XpTotal") && comp.at("XpTotal").type == NbtTagType::Int)
            player.xp_total = std::get<int32_t>(comp.at("XpTotal").value);

        if (comp.contains("SleepTimer") && comp.at("SleepTimer").type == NbtTagType::Int)
            player.time_since_rest = std::get<int32_t>(comp.at("SleepTimer").value);

        if (comp.contains("playerGameType") && comp.at("playerGameType").type == NbtTagType::Int)
            player.mode = static_cast<GameMode>(std::get<int32_t>(comp.at("playerGameType").value));

        if (comp.contains("Invulnerable") && comp.at("Invulnerable").type == NbtTagType::Byte)
            player.invulnerable = (std::get<int8_t>(comp.at("Invulnerable").value) != 0);

        if (comp.contains("abilities") && comp.at("abilities").type == NbtTagType::Compound) {
            const auto& ab = *std::get<std::unique_ptr<NbtCompound>>(comp.at("abilities").value);
            if (ab.contains("flying")) player.flying = (std::get<int8_t>(ab.at("flying").value) != 0);
            if (ab.contains("flySpeed")) player.fly_speed = std::get<float>(ab.at("flySpeed").value);
            if (ab.contains("walkSpeed")) player.walk_speed = std::get<float>(ab.at("walkSpeed").value);
            if (ab.contains("mayfly")) player.may_fly = (std::get<int8_t>(ab.at("mayfly").value) != 0);
        }

        if (comp.contains("InvItems") && comp.at("InvItems").type == NbtTagType::List &&
            comp.contains("InvCounts") && comp.at("InvCounts").type == NbtTagType::List) {
            const auto& items = *std::get<std::unique_ptr<NbtList>>(comp.at("InvItems").value);
            const auto& counts = *std::get<std::unique_ptr<NbtList>>(comp.at("InvCounts").value);
            size_t min_size = items.size() < counts.size() ? items.size() : counts.size();
            size_t num_slots = player.inventory.size();
            size_t iter_count = min_size < num_slots ? min_size : num_slots;
            for (size_t i = 0; i < iter_count; ++i) {
                if (items[i].type == NbtTagType::Int && counts[i].type == NbtTagType::Int) {
                    BlockId bid = static_cast<BlockId>(std::get<int32_t>(items[i].value));
                    uint8_t c = static_cast<uint8_t>(std::get<int32_t>(counts[i].value));
                    ItemStack stack(bid, c);
                    // Enchants are optional (older saves): default to none.
                    player.inventory.set_slot(i, stack);
                }
            }
        }

        if (comp.contains("InvEnchants") && comp.at("InvEnchants").type == NbtTagType::List) {
            const auto& enchants = *std::get<std::unique_ptr<NbtList>>(comp.at("InvEnchants").value);
            size_t num_slots = player.inventory.size();
            size_t n = enchants.size() < num_slots ? enchants.size() : num_slots;
            for (size_t i = 0; i < n; ++i) {
                if (enchants[i].type == NbtTagType::Int) {
                    ItemStack s = player.inventory.get_slot(i);
                    s.enchant_levels = static_cast<uint16_t>(std::get<int32_t>(enchants[i].value) & 0xFFFF);
                    player.inventory.set_slot(i, s);
                }
            }
        }

        return true;
    }
    } catch (const std::exception&) {
        return false; // both copies unusable
    }
    return false;
}

void LevelStorage::save_time_of_day(float time_of_day) {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::filesystem::path p = std::filesystem::path(m_world_dir) / "time.dat";
    savefs::write_atomic(p, &time_of_day, sizeof(time_of_day));
}

float LevelStorage::load_time_of_day() {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::filesystem::path p = std::filesystem::path(m_world_dir) / "time.dat";
    std::vector<uint8_t> bytes;
    if (!savefs::read_with_fallback_validated(p, bytes, [](const std::vector<uint8_t>& b) {
            return b.size() >= sizeof(float);
        })) return 0.25f;
    float val = 0.25f;
    std::memcpy(&val, bytes.data(), sizeof(float));
    return val;
}

void LevelStorage::save_furnaces(const std::vector<FurnaceSave>& furnaces) {
    std::lock_guard<std::mutex> lock(m_mutex);
    NbtTag root = NbtTag::Compound();
    auto& comp = *std::get<std::unique_ptr<NbtCompound>>(root.value);

    NbtTag list = NbtTag::List(NbtTagType::Compound);
    auto& entries = *std::get<std::unique_ptr<NbtList>>(list.value);
    for (const auto& f : furnaces) {
        NbtTag e = NbtTag::Compound();
        auto& c = *std::get<std::unique_ptr<NbtCompound>>(e.value);
        c["Dim"] = NbtTag(f.dim);
        c["X"] = NbtTag(f.x);
        c["Y"] = NbtTag(f.y);
        c["Z"] = NbtTag(f.z);
        c["InItem"] = NbtTag(f.in_item);
        c["InCount"] = NbtTag(f.in_count);
        c["FuelItem"] = NbtTag(f.fuel_item);
        c["FuelCount"] = NbtTag(f.fuel_count);
        c["OutItem"] = NbtTag(f.out_item);
        c["OutCount"] = NbtTag(f.out_count);
        c["BurnLeft"] = NbtTag(f.burn_left);
        c["BurnTotal"] = NbtTag(f.burn_total);
        c["Cook"] = NbtTag(f.cook);
        c["PendingXp"] = NbtTag(f.pending_xp);
        entries.push_back(std::move(e));
    }
    comp["Furnaces"] = std::move(list);

    std::vector<uint8_t> bytes = NbtSerializer::serialize("", root);
    std::filesystem::path p = std::filesystem::path(m_world_dir) / "furnaces.dat";
    savefs::write_atomic(p, bytes.data(), bytes.size());
}

std::vector<FurnaceSave> LevelStorage::load_furnaces() {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<FurnaceSave> out;
    std::filesystem::path p = std::filesystem::path(m_world_dir) / "furnaces.dat";
    std::vector<uint8_t> bytes;
    if (!savefs::read_with_fallback_validated(p, bytes, [](const std::vector<uint8_t>& b) {
            try {
                auto [n, r] = NbtSerializer::deserialize(b);
                return r.type == NbtTagType::Compound;
            } catch (...) { return false; }
        })) return out;

    try {
        auto [name, root] = NbtSerializer::deserialize(bytes);
        if (root.type != NbtTagType::Compound) return out;
        const auto& comp = *std::get<std::unique_ptr<NbtCompound>>(root.value);
        if (!comp.contains("Furnaces") || comp.at("Furnaces").type != NbtTagType::List) return out;
        const auto& entries = *std::get<std::unique_ptr<NbtList>>(comp.at("Furnaces").value);

        auto iget = [](const NbtCompound& c, const char* k, int32_t def) {
            auto it = c.find(k);
            return (it != c.end() && it->second.type == NbtTagType::Int)
                       ? std::get<int32_t>(it->second.value) : def;
        };
        for (const auto& e : entries) {
            if (e.type != NbtTagType::Compound) continue;
            const auto& c = *std::get<std::unique_ptr<NbtCompound>>(e.value);
            FurnaceSave f;
            f.dim = iget(c, "Dim", 0);
            f.x = iget(c, "X", 0);
            f.y = iget(c, "Y", 0);
            f.z = iget(c, "Z", 0);
            f.in_item = iget(c, "InItem", 0);
            f.in_count = iget(c, "InCount", 0);
            f.fuel_item = iget(c, "FuelItem", 0);
            f.fuel_count = iget(c, "FuelCount", 0);
            f.out_item = iget(c, "OutItem", 0);
            f.out_count = iget(c, "OutCount", 0);
            f.burn_left = iget(c, "BurnLeft", 0);
            f.burn_total = iget(c, "BurnTotal", 0);
            f.cook = iget(c, "Cook", 0);
            auto xp_it = c.find("PendingXp");
            if (xp_it != c.end() && xp_it->second.type == NbtTagType::Float)
                f.pending_xp = std::get<float>(xp_it->second.value);
            out.push_back(f);
        }
    } catch (...) {
        return {}; // corrupt furnaces.dat: start clean rather than crash
    }
    return out;
}

} // namespace mc
