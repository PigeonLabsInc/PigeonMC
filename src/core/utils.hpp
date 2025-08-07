#pragma once

#include "types.hpp"
#include <string>
#include <vector>
#include <random>
#include <algorithm>
#include <sstream>
#include <iomanip>

namespace mc::utils {

class Random {
private:
    std::mt19937 generator_;
    
public:
    Random() : generator_(std::random_device{}()) {}
    explicit Random(u32 seed) : generator_(seed) {}
    
    i32 next_int() {
        return static_cast<i32>(generator_());
    }
    
    i32 next_int(i32 max) {
        std::uniform_int_distribution<i32> dist(0, max - 1);
        return dist(generator_);
    }
    
    i32 next_int(i32 min, i32 max) {
        std::uniform_int_distribution<i32> dist(min, max);
        return dist(generator_);
    }
    
    f32 next_float() {
        std::uniform_real_distribution<f32> dist(0.0f, 1.0f);
        return dist(generator_);
    }
    
    f64 next_double() {
        std::uniform_real_distribution<f64> dist(0.0, 1.0);
        return dist(generator_);
    }
    
    bool next_bool() {
        return generator_() % 2 == 0;
    }
};

inline std::string uuid_to_string(const UUID& uuid) {
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    
    for (size_t i = 0; i < 16; ++i) {
        if (i == 4 || i == 6 || i == 8 || i == 10) {
            oss << '-';
        }
        oss << std::setw(2) << static_cast<u32>(uuid[i]);
    }
    
    return oss.str();
}

inline UUID string_to_uuid(const std::string& str) {
    UUID uuid{};
    std::string clean = str;
    clean.erase(std::remove(clean.begin(), clean.end(), '-'), clean.end());
    
    if (clean.length() != 32) return uuid;
    
    for (size_t i = 0; i < 16; ++i) {
        std::string byte_str = clean.substr(i * 2, 2);
        uuid[i] = static_cast<u8>(std::stoul(byte_str, nullptr, 16));
    }
    
    return uuid;
}

inline UUID generate_offline_uuid(const std::string& username) {
    std::string offline_string = "OfflinePlayer:" + username;
    
    std::hash<std::string> hasher;
    u64 hash = hasher(offline_string);
    
    UUID uuid{};
    for (int i = 0; i < 8; ++i) {
        uuid[i] = static_cast<u8>(hash >> (i * 8));
        uuid[i + 8] = static_cast<u8>(hash >> (i * 8));
    }
    
    uuid[6] = (uuid[6] & 0x0F) | 0x30;
    uuid[8] = (uuid[8] & 0x3F) | 0x80;
    
    return uuid;
}

inline std::vector<std::string> split_string(const std::string& str, char delimiter) {
    std::vector<std::string> tokens;
    std::stringstream ss(str);
    std::string token;
    
    while (std::getline(ss, token, delimiter)) {
        if (!token.empty()) {
            tokens.push_back(token);
        }
    }
    
    return tokens;
}

inline std::string join_strings(const std::vector<std::string>& strings, const std::string& delimiter) {
    if (strings.empty()) return "";
    
    std::ostringstream oss;
    for (size_t i = 0; i < strings.size(); ++i) {
        if (i > 0) oss << delimiter;
        oss << strings[i];
    }
    
    return oss.str();
}

inline std::string to_lower(const std::string& str) {
    std::string result = str;
    std::transform(result.begin(), result.end(), result.begin(), ::tolower);
    return result;
}

inline std::string to_upper(const std::string& str) {
    std::string result = str;
    std::transform(result.begin(), result.end(), result.begin(), ::toupper);
    return result;
}

inline std::string trim(const std::string& str) {
    size_t start = str.find_first_not_of(" \t\n\r");
    if (start == std::string::npos) return "";
    
    size_t end = str.find_last_not_of(" \t\n\r");
    return str.substr(start, end - start + 1);
}

inline bool starts_with(const std::string& str, const std::string& prefix) {
    return str.length() >= prefix.length() && 
           str.substr(0, prefix.length()) == prefix;
}

inline bool ends_with(const std::string& str, const std::string& suffix) {
    return str.length() >= suffix.length() && 
           str.substr(str.length() - suffix.length()) == suffix;
}

inline std::string format_bytes(u64 bytes) {
    const char* units[] = {"B", "KB", "MB", "GB", "TB"};
    const size_t num_units = sizeof(units) / sizeof(units[0]);
    
    f64 size = static_cast<f64>(bytes);
    size_t unit = 0;
    
    while (size >= 1024.0 && unit < num_units - 1) {
        size /= 1024.0;
        unit++;
    }
    
    std::ostringstream oss;
    if (unit == 0) {
        oss << static_cast<u64>(size) << " " << units[unit];
    } else {
        oss << std::fixed << std::setprecision(2) << size << " " << units[unit];
    }
    
    return oss.str();
}

inline std::string format_duration(i64 seconds) {
    if (seconds < 60) {
        return std::to_string(seconds) + "s";
    } else if (seconds < 3600) {
        return std::to_string(seconds / 60) + "m " + std::to_string(seconds % 60) + "s";
    } else if (seconds < 86400) {
        i64 hours = seconds / 3600;
        i64 mins = (seconds % 3600) / 60;
        return std::to_string(hours) + "h " + std::to_string(mins) + "m";
    } else {
        i64 days = seconds / 86400;
        i64 hours = (seconds % 86400) / 3600;
        return std::to_string(days) + "d " + std::to_string(hours) + "h";
    }
}

inline i64 current_time_millis() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

inline i64 steady_time_millis() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

template<typename T>
constexpr T clamp(const T& value, const T& min_val, const T& max_val) {
    return (value < min_val) ? min_val : (value > max_val) ? max_val : value;
}

template<typename T>
constexpr T lerp(const T& a, const T& b, f32 t) {
    return a + static_cast<T>((b - a) * t);
}

inline f32 distance_2d(f32 x1, f32 z1, f32 x2, f32 z2) {
    f32 dx = x2 - x1;
    f32 dz = z2 - z1;
    return std::sqrt(dx * dx + dz * dz);
}

inline f32 distance_3d(f32 x1, f32 y1, f32 z1, f32 x2, f32 y2, f32 z2) {
    f32 dx = x2 - x1;
    f32 dy = y2 - y1;
    f32 dz = z2 - z1;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

inline bool is_valid_username(const std::string& username) {
    if (username.length() < 3 || username.length() > 16) {
        return false;
    }
    
    for (char c : username) {
        if (!std::isalnum(c) && c != '_') {
            return false;
        }
    }
    
    return true;
}

inline Position chunk_to_block(const ChunkPos& chunk_pos, i32 x, i32 z) {
    return Position(chunk_pos.x * 16 + x, 0, chunk_pos.z * 16 + z);
}

inline ChunkPos block_to_chunk(const Position& block_pos) {
    return ChunkPos(block_pos.x >> 4, block_pos.z >> 4);
}

inline u32 hash_position(const Position& pos) {
    return static_cast<u32>(pos.x) ^ 
           (static_cast<u32>(pos.y) << 8) ^ 
           (static_cast<u32>(pos.z) << 16);
}

inline u32 hash_chunk_pos(const ChunkPos& pos) {
    return static_cast<u32>(pos.x) ^ (static_cast<u32>(pos.z) << 16);
}

class Timer {
private:
    std::chrono::high_resolution_clock::time_point start_time_;
    
public:
    Timer() : start_time_(std::chrono::high_resolution_clock::now()) {}
    
    void reset() {
        start_time_ = std::chrono::high_resolution_clock::now();
    }
    
    i64 elapsed_millis() const {
        auto now = std::chrono::high_resolution_clock::now();
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            now - start_time_).count();
    }
    
    i64 elapsed_micros() const {
        auto now = std::chrono::high_resolution_clock::now();
        return std::chrono::duration_cast<std::chrono::microseconds>(
            now - start_time_).count();
    }
    
    f64 elapsed_seconds() const {
        auto now = std::chrono::high_resolution_clock::now();
        return std::chrono::duration<f64>(now - start_time_).count();
    }
};

class RateLimit {
private:
    i64 last_reset_;
    u32 count_;
    u32 max_rate_;
    i64 window_ms_;
    
public:
    RateLimit(u32 max_rate, i64 window_ms = 1000) 
        : last_reset_(steady_time_millis()), count_(0)
        , max_rate_(max_rate), window_ms_(window_ms) {}
    
    bool allow() {
        i64 now = steady_time_millis();
        
        if (now - last_reset_ >= window_ms_) {
            last_reset_ = now;
            count_ = 0;
        }
        
        if (count_ < max_rate_) {
            count_++;
            return true;
        }
        
        return false;
    }
    
    void reset() {
        last_reset_ = steady_time_millis();
        count_ = 0;
    }
    
    u32 remaining() const {
        return max_rate_ > count_ ? max_rate_ - count_ : 0;
    }
};

}

namespace std {
    template<>
    struct hash<mc::Position> {
        size_t operator()(const mc::Position& pos) const {
            return mc::utils::hash_position(pos);
        }
    };
    
    template<>
    struct hash<mc::ChunkPos> {
        size_t operator()(const mc::ChunkPos& pos) const {
            return mc::utils::hash_chunk_pos(pos);
        }
    };
}
