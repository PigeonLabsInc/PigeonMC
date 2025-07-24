#pragma once

#include "packet_types.hpp"
#include "world/chunk.hpp"
#include "core/buffer.hpp"
#include <vector>

namespace mc::network::play {

class ChunkDataPacket : public Packet {
public:
    i32 chunk_x;
    i32 chunk_z;
    std::vector<u8> chunk_data;
    std::vector<u64> block_entities;
    
    ChunkDataPacket() : chunk_x(0), chunk_z(0) {}
    ChunkDataPacket(i32 x, i32 z) : chunk_x(x), chunk_z(z) {}
    
    i32 get_id() const override { return 0x24; }
    ConnectionState get_state() const override { return ConnectionState::PLAY; }
    PacketDirection get_direction() const override { return PacketDirection::CLIENTBOUND; }
    
    void write(Buffer& buffer) const override {
        buffer.write_be<i32>(chunk_x);
        buffer.write_be<i32>(chunk_z);
        
        buffer.write_varint(static_cast<i32>(chunk_data.size()));
        buffer.write(chunk_data.data(), chunk_data.size());
        
        buffer.write_varint(static_cast<i32>(block_entities.size()));
        for (u64 be : block_entities) {
            buffer.write_be<u64>(be);
        }
    }
    
    void read(Buffer& buffer) override {
        chunk_x = buffer.read_be<i32>();
        chunk_z = buffer.read_be<i32>();
        
        i32 data_size = buffer.read_varint();
        chunk_data.resize(data_size);
        buffer.read(chunk_data.data(), data_size);
        
        i32 be_count = buffer.read_varint();
        block_entities.resize(be_count);
        for (i32 i = 0; i < be_count; ++i) {
            block_entities[i] = buffer.read_be<u64>();
        }
    }
    
    void serialize_chunk(const world::ChunkPtr& chunk) {
        if (!chunk) return;
        
        Buffer temp_buffer(65536);
        
        auto sections = chunk->get_sections();
        for (size_t i = 0; i < sections.size(); ++i) {
            const auto* section = sections[i];
            
            if (!section || section->is_empty()) {
                temp_buffer.write_be<i16>(0);
                continue;
            }
            
            temp_buffer.write_be<i16>(section->block_count);
            
            serialize_palette(temp_buffer, section);
            serialize_blocks(temp_buffer, section);
            serialize_lighting(temp_buffer, section);
        }
        
        serialize_biomes(temp_buffer);
        
        chunk_data.resize(temp_buffer.size());
        std::memcpy(chunk_data.data(), temp_buffer.data(), temp_buffer.size());
    }
    
private:
    void serialize_palette(Buffer& buffer, const world::ChunkSection* section) const {
        buffer.write_byte(15);
        buffer.write_varint(0);
    }
    
    void serialize_blocks(Buffer& buffer, const world::ChunkSection* section) const {
        constexpr size_t blocks_per_section = 16 * 16 * 16;
        
        buffer.write_varint(static_cast<i32>(blocks_per_section));
        
        for (size_t i = 0; i < blocks_per_section; ++i) {
            i32 y = (i / 256) % 16;
            i32 z = (i / 16) % 16;
            i32 x = i % 16;
            
            auto block = section->get_block(x, y, z);
            buffer.write_be<u64>(static_cast<u64>(block.id));
        }
    }
    
    void serialize_lighting(Buffer& buffer, const world::ChunkSection* section) const {
        buffer.write(section->sky_light, sizeof(section->sky_light));
        buffer.write(section->block_light, sizeof(section->block_light));
    }
    
    void serialize_biomes(Buffer& buffer) const {
        for (i32 i = 0; i < 1024; ++i) {
            buffer.write_varint(1);
        }
    }
};

class UnloadChunkPacket : public Packet {
public:
    i32 chunk_x;
    i32 chunk_z;
    
    UnloadChunkPacket() : chunk_x(0), chunk_z(0) {}
    UnloadChunkPacket(i32 x, i32 z) : chunk_x(x), chunk_z(z) {}
    
    i32 get_id() const override { return 0x1D; }
    ConnectionState get_state() const override { return ConnectionState::PLAY; }
    PacketDirection get_direction() const override { return PacketDirection::CLIENTBOUND; }
    
    void write(Buffer& buffer) const override {
        buffer.write_be<i32>(chunk_x);
        buffer.write_be<i32>(chunk_z);
    }
    
    void read(Buffer& buffer) override {
        chunk_x = buffer.read_be<i32>();
        chunk_z = buffer.read_be<i32>();
    }
};

class UpdateViewPositionPacket : public Packet {
public:
    i32 chunk_x;
    i32 chunk_z;
    
    UpdateViewPositionPacket() : chunk_x(0), chunk_z(0) {}
    UpdateViewPositionPacket(i32 x, i32 z) : chunk_x(x), chunk_z(z) {}
    
    i32 get_id() const override { return 0x4E; }
    ConnectionState get_state() const override { return ConnectionState::PLAY; }
    PacketDirection get_direction() const override { return PacketDirection::CLIENTBOUND; }
    
    void write(Buffer& buffer) const override {
        buffer.write_varint(chunk_x);
        buffer.write_varint(chunk_z);
    }
    
