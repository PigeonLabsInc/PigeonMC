#pragma once

#include "packet_types.hpp"
#include "core/buffer.hpp"
#include "core/thread_pool.hpp"
#include <asio.hpp>
#include <memory>
#include <atomic>
#include <queue>
#include <mutex>
#include <chrono>

namespace mc::network {

using tcp = asio::ip::tcp;

class Connection : public std::enable_shared_from_this<Connection> {
private:
    tcp::socket socket_;
    ConnectionState state_;
    Buffer read_buffer_;
    Buffer write_buffer_;
    std::queue<Buffer> write_queue_;
    std::mutex write_mutex_;
    std::atomic<bool> writing_{false}

using ConnectionPtr = std::shared_ptr<Connection>;

};
    std::atomic<bool> closed_{false};
    std::atomic<i64> last_ping_time_{0};
    std::atomic<i64> last_keep_alive_{0};
    
    bool compression_enabled_{false};
    i32 compression_threshold_{-1};
    
    GameProfile profile_;
    std::atomic<u32> entity_id_{0};
    Location location_;
    std::mutex location_mutex_;
    
    void start_read() {
        if (closed_.load()) return;
        
        auto self = shared_from_this();
        asio::async_read(socket_, asio::buffer(read_buffer_.data() + read_buffer_.size(), 
                        read_buffer_.writable()),
            [self](std::error_code ec, std::size_t bytes_transferred) {
                if (!ec && bytes_transferred > 0) {
                    self->handle_read(bytes_transferred);
                } else {
                    self->close();
                }
            });
    }
    
    void handle_read(std::size_t bytes_transferred) {
        read_buffer_.write(nullptr, bytes_transferred);
        
        while (read_buffer_.readable() > 0) {
            size_t initial_pos = read_buffer_.size();
            
            try {
                i32 packet_length = read_buffer_.read_varint();
                
                if (read_buffer_.readable() < static_cast<size_t>(packet_length)) {
                    break;
                }
                
                Buffer packet_data(read_buffer_.data() + initial_pos, packet_length);
                process_packet(packet_data);
                
            } catch (const std::exception& e) {
                close();
                return;
            }
        }
        
        start_read();
    }
    
    void process_packet(Buffer& packet_buffer) {
        i32 packet_id = packet_buffer.read_varint();
        
        auto packet = g_packet_manager.create_packet(state_, PacketDirection::SERVERBOUND, packet_id);
        if (!packet) {
            return;
        }
        
        packet->read(packet_buffer);
        handle_packet(std::move(packet));
    }
    
    void handle_packet(std::unique_ptr<Packet> packet) {
        switch (state_) {
            case ConnectionState::HANDSHAKING:
                handle_handshake_packet(packet.get());
                break;
            case ConnectionState::STATUS:
                handle_status_packet(packet.get());
                break;
            case ConnectionState::LOGIN:
                handle_login_packet(packet.get());
                break;
            case ConnectionState::PLAY:
                handle_play_packet(packet.get());
                break;
        }
    }
    
    void handle_handshake_packet(Packet* packet) {
        if (auto* handshake = dynamic_cast<handshake::HandshakePacket*>(packet)) {
            if (handshake->protocol_version != MINECRAFT_PROTOCOL_VERSION) {
                close();
                return;
            }
            
            state_ = static_cast<ConnectionState>(handshake->next_state);
        }
    }
    
    void handle_status_packet(Packet* packet) {
        if (dynamic_cast<status::StatusRequestPacket*>(packet)) {
            std::string status_json = R"({
                "version": {"name": ")" + std::string(MINECRAFT_VERSION) + R"(", "protocol": )" + std::to_string(MINECRAFT_PROTOCOL_VERSION) + R"(},
                "players": {"max": 100, "online": 0},
                "description": {"text": "High Performance Minecraft Server"},
                "favicon": ""
            })";
            
