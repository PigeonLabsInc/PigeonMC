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
#include <thread>
#include <vector>

namespace mc::network {

using tcp = asio::ip::tcp;
using ConnectionPtr = std::shared_ptr<class Connection>;

class Connection : public std::enable_shared_from_this<Connection> {
private:
    tcp::socket socket_;
    ConnectionState state_;
    Buffer read_buffer_;
    Buffer write_buffer_;
    std::queue<Buffer> write_queue_;
    std::mutex write_mutex_;
    std::atomic<bool> writing_{false};
    std::atomic<bool> closed_{false};
    std::atomic<i64> last_ping_time_{0};
    std::atomic<i64> last_keep_alive_{0};
    bool compression_enabled_{false};
    i32 compression_threshold_{-1};
    GameProfile profile_;
    std::atomic<u32> entity_id_{0};
    Location location_;
    std::mutex location_mutex_;
    std::vector<byte> temp_read_buf_;

    static size_t get_varint_size(i32 value) {
        u32 uvalue = static_cast<u32>(value);
        size_t size = 0;
        do {
            uvalue >>= 7;
            size++;
        } while (uvalue != 0);
        return size;
    }

    void start_read() {
        if (closed_.load()) return;
        if (temp_read_buf_.empty()) temp_read_buf_.resize(8192);
        auto self = shared_from_this();
        socket_.async_read_some(asio::buffer(temp_read_buf_.data(), temp_read_buf_.size()),
            [self](std::error_code ec, std::size_t bytes_transferred) {
                if (!ec && bytes_transferred > 0) {
                    self->handle_read(bytes_transferred);
                } else {
                    self->close();
                }
            });
    }

    void handle_read(std::size_t bytes_transferred) {
        read_buffer_.write(temp_read_buf_.data(), bytes_transferred);
        while (read_buffer_.readable() > 0) {
            size_t initial_pos = read_buffer_.size();
            try {
                i32 packet_length = read_buffer_.read_varint();
                if (read_buffer_.readable() < static_cast<size_t>(packet_length)) {
                    break;
                }
                Buffer packet_data(read_buffer_.data() + initial_pos, packet_length);
                process_packet(packet_data);
            } catch (...) {
                close();
                return;
            }
        }
        start_read();
    }

    void process_packet(Buffer& packet_buffer) {
        i32 packet_id = packet_buffer.read_varint();
        auto packet = g_packet_manager.create_packet(state_, PacketDirection::SERVERBOUND, packet_id);
        if (!packet) return;
        packet->read(packet_buffer);
        handle_packet(std::move(packet));
    }

    void handle_packet(std::unique_ptr<Packet> packet) {
        switch (state_) {
            case ConnectionState::HANDSHAKING: handle_handshake_packet(packet.get()); break;
            case ConnectionState::STATUS:     handle_status_packet(packet.get());    break;
            case ConnectionState::LOGIN:      handle_login_packet(packet.get());     break;
            case ConnectionState::PLAY:       handle_play_packet(packet.get());      break;
        }
    }

    void handle_handshake_packet(Packet* p) {
        if (auto* h = dynamic_cast<handshake::HandshakePacket*>(p)) {
            if (h->protocol_version != MINECRAFT_PROTOCOL_VERSION) {
                close();
                return;
            }
            state_ = static_cast<ConnectionState>(h->next_state);
        }
    }

    void handle_status_packet(Packet* p) {
        if (dynamic_cast<status::StatusRequestPacket*>(p)) {
            std::string j = R"({"version":{"name":")" + std::string(MINECRAFT_VERSION) + R"(","protocol":)" + std::to_string(MINECRAFT_PROTOCOL_VERSION) + R"(},"players":{"max":100,"online":0},"description":{"text":"High Performance Minecraft Server"},"favicon":""})";
            send_packet(std::make_unique<status::StatusResponsePacket>(j));
        } else if (auto* ping = dynamic_cast<status::PingRequestPacket*>(p)) {
            send_packet(std::make_unique<status::PingResponsePacket>(ping->payload));
            close();
        }
    }

