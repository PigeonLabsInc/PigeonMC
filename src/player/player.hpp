#pragma once

#include "core/types.hpp"
#include "network/connection.hpp"
#include "world/chunk.hpp"
#include <memory>
#include <atomic>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <string>

namespace mc::player {

struct ItemStack {
    u16 item_id;
    u8 count;
    i16 damage;
    
    ItemStack() : item_id(0), count(0), damage(0) {}
    ItemStack(u16 id, u8 count = 1, i16 damage = 0) 
        : item_id(id), count(count), damage(damage) {}
    
    bool is_empty() const { return item_id == 0 || count == 0; }
    bool is_stackable_with(const ItemStack& other) const {
        return item_id == other.item_id && damage == other.damage;
    }
    
    u8 max_stack_size() const {
        if (item_id == 0) return 0;
        if (item_id < 256) return 64;
        return 1;
    }
    
    bool can_merge_with(const ItemStack& other) const {
        return is_stackable_with(other) && count + other.count <= max_stack_size();
    }
};

class Inventory {
private:
    std::vector<ItemStack> slots_;
    size_t size_;
    std::mutex inventory_mutex_;
    
public:
    static constexpr size_t PLAYER_INVENTORY_SIZE = 36;
    static constexpr size_t HOTBAR_SIZE = 9;
    
    explicit Inventory(size_t size = PLAYER_INVENTORY_SIZE) : size_(size) {
        slots_.resize(size);
    }
    
    ItemStack get_item(size_t slot) const {
        std::lock_guard<std::mutex> lock(inventory_mutex_);
        return slot < slots_.size() ? slots_[slot] : ItemStack();
    }
    
    void set_item(size_t slot, const ItemStack& item) {
        std::lock_guard<std::mutex> lock(inventory_mutex_);
        if (slot < slots_.size()) {
            slots_[slot] = item;
        }
    }
    
    bool add_item(const ItemStack& item) {
        if (item.is_empty()) return true;
        
        std::lock_guard<std::mutex> lock(inventory_mutex_);
        ItemStack remaining = item;
        
        for (size_t i = 0; i < slots_.size() && !remaining.is_empty(); ++i) {
            if (slots_[i].is_empty()) {
                slots_[i] = remaining;
                remaining = ItemStack();
            } else if (slots_[i].can_merge_with(remaining)) {
                u8 can_add = slots_[i].max_stack_size() - slots_[i].count;
                u8 to_add = std::min(can_add, remaining.count);
                
                slots_[i].count += to_add;
                remaining.count -= to_add;
                
                if (remaining.count == 0) {
                    remaining = ItemStack();
                }
            }
        }
        
        return remaining.is_empty();
    }
    
    ItemStack remove_item(size_t slot, u8 amount = 255) {
        std::lock_guard<std::mutex> lock(inventory_mutex_);
        
        if (slot >= slots_.size() || slots_[slot].is_empty()) {
            return ItemStack();
        }
        
        u8 to_remove = std::min(amount, slots_[slot].count);
        ItemStack result(slots_[slot].item_id, to_remove, slots_[slot].damage);
        
        slots_[slot].count -= to_remove;
        if (slots_[slot].count == 0) {
            slots_[slot] = ItemStack();
        }
        
        return result;
    }
    
    bool has_item(u16 item_id, u8 count = 1) const {
        std::lock_guard<std::mutex> lock(inventory_mutex_);
        
        u8 found_count = 0;
        for (const auto& slot : slots_) {
            if (slot.item_id == item_id) {
                found_count += slot.count;
                if (found_count >= count) return true;
            }
        }
        
        return false;
    }
    
    void clear() {
        std::lock_guard<std::mutex> lock(inventory_mutex_);
        std::fill(slots_.begin(), slots_.end(), ItemStack());
    }
    
    size_t size() const { return size_; }
    
