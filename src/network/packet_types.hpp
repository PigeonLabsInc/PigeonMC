#pragma once
#include "core/types.hpp"
#include "core/buffer.hpp"
#include <memory>
#include <string>
#include <vector>
#include <unordered_map>
#include <functional>
#include <cstring>

namespace mc::network {

enum class ConnectionState : i32 {
    HANDSHAKING = 0,
    STATUS = 1,
    LOGIN = 2,
    PLAY = 3
};

enum class PacketDirection {
    CLIENTBOUND,
    SERVERBOUND
};

struct PacketHeader {
    i32 length;
    i32 packet_id;
    PacketHeader() : length(0), packet_id(0) {}
    PacketHeader(i32 len, i32 id) : length(len), packet_id(id) {}
};

class Packet {
public:
    virtual ~Packet() = default;
    virtual i32 get_id() const = 0;
    virtual void write(Buffer& buffer) const = 0;
    virtual void read(Buffer& buffer) = 0;
    virtual ConnectionState get_state() const = 0;
    virtual PacketDirection get_direction() const = 0;
};

namespace handshake {

class HandshakePacket : public Packet {
public:
    i32 protocol_version;
    std::string server_address;
    u16 server_port;
    i32 next_state;
    HandshakePacket() : protocol_version(0), server_port(0), next_state(0) {}
    i32 get_id() const override { return 0x00; }
    ConnectionState get_state() const override { return ConnectionState::HANDSHAKING; }
    PacketDirection get_direction() const override { return PacketDirection::SERVERBOUND; }
    void write(Buffer& buffer) const override {
        buffer.write_varint(protocol_version);
        buffer.write_string(server_address);
        buffer.write_be<u16>(server_port);
        buffer.write_varint(next_state);
    }
    void read(Buffer& buffer) override {
        protocol_version = buffer.read_varint();
        server_address = buffer.read_string();
        server_port = buffer.read_be<u16>();
        next_state = buffer.read_varint();
    }
};

}

namespace status {

class StatusRequestPacket : public Packet {
public:
    StatusRequestPacket() = default;
    i32 get_id() const override { return 0x00; }
    ConnectionState get_state() const override { return ConnectionState::STATUS; }
    PacketDirection get_direction() const override { return PacketDirection::SERVERBOUND; }
    void write(Buffer&) const override {}
    void read(Buffer&) override {}
};

class StatusResponsePacket : public Packet {
public:
    std::string json_response;
    StatusResponsePacket() = default;
    explicit StatusResponsePacket(const std::string& json) : json_response(json) {}
    i32 get_id() const override { return 0x00; }
    ConnectionState get_state() const override { return ConnectionState::STATUS; }
    PacketDirection get_direction() const override { return PacketDirection::CLIENTBOUND; }
    void write(Buffer& buffer) const override {
        buffer.write_string(json_response);
    }
    void read(Buffer& buffer) override {
        json_response = buffer.read_string();
    }
};

class PingRequestPacket : public Packet {
public:
    i64 payload;
    PingRequestPacket() : payload(0) {}
    explicit PingRequestPacket(i64 payload) : payload(payload) {}
    i32 get_id() const override { return 0x01; }
    ConnectionState get_state() const override { return ConnectionState::STATUS; }
    PacketDirection get_direction() const override { return PacketDirection::SERVERBOUND; }
    void write(Buffer& buffer) const override {
        buffer.write_be<i64>(payload);
    }
    void read(Buffer& buffer) override {
        payload = buffer.read_be<i64>();
    }
};

class PingResponsePacket : public Packet {
public:
    i64 payload;
    PingResponsePacket() : payload(0) {}
    explicit PingResponsePacket(i64 payload) : payload(payload) {}
    i32 get_id() const override { return 0x01; }
    ConnectionState get_state() const override { return ConnectionState::STATUS; }
    PacketDirection get_direction() const override { return PacketDirection::CLIENTBOUND; }
    void write(Buffer& buffer) const override {
        buffer.write_be<i64>(payload);
    }
    void read(Buffer& buffer) override {
        payload = buffer.read_be<i64>();
    }
};

}

namespace login {

class LoginStartPacket : public Packet {
public:
    std::string username;
    UUID player_uuid;
    LoginStartPacket() = default;
    explicit LoginStartPacket(const std::string& name) : username(name) {}
    i32 get_id() const override { return 0x00; }
    ConnectionState get_state() const override { return ConnectionState::LOGIN; }
    PacketDirection get_direction() const override { return PacketDirection::SERVERBOUND; }
    void write(Buffer& buffer) const override {
        buffer.write_string(username);
        buffer.write(player_uuid.data(), 16);
    }
    void read(Buffer& buffer) override {
        username = buffer.read_string();
        buffer.read(player_uuid.data(), 16);
    }
};

class LoginSuccessPacket : public Packet {
public:
    UUID player_uuid;
    std::string username;
    LoginSuccessPacket() = default;
    LoginSuccessPacket(const UUID& uuid, const std::string& name) : player_uuid(uuid), username(name) {}
    i32 get_id() const override { return 0x02; }
    ConnectionState get_state() const override { return ConnectionState::LOGIN; }
    PacketDirection get_direction() const override { return PacketDirection::CLIENTBOUND; }
    void write(Buffer& buffer) const override {
        buffer.write(player_uuid.data(), 16);
        buffer.write_string(username);
        buffer.write_varint(0);
    }
    void read(Buffer& buffer) override {
        buffer.read(player_uuid.data(), 16);
        username = buffer.read_string();
        buffer.read_varint();
    }
};

}

namespace play {

class KeepAlivePacket : public Packet {
public:
    i64 keep_alive_id;
    PacketDirection direction_;
    KeepAlivePacket() : keep_alive_id(0), direction_(PacketDirection::CLIENTBOUND) {}
    KeepAlivePacket(PacketDirection dir) : keep_alive_id(0), direction_(dir) {}
    KeepAlivePacket(i64 id, PacketDirection dir) : keep_alive_id(id), direction_(dir) {}
    i32 get_id() const override {
        return direction_ == PacketDirection::CLIENTBOUND ? 0x21 : 0x12;
    }
    ConnectionState get_state() const override { return ConnectionState::PLAY; }
    PacketDirection get_direction() const override { return direction_; }
    void write(Buffer& buffer) const override {
        buffer.write_be<i64>(keep_alive_id);
    }
    void read(Buffer& buffer) override {
        keep_alive_id = buffer.read_be<i64>();
    }
};

class JoinGamePacket : public Packet {
public:
    i32 entity_id;
    bool is_hardcore;
    GameMode game_mode;
    GameMode previous_game_mode;
    std::vector<std::string> world_names;
    std::string dimension_type;
    std::string dimension_name;
    i64 hashed_seed;
    i32 max_players;
    i32 view_distance;
    i32 simulation_distance;
    bool reduced_debug_info;
    bool enable_respawn_screen;
    bool is_debug;
    bool is_flat;
    JoinGamePacket()
        : entity_id(0), is_hardcore(false), game_mode(GameMode::SURVIVAL),
          previous_game_mode(GameMode::SURVIVAL), hashed_seed(0),
          max_players(20), view_distance(10), simulation_distance(10),
          reduced_debug_info(false), enable_respawn_screen(true),
          is_debug(false), is_flat(false) {}
    i32 get_id() const override { return 0x26; }
    ConnectionState get_state() const override { return ConnectionState::PLAY; }
    PacketDirection get_direction() const override { return PacketDirection::CLIENTBOUND; }
    void write(Buffer& buffer) const override {
        buffer.write_be<i32>(entity_id);
        buffer.write_byte(is_hardcore ? 1 : 0);
        buffer.write_byte(static_cast<byte>(game_mode));
        buffer.write_byte(static_cast<byte>(previous_game_mode));
        buffer.write_varint(static_cast<i32>(world_names.size()));
        for (const auto& world : world_names) {
            buffer.write_string(world);
        }
        buffer.write_string(dimension_type);
        buffer.write_string(dimension_name);
        buffer.write_be<i64>(hashed_seed);
        buffer.write_varint(max_players);
        buffer.write_varint(view_distance);
        buffer.write_varint(simulation_distance);
        buffer.write_byte(reduced_debug_info ? 1 : 0);
        buffer.write_byte(enable_respawn_screen ? 1 : 0);
        buffer.write_byte(is_debug ? 1 : 0);
        buffer.write_byte(is_flat ? 1 : 0);
        buffer.write_byte(0);
    }
    void read(Buffer& buffer) override {
        entity_id = buffer.read_be<i32>();
        is_hardcore = buffer.read_byte() != 0;
        game_mode = static_cast<GameMode>(buffer.read_byte());
        previous_game_mode = static_cast<GameMode>(buffer.read_byte());
        i32 world_count = buffer.read_varint();
        world_names.resize(world_count);
        for (i32 i = 0; i < world_count; ++i) {
            world_names[i] = buffer.read_string();
        }
        dimension_type = buffer.read_string();
        dimension_name = buffer.read_string();
        hashed_seed = buffer.read_be<i64>();
        max_players = buffer.read_varint();
        view_distance = buffer.read_varint();
        simulation_distance = buffer.read_varint();
        reduced_debug_info = buffer.read_byte() != 0;
        enable_respawn_screen = buffer.read_byte() != 0;
        is_debug = buffer.read_byte() != 0;
        is_flat = buffer.read_byte() != 0;
        buffer.read_byte();
    }
};

class PlayerPositionPacket : public Packet {
public:
    f64 x, y, z;
    bool on_ground;
    PlayerPositionPacket() : x(0), y(0), z(0), on_ground(false) {}
    PlayerPositionPacket(f64 x, f64 y, f64 z, bool on_ground)
        : x(x), y(y), z(z), on_ground(on_ground) {}
    i32 get_id() const override { return 0x14; }
    ConnectionState get_state() const override { return ConnectionState::PLAY; }
    PacketDirection get_direction() const override { return PacketDirection::SERVERBOUND; }
    void write(Buffer& buffer) const override {
        {
            u64 bits;
            std::memcpy(&bits, &x, sizeof(bits));
            buffer.write_be<u64>(bits);
        }
        {
            u64 bits;
            std::memcpy(&bits, &y, sizeof(bits));
            buffer.write_be<u64>(bits);
        }
        {
            u64 bits;
            std::memcpy(&bits, &z, sizeof(bits));
            buffer.write_be<u64>(bits);
        }
        buffer.write_byte(on_ground ? 1 : 0);
    }
    void read(Buffer& buffer) override {
        {
            u64 bits = buffer.read_be<u64>();
            std::memcpy(&x, &bits, sizeof(bits));
        }
        {
            u64 bits = buffer.read_be<u64>();
            std::memcpy(&y, &bits, sizeof(bits));
        }
        {
            u64 bits = buffer.read_be<u64>();
            std::memcpy(&z, &bits, sizeof(bits));
        }
        on_ground = buffer.read_byte() != 0;
    }
};

}

using PacketFactory = std::function<std::unique_ptr<Packet>()>;
using PacketRegistry = std::unordered_map<i32, PacketFactory>;

class PacketManager {
private:
    std::unordered_map<ConnectionState, std::unordered_map<PacketDirection, PacketRegistry>> registries_;
public:
    PacketManager();
    template<typename T>
    void register_packet() {
        T sample;
        auto factory = []() -> std::unique_ptr<Packet> {
            return std::make_unique<T>();
        };
        registries_[sample.get_state()][sample.get_direction()][sample.get_id()] = factory;
    }
    std::unique_ptr<Packet> create_packet(ConnectionState state, PacketDirection direction, i32 packet_id) const;
};

extern PacketManager g_packet_manager;

}