    void handle_login_packet(Packet* p) {
        if (auto* ls = dynamic_cast<login::LoginStartPacket*>(p)) {
            profile_.username = ls->username;
            profile_.display_name = ls->username;
            profile_.uuid = ls->player_uuid;
            send_packet(std::make_unique<login::LoginSuccessPacket>(profile_.uuid, profile_.username));
            state_ = ConnectionState::PLAY;
            g_thread_pool.submit([self = shared_from_this()]() {
                self->initialize_play_state();
            });
        }
    }

    void handle_play_packet(Packet* p) {
        if (auto* ka = dynamic_cast<play::KeepAlivePacket*>(p)) {
            last_keep_alive_.store(std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count());
        } else if (auto* pos = dynamic_cast<play::PlayerPositionPacket*>(p)) {
            std::lock_guard<std::mutex> lg(location_mutex_);
            location_ = {pos->x, pos->y, pos->z};
        }
    }

    void initialize_play_state() {
        entity_id_.store(1);
        auto jp = std::make_unique<play::JoinGamePacket>();
        jp->entity_id = entity_id_.load();
        jp->world_names = {"minecraft:overworld"};
        jp->dimension_type = "minecraft:overworld";
        jp->dimension_name = "minecraft:overworld";
        jp->hashed_seed = 12345;
        send_packet(std::move(jp));
        start_keep_alive_timer();
    }

    void start_keep_alive_timer() {
        auto self = shared_from_this();
        g_thread_pool.submit([self]() {
            while (!self->closed_.load()) {
                std::this_thread::sleep_for(std::chrono::seconds(20));
                if (self->state_ == ConnectionState::PLAY) {
                    i64 ts = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now().time_since_epoch()).count();
                    self->send_packet(std::make_unique<play::KeepAlivePacket>(ts, PacketDirection::CLIENTBOUND));
                    if (ts - self->last_keep_alive_.load() > 30000) {
                        self->close();
                        break;
                    }
                }
            }
        });
    }

    void start_write() {
        if (writing_.exchange(true)) return;
        std::lock_guard<std::mutex> lg(write_mutex_);
        if (write_queue_.empty()) {
            writing_.store(false);
            return;
        }
        Buffer& buf = write_queue_.front();
        auto self = shared_from_this();
        asio::async_write(socket_, asio::buffer(buf.data(), buf.size()),
            [self](std::error_code ec, std::size_t) {
                self->handle_write(ec);
            });
    }

    void handle_write(std::error_code ec) {
        {
            std::lock_guard<std::mutex> lg(write_mutex_);
            if (!write_queue_.empty()) write_queue_.pop();
        }
        if (ec) { close(); return; }
        writing_.store(false);
        start_write();
    }

public:
    explicit Connection(tcp::socket&& s)
        : socket_(std::move(s))
        , state_(ConnectionState::HANDSHAKING)
        , read_buffer_(8192)
        , write_buffer_(8192)
        , temp_read_buf_() {
        socket_.set_option(tcp::no_delay(true));
        socket_.set_option(asio::socket_base::keep_alive(true));
    }

    ~Connection() { close(); }

    void start() { start_read(); }

    void send_packet(std::unique_ptr<Packet> p) {
        if (closed_.load()) return;
        Buffer tmp(1024);
        p->write(tmp);
        Buffer fin(tmp.size() + 16);
        fin.write_varint(static_cast<i32>(tmp.size()) + static_cast<i32>(get_varint_size(p->get_id())));
        fin.write_varint(p->get_id());
        fin.write(tmp.data(), tmp.size());
        {
            std::lock_guard<std::mutex> lg(write_mutex_);
            write_queue_.push(std::move(fin));
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
        std::lock_guard<std::mutex> lg(location_mutex_);
        return location_;
    }
    std::string get_remote_address() const {
        try { return socket_.remote_endpoint().address().to_string(); }
        catch (...) { return "unknown"; }
    }
};

}