    std::vector<ItemStack> get_all_items() const {
        std::lock_guard<std::mutex> lock(inventory_mutex_);
        return slots_;
    }
};

struct PlayerStats {
    f32 health;
    f32 max_health;
    i32 food_level;
    f32 food_saturation;
    f32 exhaustion;
    i32 experience_level;
    f32 experience_progress;
    i32 total_experience;
    
    PlayerStats() : health(20.0f), max_health(20.0f), food_level(20)
        , food_saturation(5.0f), exhaustion(0.0f), experience_level(0)
        , experience_progress(0.0f), total_experience(0) {}
};

enum class PlayerGameMode : u8 {
    SURVIVAL = 0,
    CREATIVE = 1,
    ADVENTURE = 2,
    SPECTATOR = 3
};

class Player {
private:
    network::ConnectionPtr connection_;
    GameProfile profile_;
    u32 entity_id_;
    
    Location location_;
    Location spawn_location_;
    std::mutex location_mutex_;
    
    PlayerGameMode game_mode_;
    PlayerStats stats_;
    std::mutex stats_mutex_;
    
    Inventory inventory_;
    u8 selected_slot_;
    
    std::unordered_set<world::ChunkPos, world::ChunkPosHash> loaded_chunks_;
    std::mutex chunks_mutex_;
    
    std::atomic<bool> online_{false};
    std::atomic<timestamp_t> last_activity_;
    std::atomic<timestamp_t> join_time_;
    
    i32 view_distance_;
    std::atomic<bool> flying_{false};
    std::atomic<bool> sneaking_{false};
    std::atomic<bool> sprinting_{false};

public:
    Player(network::ConnectionPtr connection, const GameProfile& profile, u32 entity_id)
        : connection_(connection), profile_(profile), entity_id_(entity_id)
        , game_mode_(PlayerGameMode::SURVIVAL), selected_slot_(0)
        , view_distance_(10) {
        
        spawn_location_ = Location(0, 65, 0);
        location_ = spawn_location_;
        
        online_.store(true);
        join_time_.store(std::chrono::steady_clock::now());
        last_activity_.store(std::chrono::steady_clock::now());
    }
    
    const GameProfile& get_profile() const { return profile_; }
    u32 get_entity_id() const { return entity_id_; }
    network::ConnectionPtr get_connection() const { return connection_; }
    
    Location get_location() const {
        std::lock_guard<std::mutex> lock(location_mutex_);
        return location_;
    }
    
    void set_location(const Location& location) {
        std::lock_guard<std::mutex> lock(location_mutex_);
        location_ = location;
        last_activity_.store(std::chrono::steady_clock::now());
    }
    
    world::ChunkPos get_chunk_pos() const {
        return get_location().toChunkPos();
    }
    
    Location get_spawn_location() const { return spawn_location_; }
    void set_spawn_location(const Location& location) { spawn_location_ = location; }
    
    PlayerGameMode get_game_mode() const { return game_mode_; }
    void set_game_mode(PlayerGameMode mode) { game_mode_ = mode; }
    
    PlayerStats get_stats() const {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        return stats_;
    }
    
    void set_stats(const PlayerStats& stats) {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_ = stats;
    }
    
    f32 get_health() const {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        return stats_.health;
    }
    
    void set_health(f32 health) {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_.health = std::clamp(health, 0.0f, stats_.max_health);
    }
    
    void damage(f32 amount) {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_.health = std::max(0.0f, stats_.health - amount);
    }
    
    void heal(f32 amount) {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_.health = std::min(stats_.max_health, stats_.health + amount);
    }
    
    bool is_alive() const {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        return stats_.health > 0.0f;
    }
    
    Inventory& get_inventory() { return inventory_; }
    const Inventory& get_inventory() const { return inventory_; }
    
    u8 get_selected_slot() const { return selected_slot_; }
    void set_selected_slot(u8 slot) { 
        selected_slot_ = std::min(slot, static_cast<u8>(8));
    }
    