            send_packet(std::make_unique<status::StatusResponsePacket>(status_json));
            
        } else if (auto* ping = dynamic_cast<status::PingRequestPacket*>(packet)) {
            send_packet(std::make_unique<status::PingResponsePacket>(ping->payload));
            close();
        }
    }
    
    void handle_login_packet(Packet* packet) {
        if (auto* login_start = dynamic_cast<login::LoginStartPacket*>(packet)) {
            profile_.username = login_start->username;
            profile_.display_name = login_start->username;
            profile_.uuid = login_start->player_uuid;
            
            send_packet(std::make_unique<login::LoginSuccessPacket>(profile_.uuid, profile_.username));
            state_ = ConnectionState::PLAY;
            
            g_thread_pool.submit([self = shared_from_this()]() {
                self->initialize_play_state();
            });
        }
    }
    
    void handle_play_packet(Packet* packet) {
        if (auto* keep_alive = dynamic_cast<play::KeepAlivePacket*>(packet)) {
            last_keep_alive_.store(std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count());
                
        } else if (auto* pos = dynamic_cast<play::PlayerPositionPacket*>(packet)) {
            std::lock_guard<std::mutex> lock(location_mutex_);
            location_.x = pos->x;
            location_.y = pos->y;
            location_.z = pos->z;
        }
    }
    
    void initialize_play_state() {
        entity_id_.store(1);
        
        auto join_packet = std::make_unique<play::JoinGamePacket>();
        join_packet->entity_id = entity_id_.load();
        join_packet->world_names = {"minecraft:overworld"};
        join_packet->dimension_type = "minecraft:overworld";
        join_packet->dimension_name = "minecraft:overworld";
        join_packet->hashed_seed = 12345;
        
        send_packet(std::move(join_packet));
        
        start_keep_alive_timer();
    }
    
    void start_keep_alive_timer() {
        auto self = shared_from_this();
        g_thread_pool.submit([self]() {
            while (!self->closed_.load()) {
                std::this_thread::sleep_for(std::chrono::seconds(20));
                
                if (self->state_ == ConnectionState::PLAY) {
                    i64 timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now().time_since_epoch()).count();
                    
                    auto keep_alive = std::make_unique<play::KeepAlivePacket>(
                        timestamp, PacketDirection::CLIENTBOUND);
                    self->send_packet(std::move(keep_alive));
                    
                    i64 last_response = self->last_keep_alive_.load();
                    if (timestamp - last_response > 30000) {
                        self->close();
                        break;
                    }
                }
            }
        });
    }
    
    void start_write() {
        if (writing_.exchange(true)) return;
        
        std::lock_guard<std::mutex> lock(write_mutex_);
        if (write_queue_.empty()) {
            writing_.store(false);
            return;
        }
        
        Buffer& buffer = write_queue_.front();
        auto self = shared_from_this();
        
        asio::async_write(socket_, asio::buffer(buffer.data(), buffer.size()),
            [self](std::error_code ec, std::size_t bytes_transferred) {
                self->handle_write(ec, bytes_transferred);
            });
    }
    
    void handle_write(std::error_code ec, std::size_t bytes_transferred) {
        {
            std::lock_guard<std::mutex> lock(write_mutex_);
            if (!write_queue_.empty()) {
                write_queue_.pop();
            }
        }
        
        if (ec) {
            close();
            return;
        }
        
        writing_.store(false);
        start_write();
    }

public:
    explicit Connection(tcp::socket socket) 
        : socket_(std::move(socket))
        , state_(ConnectionState::HANDSHAKING)
        , read_buffer_(8192)
        , write_buffer_(8192) {
        
        socket_.set_option(tcp::no_delay(true));
        socket_.set_option(asio::socket_base::keep_alive(true));
    }
    
    ~Connection() {
        close();
    }
    
    void start() {
        start_read();
    }
    
    void send_packet(std::unique_ptr<Packet> packet) {
        if (closed_.load()) return;
        
        Buffer temp_buffer(1024);
        packet->write(temp_buffer);
        
        Buffer final_buffer(temp_buffer.size() + 16);
        final_buffer.write_varint(static_cast<i32>(temp_buffer.size()) + 
                                 static_cast<i32>(get_varint_size(packet->get_id())));
        final_buffer.write_varint(packet->get_id());
        final_buffer.write(temp_buffer.data(), temp_buffer.size());
        
        {
            std::lock_guard<std::mutex> lock(write_mutex_);
            write_queue_.push(std::move(final_buffer));
        }
        
        start_write();
    }
    
    void close() {
        if (closed_.exchange(true)) return;
        
        std::error_code ec;
        socket_.close(ec);
    }
    
    bool is_closed() const { return closed_.load(); }
    ConnectionState get_state() const { return state_; }
    const GameProfile& get_profile() const { return profile_; }
    u32 get_entity_id() const { return entity_id_.load(); }
    
    Location get_location() const {
        std::lock_guard<std::mutex> lock(location_mutex_);
        return location_;
    }
    
    std::string get_remote_address() const {
        try {
            return socket_.remote_endpoint().address().to_string();
        } catch (...) {
            return "unknown";
        }
    }
    
private:
    static size_t get_varint_size(i32 value) {
        u32 uvalue = static_cast<u32>(value);
        size_t size = 0;
        do {
            uvalue >>= 7;
            size++;
        } while (uvalue != 0);
        return size;
    }
};