    void read(Buffer& buffer) override {
        chunk_x = buffer.read_varint();
        chunk_z = buffer.read_varint();
    }
};

class PlayerPositionAndLookPacket : public Packet {
public:
    f64 x, y, z;
    f32 yaw, pitch;
    u8 flags;
    i32 teleport_id;
    bool dismount_vehicle;
    
    PlayerPositionAndLookPacket() : x(0), y(0), z(0), yaw(0), pitch(0)
        , flags(0), teleport_id(0), dismount_vehicle(false) {}
    
    i32 get_id() const override { return 0x3C; }
    ConnectionState get_state() const override { return ConnectionState::PLAY; }
    PacketDirection get_direction() const override { return PacketDirection::CLIENTBOUND; }
    
    void write(Buffer& buffer) const override {
        buffer.write_be<f64>(x);
        buffer.write_be<f64>(y);
        buffer.write_be<f64>(z);
        buffer.write_be<f32>(yaw);
        buffer.write_be<f32>(pitch);
        buffer.write_byte(flags);
        buffer.write_varint(teleport_id);
        buffer.write_byte(dismount_vehicle ? 1 : 0);
    }
    
    void read(Buffer& buffer) override {
        x = buffer.read_be<f64>();
        y = buffer.read_be<f64>();
        z = buffer.read_be<f64>();
        yaw = buffer.read_be<f32>();
        pitch = buffer.read_be<f32>();
        flags = buffer.read_byte();
        teleport_id = buffer.read_varint();
        dismount_vehicle = buffer.read_byte() != 0;
    }
};

class BlockChangePacket : public Packet {
public:
    Position position;
    u32 block_state;
    
    BlockChangePacket() : block_state(0) {}
    BlockChangePacket(const Position& pos, u32 state) : position(pos), block_state(state) {}
    
    i32 get_id() const override { return 0x0C; }
    ConnectionState get_state() const override { return ConnectionState::PLAY; }
    PacketDirection get_direction() const override { return PacketDirection::CLIENTBOUND; }
    
    void write(Buffer& buffer) const override {
        u64 encoded_pos = static_cast<u64>(position.x & 0x3FFFFFF) << 38 |
                         static_cast<u64>(position.z & 0x3FFFFFF) << 12 |
                         static_cast<u64>(position.y & 0xFFF);
        buffer.write_be<u64>(encoded_pos);
        buffer.write_varint(static_cast<i32>(block_state));
    }
    
    void read(Buffer& buffer) override {
        u64 encoded_pos = buffer.read_be<u64>();
        position.x = static_cast<i32>(encoded_pos >> 38);
        position.z = static_cast<i32>((encoded_pos >> 12) & 0x3FFFFFF);
        position.y = static_cast<i32>(encoded_pos & 0xFFF);
        
        if (position.x >= 0x2000000) position.x -= 0x4000000;
        if (position.z >= 0x2000000) position.z -= 0x4000000;
        if (position.y >= 0x800) position.y -= 0x1000;
        
        block_state = static_cast<u32>(buffer.read_varint());
    }
};

class MultiBlockChangePacket : public Packet {
public:
    struct BlockChange {
        Position position;
        u32 block_state;
    };
    
    world::ChunkPos chunk_pos;
    std::vector<BlockChange> changes;
    
    MultiBlockChangePacket() = default;
    MultiBlockChangePacket(const world::ChunkPos& pos) : chunk_pos(pos) {}
    
    i32 get_id() const override { return 0x10; }
    ConnectionState get_state() const override { return ConnectionState::PLAY; }
    PacketDirection get_direction() const override { return PacketDirection::CLIENTBOUND; }
    
    void write(Buffer& buffer) const override {
        u64 chunk_coord = static_cast<u64>(chunk_pos.x & 0x3FFFFF) << 42 |
                         static_cast<u64>(chunk_pos.z & 0x3FFFFF) << 20;
        buffer.write_be<u64>(chunk_coord);
        
        buffer.write_varint(static_cast<i32>(changes.size()));
        
        for (const auto& change : changes) {
            u64 encoded = static_cast<u64>(change.position.x & 0xF) << 8 |
                         static_cast<u64>(change.position.z & 0xF) << 4 |
                         static_cast<u64>(change.position.y & 0xF);
            buffer.write_varint(static_cast<i32>(encoded));
            buffer.write_varint(static_cast<i32>(change.block_state));
        }
    }
    
    void read(Buffer& buffer) override {
        u64 chunk_coord = buffer.read_be<u64>();
        chunk_pos.x = static_cast<i32>(chunk_coord >> 42);
        chunk_pos.z = static_cast<i32>((chunk_coord >> 20) & 0x3FFFFF);
        
        if (chunk_pos.x >= 0x200000) chunk_pos.x -= 0x400000;
        if (chunk_pos.z >= 0x200000) chunk_pos.z -= 0x400000;
        
        i32 count = buffer.read_varint();
        changes.resize(count);
        
        for (i32 i = 0; i < count; ++i) {
            i32 encoded = buffer.read_varint();
            changes[i].position.x = (encoded >> 8) & 0xF;
            changes[i].position.z = (encoded >> 4) & 0xF;
            changes[i].position.y = encoded & 0xF;
            changes[i].block_state = static_cast<u32>(buffer.read_varint());
        }
    }
    
    void add_change(const Position& pos, u32 block_state) {
        changes.push_back({pos, block_state});
    }
};

}
