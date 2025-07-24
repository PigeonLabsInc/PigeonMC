#pragma once

#include "block.hpp"
#include "core/types.hpp"
#include "core/thread_pool.hpp"
#include <array>
#include <memory>
#include <atomic>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <functional>

namespace mc::world {

constexpr i32 SECTIONS_PER_CHUNK = (WORLD_MAX_Y - WORLD_MIN_Y) / 16;

class Chunk {
private:
    ChunkPos position_;
    std::array<std::unique_ptr<ChunkSection>, SECTIONS_PER_CHUNK> sections_;
    std::atomic<bool> loaded_{false};
    std::atomic<bool> dirty_{false};
    std::atomic<timestamp_t> last_access_;
    std::mutex sections_mutex_;
    
    i32 get_section_index(i32 y) const {
        return (y - WORLD_MIN_Y) / 16;
    }
    
    ChunkSection* get_or_create_section(i32 section_idx) {
        if (section_idx < 0 || section_idx >= SECTIONS_PER_CHUNK) return nullptr;
        
        if (!sections_[section_idx]) {
            sections_[section_idx] = std::make_unique<ChunkSection>();
        }
        return sections_[section_idx].get();
    }

public:
    explicit Chunk(const ChunkPos& pos) : position_(pos) {
        last_access_.store(std::chrono::steady_clock::now());
    }
    
    const ChunkPos& get_position() const { return position_; }
    
    Block get_block(i32 x, i32 y, i32 z) const {
        std::lock_guard<std::mutex> lock(sections_mutex_);
        
        i32 section_idx = get_section_index(y);
        if (section_idx < 0 || section_idx >= SECTIONS_PER_CHUNK) return Block();
        
        const auto& section = sections_[section_idx];
        if (!section) return Block();
        
        i32 local_y = y - (section_idx * 16 + WORLD_MIN_Y);
        return section->get_block(x, local_y, z);
    }
    
    void set_block(i32 x, i32 y, i32 z, const Block& block) {
        std::lock_guard<std::mutex> lock(sections_mutex_);
        
        i32 section_idx = get_section_index(y);
        ChunkSection* section = get_or_create_section(section_idx);
        if (!section) return;
        
        i32 local_y = y - (section_idx * 16 + WORLD_MIN_Y);
        section->set_block(x, local_y, z, block);
        
        dirty_.store(true);
        last_access_.store(std::chrono::steady_clock::now());
    }
    
    u8 get_block_light(i32 x, i32 y, i32 z) const {
        std::lock_guard<std::mutex> lock(sections_mutex_);
        
        i32 section_idx = get_section_index(y);
        if (section_idx < 0 || section_idx >= SECTIONS_PER_CHUNK) return 0;
        
        const auto& section = sections_[section_idx];
        if (!section) return 0;
        
        i32 local_y = y - (section_idx * 16 + WORLD_MIN_Y);
        return section->get_block_light(x, local_y, z);
    }
    
    void set_block_light(i32 x, i32 y, i32 z, u8 light) {
        std::lock_guard<std::mutex> lock(sections_mutex_);
        
        i32 section_idx = get_section_index(y);
        ChunkSection* section = get_or_create_section(section_idx);
        if (!section) return;
        
        i32 local_y = y - (section_idx * 16 + WORLD_MIN_Y);
        section->set_block_light(x, local_y, z, light);
        dirty_.store(true);
    }
    
    u8 get_sky_light(i32 x, i32 y, i32 z) const {
        std::lock_guard<std::mutex> lock(sections_mutex_);
        
        i32 section_idx = get_section_index(y);
        if (section_idx < 0 || section_idx >= SECTIONS_PER_CHUNK) return 0;
        
        const auto& section = sections_[section_idx];
        if (!section) return 15;
        
        i32 local_y = y - (section_idx * 16 + WORLD_MIN_Y);
        return section->get_sky_light(x, local_y, z);
    }
    
