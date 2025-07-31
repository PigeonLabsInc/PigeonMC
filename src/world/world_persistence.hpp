#pragma once

#include "chunk.hpp"
#include "core/buffer.hpp"
#include "core/thread_pool.hpp"
#include <filesystem>
#include <fstream>
#include <future>

namespace mc::world {

class WorldPersistence {
private:
    std::string world_directory_;
    std::string region_directory_;
    std::mutex save_mutex_;
    
    struct RegionFile {
        std::fstream file;
        std::array<u32, 1024> locations;
        std::array<u32, 1024> timestamps;
        bool dirty;
        
        RegionFile() : dirty(false) {
            locations.fill(0);
            timestamps.fill(0);
        }
    };
    
    std::unordered_map<std::pair<i32, i32>, std::unique_ptr<RegionFile>, 
                       PairHash<i32, i32>> region_files_;
    
    template<typename T1, typename T2>
    struct PairHash {
        size_t operator()(const std::pair<T1, T2>& p) const {
            auto h1 = std::hash<T1>{}(p.first);
            auto h2 = std::hash<T2>{}(p.second);
            return h1 ^ (h2 << 1);
        }
    };
    
    std::pair<i32, i32> get_region_coords(const ChunkPos& chunk_pos) const {
        return {chunk_pos.x >> 5, chunk_pos.z >> 5};
    }
    
    std::pair<i32, i32> get_local_chunk_coords(const ChunkPos& chunk_pos) const {
        return {chunk_pos.x & 31, chunk_pos.z & 31};
    }
    
    std::string get_region_filename(i32 region_x, i32 region_z) const {
        return "r." + std::to_string(region_x) + "." + std::to_string(region_z) + ".mca";
    }
    
    RegionFile* get_region_file(i32 region_x, i32 region_z) {
        auto key = std::make_pair(region_x, region_z);
        auto it = region_files_.find(key);
        
        if (it != region_files_.end()) {
            return it->second.get();
        }
        
        auto region_file = std::make_unique<RegionFile>();
        std::string filename = region_directory_ + "/" + get_region_filename(region_x, region_z);
        
        region_file->file.open(filename, std::ios::in | std::ios::out | std::ios::binary);
        
        if (!region_file->file.is_open()) {
            region_file->file.open(filename, std::ios::out | std::ios::binary);
            region_file->file.close();
            region_file->file.open(filename, std::ios::in | std::ios::out | std::ios::binary);
        }
        
        if (region_file->file.is_open()) {
            load_region_header(*region_file);
        }
        
        RegionFile* result = region_file.get();
        region_files_[key] = std::move(region_file);
        
        return result;
    }
    
    void load_region_header(RegionFile& region_file) {
        region_file.file.seekg(0);
        
        for (i32 i = 0; i < 1024; ++i) {
            u32 location;
            region_file.file.read(reinterpret_cast<char*>(&location), sizeof(u32));
            region_file.locations[i] = be32toh(location);
        }
        
        for (i32 i = 0; i < 1024; ++i) {
            u32 timestamp;
            region_file.file.read(reinterpret_cast<char*>(&timestamp), sizeof(u32));
            region_file.timestamps[i] = be32toh(timestamp);
        }
    }
    
    void save_region_header(RegionFile& region_file) {
        region_file.file.seekp(0);
        
        for (i32 i = 0; i < 1024; ++i) {
            u32 location = htobe32(region_file.locations[i]);
            region_file.file.write(reinterpret_cast<const char*>(&location), sizeof(u32));
        }
        
        for (i32 i = 0; i < 1024; ++i) {
            u32 timestamp = htobe32(region_file.timestamps[i]);
            region_file.file.write(reinterpret_cast<const char*>(&timestamp), sizeof(u32));
        }
        
        region_file.file.flush();
    }
    
    u32 be32toh(u32 value) const {
        return ((value & 0xFF000000) >> 24) |
               ((value & 0x00FF0000) >> 8) |
               ((value & 0x0000FF00) << 8) |
               ((value & 0x000000FF) << 24);
    }
    
    u32 htobe32(u32 value) const {
        return be32toh(value);
    }

public:
    explicit WorldPersistence(const std::string& world_name = "world") 
        : world_directory_(world_name) {
        
        region_directory_ = world_directory_ + "/region";
        
        std::filesystem::create_directories(world_directory_);
        std::filesystem::create_directories(region_directory_);
    }
    
    ~WorldPersistence() {
        save_all_chunks();
        close_all_region_files();
    }
    
    bool save_chunk(ChunkPtr chunk) {
        if (!chunk || !chunk->is_dirty()) {
            return true;
        }
        
        std::lock_guard<std::mutex> lock(save_mutex_);
        
        try {
            auto [region_x, region_z] = get_region_coords(chunk->get_position());
            auto [local_x, local_z] = get_local_chunk_coords(chunk->get_position());
            
            RegionFile* region_file = get_region_file(region_x, region_z);
            if (!region_file || !region_file->file.is_open()) {
                return false;
            }
            
            Buffer chunk_data(65536);
            serialize_chunk(chunk, chunk_data);
            
            i32 chunk_index = local_z * 32 + local_x;
            u32 old_location = region_file->locations[chunk_index];
            
            region_file->file.seekp(0, std::ios::end);
            u32 sector_offset = static_cast<u32>(region_file->file.tellp()) / 4096;
            u32 sector_count = (chunk_data.size() + 4095) / 4096;
            
            region_file->file.write(reinterpret_cast<const char*>(chunk_data.data()), chunk_data.size());
            
            size_t padding = (4096 - (chunk_data.size() % 4096)) % 4096;
            if (padding > 0) {
                std::vector<char> padding_data(padding, 0);
                region_file->file.write(padding_data.data(), padding);
            }
            
            region_file->locations[chunk_index] = (sector_offset << 8) | (sector_count & 0xFF);
            region_file->timestamps[chunk_index] = static_cast<u32>(
                std::chrono::system_clock::now().time_since_epoch().count() / 1000000000);
            
            region_file->dirty = true;
            save_region_header(*region_file);
            
            chunk->set_dirty(false);
            
            return true;
            
        } catch (const std::exception& e) {
            LOG_ERROR("Failed to save chunk " + std::to_string(chunk->get_position().x) + 
                     ", " + std::to_string(chunk->get_position().z) + ": " + e.what());
            return false;
        }
    }
    