    ItemStack get_item_in_hand() const {
        return inventory_.get_item(selected_slot_);
    }
    
    void set_item_in_hand(const ItemStack& item) {
        inventory_.set_item(selected_slot_, item);
    }
    
    i32 get_view_distance() const { return view_distance_; }
    void set_view_distance(i32 distance) { 
        view_distance_ = std::clamp(distance, 2, 32);
    }
    
    bool is_flying() const { return flying_.load(); }
    void set_flying(bool flying) { flying_.store(flying); }
    
    bool is_sneaking() const { return sneaking_.load(); }
    void set_sneaking(bool sneaking) { sneaking_.store(sneaking); }
    
    bool is_sprinting() const { return sprinting_.load(); }
    void set_sprinting(bool sprinting) { sprinting_.store(sprinting); }
    
    bool is_online() const { return online_.load() && connection_ && !connection_->is_closed(); }
    
    void disconnect() {
        online_.store(false);
        if (connection_) {
            connection_->close();
        }
    }
    
    timestamp_t get_join_time() const { return join_time_.load(); }
    timestamp_t get_last_activity() const { return last_activity_.load(); }
    
    void update_activity() {
        last_activity_.store(std::chrono::steady_clock::now());
    }
    
    void update_loaded_chunks() {
        world::ChunkPos player_chunk = get_chunk_pos();
        std::unordered_set<world::ChunkPos, world::ChunkPosHash> needed_chunks;
        
        for (i32 dx = -view_distance_; dx <= view_distance_; ++dx) {
            for (i32 dz = -view_distance_; dz <= view_distance_; ++dz) {
                if (dx * dx + dz * dz <= view_distance_ * view_distance_) {
                    needed_chunks.insert(world::ChunkPos(player_chunk.x + dx, player_chunk.z + dz));
                }
            }
        }
        
        std::lock_guard<std::mutex> lock(chunks_mutex_);
        
        for (auto it = loaded_chunks_.begin(); it != loaded_chunks_.end();) {
            if (needed_chunks.find(*it) == needed_chunks.end()) {
                it = loaded_chunks_.erase(it);
            } else {
                ++it;
            }
        }
        
        for (const auto& chunk_pos : needed_chunks) {
            if (loaded_chunks_.find(chunk_pos) == loaded_chunks_.end()) {
                loaded_chunks_.insert(chunk_pos);
                world::g_chunk_manager.load_chunk(chunk_pos);
            }
        }
    }
    
    std::unordered_set<world::ChunkPos, world::ChunkPosHash> get_loaded_chunks() const {
        std::lock_guard<std::mutex> lock(chunks_mutex_);
        return loaded_chunks_;
    }
    
    f64 distance_to(const Player& other) const {
        Location my_loc = get_location();
        Location other_loc = other.get_location();
        
        f64 dx = my_loc.x - other_loc.x;
        f64 dy = my_loc.y - other_loc.y;
        f64 dz = my_loc.z - other_loc.z;
        
        return std::sqrt(dx * dx + dy * dy + dz * dz);
    }
    
    f64 distance_to(const Location& location) const {
        Location my_loc = get_location();
        
        f64 dx = my_loc.x - location.x;
        f64 dy = my_loc.y - location.y;
        f64 dz = my_loc.z - location.z;
        
        return std::sqrt(dx * dx + dy * dy + dz * dz);
    }
};

using PlayerPtr = std::shared_ptr<Player>;

class PlayerManager {
private:
    std::unordered_map<UUID, PlayerPtr> players_by_uuid_;
    std::unordered_map<std::string, PlayerPtr> players_by_name_;
    std::unordered_map<u32, PlayerPtr> players_by_entity_id_;
    std::mutex players_mutex_;
    
