#pragma once

#include <cstdint>
#include <string>
#include <chrono>
#include <array>

namespace mc {

using byte = std::uint8_t;
using i8 = std::int8_t;
using i16 = std::int16_t;
using i32 = std::int32_t;
using i64 = std::int64_t;
using u8 = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;
using f32 = float;
using f64 = double;

using timestamp_t = std::chrono::steady_clock::time_point;
using duration_t = std::chrono::steady_clock::duration;

struct Position {
    i32 x, y, z;
    
    Position() : x(0), y(0), z(0) {}
    Position(i32 x, i32 y, i32 z) : x(x), y(y), z(z) {}
    
    bool operator==(const Position& other) const {
        return x == other.x && y == other.y && z == other.z;
    }
    
    Position operator+(const Position& other) const {
        return Position(x + other.x, y + other.y, z + other.z);
    }
};

struct ChunkPos {
    i32 x, z;
    
    ChunkPos() : x(0), z(0) {}
    ChunkPos(i32 x, i32 z) : x(x), z(z) {}
    
    bool operator==(const ChunkPos& other) const {
        return x == other.x && z == other.z;
    }
    
    bool operator<(const ChunkPos& other) const {
        return x < other.x || (x == other.x && z < other.z);
    }
};

struct Location {
    f64 x, y, z;
    f32 yaw, pitch;
    
    Location() : x(0), y(0), z(0), yaw(0), pitch(0) {}
    Location(f64 x, f64 y, f64 z, f32 yaw = 0, f32 pitch = 0) 
        : x(x), y(y), z(z), yaw(yaw), pitch(pitch) {}
        
    Position toBlockPos() const {
        return Position(static_cast<i32>(x), static_cast<i32>(y), static_cast<i32>(z));
    }
    
    ChunkPos toChunkPos() const {
        return ChunkPos(static_cast<i32>(x) >> 4, static_cast<i32>(z) >> 4);
    }
};

using UUID = std::array<byte, 16>;

struct GameProfile {
    UUID uuid;
    std::string username;
    std::string display_name;
    
    GameProfile() = default;
    GameProfile(const UUID& uuid, const std::string& username) 
        : uuid(uuid), username(username), display_name(username) {}
};

enum class GameMode : i32 {
    SURVIVAL = 0,
    CREATIVE = 1,
    ADVENTURE = 2,
    SPECTATOR = 3
};

enum class Difficulty : byte {
    PEACEFUL = 0,
    EASY = 1,
    NORMAL = 2,
    HARD = 3
};

constexpr i32 MINECRAFT_PROTOCOL_VERSION = 763;
constexpr const char* MINECRAFT_VERSION = "1.20.1";

}
