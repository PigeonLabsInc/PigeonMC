#pragma once

#include "core/types.hpp"
#include <unordered_map>
#include <string>
#include <vector>

namespace mc::world {

using BlockId = u16;
using BlockState = u32;

constexpr BlockId AIR = 0;
constexpr BlockId STONE = 1;
constexpr BlockId GRASS_BLOCK = 2;
constexpr BlockId DIRT = 3;
constexpr BlockId COBBLESTONE = 4;
constexpr BlockId BEDROCK = 7;
constexpr BlockId WATER = 8;
constexpr BlockId LAVA = 10;

struct BlockInfo {
    BlockId id;
    std::string name;
    bool solid;
    bool transparent;
    f32 hardness;
    f32 resistance;
    u8 light_level;
    bool collidable;
    
    BlockInfo() : id(0), solid(false), transparent(true), hardness(0), 
                  resistance(0), light_level(0), collidable(false) {}
                  
    BlockInfo(BlockId id, const std::string& name, bool solid = true, 
              bool transparent = false, f32 hardness = 1.0f, f32 resistance = 1.0f,
              u8 light_level = 0, bool collidable = true)
        : id(id), name(name), solid(solid), transparent(transparent)
        , hardness(hardness), resistance(resistance), light_level(light_level)
        , collidable(collidable) {}
};

class BlockRegistry {
private:
    std::unordered_map<BlockId, BlockInfo> blocks_;
    std::unordered_map<std::string, BlockId> name_to_id_;
    
public:
    BlockRegistry() {
        register_default_blocks();
    }
    
    void register_block(const BlockInfo& info) {
        blocks_[info.id] = info;
        name_to_id_[info.name] = info.id;
    }
    
    const BlockInfo* get_block_info(BlockId id) const {
        auto it = blocks_.find(id);
        return it != blocks_.end() ? &it->second : nullptr;
    }
    
    BlockId get_block_id(const std::string& name) const {
        auto it = name_to_id_.find(name);
        return it != name_to_id_.end() ? it->second : AIR;
    }
    
    bool is_valid_block(BlockId id) const {
        return blocks_.find(id) != blocks_.end();
    }
    
private:
    void register_default_blocks() {
        register_block(BlockInfo(AIR, "minecraft:air", false, true, 0, 0, 0, false));
        register_block(BlockInfo(STONE, "minecraft:stone", true, false, 1.5f, 6.0f));
        register_block(BlockInfo(GRASS_BLOCK, "minecraft:grass_block", true, false, 0.6f, 0.6f));
        register_block(BlockInfo(DIRT, "minecraft:dirt", true, false, 0.5f, 0.5f));
        register_block(BlockInfo(COBBLESTONE, "minecraft:cobblestone", true, false, 2.0f, 6.0f));
        register_block(BlockInfo(BEDROCK, "minecraft:bedrock", true, false, -1.0f, 3600000.0f));
        register_block(BlockInfo(WATER, "minecraft:water", false, true, 100.0f, 100.0f, 0, false));
        register_block(BlockInfo(LAVA, "minecraft:lava", false, true, 100.0f, 100.0f, 15, false));
    }
};

extern BlockRegistry g_block_registry;

struct Block {
    BlockId id;
    
    Block() : id(AIR) {}
    explicit Block(BlockId block_id) : id(block_id) {}
    
    bool is_air() const { return id == AIR; }
    bool is_solid() const {
        const BlockInfo* info = g_block_registry.get_block_info(id);
        return info ? info->solid : false;
    }
    
    bool is_transparent() const {
        const BlockInfo* info = g_block_registry.get_block_info(id);
        return info ? info->transparent : true;
    }
    
    u8 get_light_level() const {
        const BlockInfo* info = g_block_registry.get_block_info(id);
        return info ? info->light_level : 0;
    }
    
    const BlockInfo* get_info() const {
        return g_block_registry.get_block_info(id);
    }
};

constexpr i32 CHUNK_SIZE = 16;
constexpr i32 CHUNK_HEIGHT = 384;
constexpr i32 WORLD_MIN_Y = -64;
constexpr i32 WORLD_MAX_Y = 320;

struct ChunkSection {
    static constexpr i32 SECTION_SIZE = 16;
    static constexpr i32 BLOCKS_PER_SECTION = SECTION_SIZE * SECTION_SIZE * SECTION_SIZE;
    
    std::vector<Block> blocks;
    u8 block_light[BLOCKS_PER_SECTION / 2];
    u8 sky_light[BLOCKS_PER_SECTION / 2];
    i16 block_count;
    
    ChunkSection() : blocks(BLOCKS_PER_SECTION), block_count(0) {
        std::fill(std::begin(block_light), std::end(block_light), 0);
        std::fill(std::begin(sky_light), std::end(sky_light), 0xFF);
    }
    
    Block get_block(i32 x, i32 y, i32 z) const {
        i32 index = (y * SECTION_SIZE + z) * SECTION_SIZE + x;
        return index >= 0 && index < BLOCKS_PER_SECTION ? blocks[index] : Block();
    }
    
    void set_block(i32 x, i32 y, i32 z, const Block& block) {
        i32 index = (y * SECTION_SIZE + z) * SECTION_SIZE + x;
        if (index >= 0 && index < BLOCKS_PER_SECTION) {
            Block old_block = blocks[index];
            blocks[index] = block;
            
            if (old_block.is_air() && !block.is_air()) {
                block_count++;
            } else if (!old_block.is_air() && block.is_air()) {
                block_count--;
            }
        }
    }
    
    u8 get_block_light(i32 x, i32 y, i32 z) const {
        i32 index = (y * SECTION_SIZE + z) * SECTION_SIZE + x;
        if (index < 0 || index >= BLOCKS_PER_SECTION) return 0;
        
        i32 byte_idx = index / 2;
        bool upper = (index % 2) == 1;
        return upper ? (block_light[byte_idx] >> 4) : (block_light[byte_idx] & 0xF);
    }
    
    void set_block_light(i32 x, i32 y, i32 z, u8 light) {
        i32 index = (y * SECTION_SIZE + z) * SECTION_SIZE + x;
        if (index < 0 || index >= BLOCKS_PER_SECTION) return;
        
        i32 byte_idx = index / 2;
        bool upper = (index % 2) == 1;
        light &= 0xF;
        
        if (upper) {
            block_light[byte_idx] = (block_light[byte_idx] & 0x0F) | (light << 4);
        } else {
            block_light[byte_idx] = (block_light[byte_idx] & 0xF0) | light;
        }
    }
    
    u8 get_sky_light(i32 x, i32 y, i32 z) const {
        i32 index = (y * SECTION_SIZE + z) * SECTION_SIZE + x;
        if (index < 0 || index >= BLOCKS_PER_SECTION) return 0;
        
        i32 byte_idx = index / 2;
        bool upper = (index % 2) == 1;
        return upper ? (sky_light[byte_idx] >> 4) : (sky_light[byte_idx] & 0xF);
    }
    
    void set_sky_light(i32 x, i32 y, i32 z, u8 light) {
        i32 index = (y * SECTION_SIZE + z) * SECTION_SIZE + x;
        if (index < 0 || index >= BLOCKS_PER_SECTION) return;
        
        i32 byte_idx = index / 2;
        bool upper = (index % 2) == 1;
        light &= 0xF;
        
        if (upper) {
            sky_light[byte_idx] = (sky_light[byte_idx] & 0x0F) | (light << 4);
        } else {
            sky_light[byte_idx] = (sky_light[byte_idx] & 0xF0) | light;
        }
    }
    
    bool is_empty() const {
        return block_count == 0;
    }
};

}