    ChunkPtr load_chunk(const ChunkPos& chunk_pos) {
        std::lock_guard<std::mutex> lock(save_mutex_);
        
        try {
            auto [region_x, region_z] = get_region_coords(chunk_pos);
            auto [local_x, local_z] = get_local_chunk_coords(chunk_pos);
            
            RegionFile* region_file = get_region_file(region_x, region_z);
            if (!region_file || !region_file->file.is_open()) {
                return nullptr;
            }
            
            i32 chunk_index = local_z * 32 + local_x;
            u32 location = region_file->locations[chunk_index];
            
            if (location == 0) {
                return nullptr;
            }
            
            u32 sector_offset = (location >> 8) & 0xFFFFFF;
            u32 sector_count = location & 0xFF;
            
            if (sector_offset == 0 || sector_count == 0) {
                return nullptr;
            }
            
            region_file->file.seekg(sector_offset * 4096);
            
            std::vector<u8> chunk_data(sector_count * 4096);
            region_file->file.read(reinterpret_cast<char*>(chunk_data.data()), chunk_data.size());
            
            Buffer buffer(chunk_data.data(), chunk_data.size());
            return deserialize_chunk(chunk_pos, buffer);
            
        } catch (const std::exception& e) {
            LOG_ERROR("Failed to load chunk " + std::to_string(chunk_pos.x) + 
                     ", " + std::to_string(chunk_pos.z) + ": " + e.what());
            return nullptr;
        }
    }
    
    std::future<bool> save_chunk_async(ChunkPtr chunk) {
        return g_thread_pool.submit([this, chunk]() {
            return save_chunk(chunk);
        });
    }
    
    std::future<ChunkPtr> load_chunk_async(const ChunkPos& chunk_pos) {
        return g_thread_pool.submit([this, chunk_pos]() {
            return load_chunk(chunk_pos);
        });
    }
    
    void save_all_chunks() {
        LOG_INFO("Saving all loaded chunks...");
        
        auto loaded_chunks = g_chunk_manager.get_loaded_chunk_count();
        size_t saved_count = 0;
        
        for (const auto& [pos, region_file] : region_files_) {
            if (region_file->dirty) {
                save_region_header(*region_file);
                region_file->dirty = false;
                saved_count++;
            }
        }
        
        LOG_INFO("Saved " + std::to_string(saved_count) + " region files");
    }
    
    void close_all_region_files() {
        std::lock_guard<std::mutex> lock(save_mutex_);
        
        for (auto& [pos, region_file] : region_files_) {
            if (region_file->file.is_open()) {
                if (region_file->dirty) {
                    save_region_header(*region_file);
                }
                region_file->file.close();
            }
        }
        
        region_files_.clear();
    }

private:
    void serialize_chunk(ChunkPtr chunk, Buffer& buffer) {
        auto sections = chunk->get_sections();
        
        buffer.write_be<i32>(static_cast<i32>(sections.size()));
        
        for (const auto* section : sections) {
            if (!section) {
                buffer.write_byte(0);
                continue;
            }
            
            buffer.write_byte(1);
            buffer.write_be<i16>(section->block_count);
            
            constexpr size_t blocks_per_section = 16 * 16 * 16;
            for (size_t i = 0; i < blocks_per_section; ++i) {
                i32 y = (i / 256) % 16;
                i32 z = (i / 16) % 16;
                i32 x = i % 16;
                
                auto block = section->get_block(x, y, z);
                buffer.write_be<u16>(block.id);
            }
            
            buffer.write(section->block_light, sizeof(section->block_light));
            buffer.write(section->sky_light, sizeof(section->sky_light));
        }
    }
    
    ChunkPtr deserialize_chunk(const ChunkPos& chunk_pos, Buffer& buffer) {
        auto chunk = std::make_shared<Chunk>(chunk_pos);
        
        i32 section_count = buffer.read_be<i32>();
        
        for (i32 s = 0; s < section_count; ++s) {
            u8 has_section = buffer.read_byte();
            if (!has_section) continue;
            
            i16 block_count = buffer.read_be<i16>();
            
            auto section = std::make_unique<ChunkSection>();
            section->block_count = block_count;
            
            constexpr size_t blocks_per_section = 16 * 16 * 16;
            for (size_t i = 0; i < blocks_per_section; ++i) {
                i32 y = (i / 256) % 16;
                i32 z = (i / 16) % 16;
                i32 x = i % 16;
                
                u16 block_id = buffer.read_be<u16>();
                section->set_block(x, y, z, Block(block_id));
            }
            
            buffer.read(section->block_light, sizeof(section->block_light));
            buffer.read(section->sky_light, sizeof(section->sky_light));
        }
        
        chunk->set_loaded(true);
        chunk->set_dirty(false);
        
        return chunk;
    }
};

extern WorldPersistence g_world_persistence;

}