    void set_sky_light(i32 x, i32 y, i32 z, u8 light) {
        std::lock_guard<std::mutex> lock(sections_mutex_);
        
        i32 section_idx = get_section_index(y);
        ChunkSection* section = get_or_create_section(section_idx);
        if (!section) return;
        
        i32 local_y = y - (section_idx * 16 + WORLD_MIN_Y);
        section->set_sky_light(x, local_y, z, light);
        dirty_.store(true);
    }
    
    bool is_loaded() const { return loaded_.load(); }
    void set_loaded(bool loaded) { loaded_.store(loaded); }
    
    bool is_dirty() const { return dirty_.load(); }
    void set_dirty(bool dirty) { dirty_.store(dirty); }
    
    timestamp_t get_last_access() const { return last_access_.load(); }
    void touch() { last_access_.store(std::chrono::steady_clock::now()); }
    
    std::vector<const ChunkSection*> get_sections() const {
        std::lock_guard<std::mutex> lock(sections_mutex_);
        std::vector<const ChunkSection*> result;
        
        for (const auto& section : sections_) {
            result.push_back(section.get());
        }
        
        return result;
    }
    
    void generate_flat_world() {
        std::lock_guard<std::mutex> lock(sections_mutex_);
        
        for (i32 x = 0; x < CHUNK_SIZE; ++x) {
            for (i32 z = 0; z < CHUNK_SIZE; ++z) {
                set_block(x, WORLD_MIN_Y, z, Block(BEDROCK));
                
                for (i32 y = WORLD_MIN_Y + 1; y <= 60; ++y) {
                    set_block(x, y, z, Block(STONE));
                }
                
                for (i32 y = 61; y <= 63; ++y) {
                    set_block(x, y, z, Block(DIRT));
                }
                
                set_block(x, 64, z, Block(GRASS_BLOCK));
            }
        }
        
        loaded_.store(true);
        dirty_.store(true);
    }
};

using ChunkPtr = std::shared_ptr<Chunk>;

struct ChunkPosHash {
    size_t operator()(const ChunkPos& pos) const {
        return std::hash<i64>{}(static_cast<i64>(pos.x) << 32 | static_cast<u32>(pos.z));
    }
};

class ChunkManager {
private:
    std::unordered_map<ChunkPos, ChunkPtr, ChunkPosHash> loaded_chunks_;
    std::unordered_set<ChunkPos, ChunkPosHash> pending_chunks_;
    std::mutex chunk_mutex_;
    
    std::atomic<size_t> max_loaded_chunks_{256};
    std::atomic<bool> auto_unload_enabled_{true};
    std::atomic<i64> chunk_timeout_ms_{300000};
    
    void cleanup_old_chunks() {
        if (!auto_unload_enabled_.load()) return;
        
        auto now = std::chrono::steady_clock::now();
        std::vector<ChunkPos> to_unload;
        
        {
            std::lock_guard<std::mutex> lock(chunk_mutex_);
            
            if (loaded_chunks_.size() <= max_loaded_chunks_.load()) return;
            
            for (const auto& [pos, chunk] : loaded_chunks_) {
                auto age = std::chrono::duration_cast<std::chrono::milliseconds>(
                    now - chunk->get_last_access()).count();
                
                if (age > chunk_timeout_ms_.load()) {
                    to_unload.push_back(pos);
                }
            }
        }
        
        for (const auto& pos : to_unload) {
            unload_chunk(pos);
        }
    }

public:
    ChunkManager() = default;
    
    ChunkPtr get_chunk(const ChunkPos& pos) {
        std::lock_guard<std::mutex> lock(chunk_mutex_);
        
        auto it = loaded_chunks_.find(pos);
        if (it != loaded_chunks_.end()) {
            it->second->touch();
            return it->second;
        }
        
        return nullptr;
    }
    