    std::atomic<u32> next_entity_id_{1};

public:
    PlayerPtr create_player(network::ConnectionPtr connection, const GameProfile& profile) {
        u32 entity_id = next_entity_id_.fetch_add(1);
        auto player = std::make_shared<Player>(connection, profile, entity_id);
        
        std::lock_guard<std::mutex> lock(players_mutex_);
        players_by_uuid_[profile.uuid] = player;
        players_by_name_[profile.username] = player;
        players_by_entity_id_[entity_id] = player;
        
        return player;
    }
    
    void remove_player(const UUID& uuid) {
        std::lock_guard<std::mutex> lock(players_mutex_);
        
        auto it = players_by_uuid_.find(uuid);
        if (it != players_by_uuid_.end()) {
            PlayerPtr player = it->second;
            
            players_by_uuid_.erase(it);
            players_by_name_.erase(player->get_profile().username);
            players_by_entity_id_.erase(player->get_entity_id());
        }
    }
    
    PlayerPtr get_player(const UUID& uuid) const {
        std::lock_guard<std::mutex> lock(players_mutex_);
        auto it = players_by_uuid_.find(uuid);
        return it != players_by_uuid_.end() ? it->second : nullptr;
    }
    
    PlayerPtr get_player(const std::string& username) const {
        std::lock_guard<std::mutex> lock(players_mutex_);
        auto it = players_by_name_.find(username);
        return it != players_by_name_.end() ? it->second : nullptr;
    }
    
    PlayerPtr get_player(u32 entity_id) const {
        std::lock_guard<std::mutex> lock(players_mutex_);
        auto it = players_by_entity_id_.find(entity_id);
        return it != players_by_entity_id_.end() ? it->second : nullptr;
    }
    
    std::vector<PlayerPtr> get_all_players() const {
        std::lock_guard<std::mutex> lock(players_mutex_);
        std::vector<PlayerPtr> players;
        
        for (const auto& [uuid, player] : players_by_uuid_) {
            players.push_back(player);
        }
        
        return players;
    }
    
    std::vector<PlayerPtr> get_online_players() const {
        std::lock_guard<std::mutex> lock(players_mutex_);
        std::vector<PlayerPtr> online_players;
        
        for (const auto& [uuid, player] : players_by_uuid_) {
            if (player->is_online()) {
                online_players.push_back(player);
            }
        }
        
        return online_players;
    }
    
    size_t get_player_count() const {
        std::lock_guard<std::mutex> lock(players_mutex_);
        return players_by_uuid_.size();
    }
    
    size_t get_online_count() const {
        std::lock_guard<std::mutex> lock(players_mutex_);
        size_t count = 0;
        
        for (const auto& [uuid, player] : players_by_uuid_) {
            if (player->is_online()) {
                count++;
            }
        }
        
        return count;
    }
    
    std::vector<PlayerPtr> get_players_in_range(const Location& center, f64 radius) const {
        std::lock_guard<std::mutex> lock(players_mutex_);
        std::vector<PlayerPtr> nearby_players;
        
        for (const auto& [uuid, player] : players_by_uuid_) {
            if (player->is_online() && player->distance_to(center) <= radius) {
                nearby_players.push_back(player);
            }
        }
        
        return nearby_players;
    }
    
    void update_all_chunks() {
        auto players = get_online_players();
        for (auto& player : players) {
            player->update_loaded_chunks();
        }
    }
    
    void cleanup_offline_players() {
        std::vector<UUID> to_remove;
        
        {
            std::lock_guard<std::mutex> lock(players_mutex_);
            for (const auto& [uuid, player] : players_by_uuid_) {
                if (!player->is_online()) {
                    auto last_activity = player->get_last_activity();
                    auto now = std::chrono::steady_clock::now();
                    auto offline_time = std::chrono::duration_cast<std::chrono::minutes>(
                        now - last_activity).count();
                    
                    if (offline_time > 10) {
                        to_remove.push_back(uuid);
                    }
                }
            }
        }
        
        for (const auto& uuid : to_remove) {
            remove_player(uuid);
        }
    }
};

extern PlayerManager g_player_manager;

}