    ChunkPtr load_chunk(const ChunkPos& pos) {
        {
            std::lock_guard<std::mutex> lock(chunk_mutex_);
            
            auto it = loaded_chunks_.find(pos);
            if (it != loaded_chunks_.end()) {
                it->second->touch();
                return it->second;
            }
            
            if (pending_chunks_.find(pos) != pending_chunks_.end()) {
                return nullptr;
            }
            
            pending_chunks_.insert(pos);
        }
        
        g_thread_pool.submit([this, pos]() {
            auto chunk = std::make_shared<Chunk>(pos);
            chunk->generate_flat_world();
            
            {
                std::lock_guard<std::mutex> lock(chunk_mutex_);
                loaded_chunks_[pos] = chunk;
                pending_chunks_.erase(pos);
            }
            
            cleanup_old_chunks();
        });
        
        return nullptr;
    }
    
    void unload_chunk(const ChunkPos& pos) {
        ChunkPtr chunk_to_save;
        
        {
            std::lock_guard<std::mutex> lock(chunk_mutex_);
            auto it = loaded_chunks_.find(pos);
            if (it != loaded_chunks_.end()) {
                chunk_to_save = it->second;
                loaded_chunks_.erase(it);
            }
        }
        
        if (chunk_to_save && chunk_to_save->is_dirty()) {
            g_thread_pool.submit([chunk_to_save]() {
                
            });
        }
    }
    
    Block get_block(const Position& pos) {
        ChunkPos chunk_pos(pos.x >> 4, pos.z >> 4);
        auto chunk = get_chunk(chunk_pos);
        if (!chunk) return Block();
        
        i32 local_x = pos.x & 15;
        i32 local_z = pos.z & 15;
        return chunk->get_block(local_x, pos.y, local_z);
    }
    
    void set_block(const Position& pos, const Block& block) {
        ChunkPos chunk_pos(pos.x >> 4, pos.z >> 4);
        auto chunk = get_chunk(chunk_pos);
        if (!chunk) {
            chunk = load_chunk(chunk_pos);
            if (!chunk) return;
        }
        
        i32 local_x = pos.x & 15;
        i32 local_z = pos.z & 15;
        chunk->set_block(local_x, pos.y, local_z, block);
    }
    
    std::vector<ChunkPtr> get_chunks_in_range(const ChunkPos& center, i32 radius) {
        std::vector<ChunkPtr> result;
        std::lock_guard<std::mutex> lock(chunk_mutex_);
        
        for (i32 dx = -radius; dx <= radius; ++dx) {
            for (i32 dz = -radius; dz <= radius; ++dz) {
                ChunkPos pos(center.x + dx, center.z + dz);
                auto it = loaded_chunks_.find(pos);
                if (it != loaded_chunks_.end()) {
                    result.push_back(it->second);
                }
            }
        }
        
        return result;
    }
    
    void load_chunks_around(const ChunkPos& center, i32 radius) {
        for (i32 dx = -radius; dx <= radius; ++dx) {
            for (i32 dz = -radius; dz <= radius; ++dz) {
                ChunkPos pos(center.x + dx, center.z + dz);
                if (!get_chunk(pos)) {
                    load_chunk(pos);
                }
            }
        }
    }
    
    size_t get_loaded_chunk_count() const {
        std::lock_guard<std::mutex> lock(chunk_mutex_);
        return loaded_chunks_.size();
    }
    
    size_t get_pending_chunk_count() const {
        std::lock_guard<std::mutex> lock(chunk_mutex_);
        return pending_chunks_.size();
    }
    
    void set_max_loaded_chunks(size_t max_chunks) {
        max_loaded_chunks_.store(max_chunks);
    }
    
    void set_auto_unload(bool enabled) {
        auto_unload_enabled_.store(enabled);
    }
    
    void set_chunk_timeout(i64 timeout_ms) {
        chunk_timeout_ms_.store(timeout_ms);
    }
};

extern ChunkManager g_chunk_manager;

}
