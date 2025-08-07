#pragma once

#include "core/utils.hpp"
#include "network/connection.hpp"
#include "network/chunk_packets.hpp"

namespace mc::network {

class Connection;

void Connection::handle_handshake_packet(Packet* packet) {
    if (auto* handshake = dynamic_cast<handshake::HandshakePacket*>(packet)) {
        if (handshake->protocol_version != MINECRAFT_PROTOCOL_VERSION) {
            LOG_WARN("Player with incompatible protocol version " + 
                    std::to_string(handshake->protocol_version) + " tried to connect");
            close();
            return;
        }
        
        state_ = static_cast<ConnectionState>(handshake->next_state);
        LOG_DEBUG("Handshake completed, switching to state " + std::to_string(static_cast<i32>(state_)));
    }
}

void Connection::handle_status_packet(Packet* packet) {
    if (dynamic_cast<status::StatusRequestPacket*>(packet)) {
        auto online_count = player::g_player_manager.get_online_count();
        auto max_players = g_config.get_max_players();
        
        std::string status_json = R"({
            "version": {"name": ")" + std::string(MINECRAFT_VERSION) + R"(", "protocol": )" + std::to_string(MINECRAFT_PROTOCOL_VERSION) + R"(},
            "players": {"max": )" + std::to_string(max_players) + R"(, "online": )" + std::to_string(online_count) + R"(},
            "description": {"text": ")" + g_config.get_motd() + R"("},
            "favicon": ""
        })";
        
        send_packet(std::make_unique<status::StatusResponsePacket>(status_json));
        
    } else if (auto* ping = dynamic_cast<status::PingRequestPacket*>(packet)) {
        send_packet(std::make_unique<status::PingResponsePacket>(ping->payload));
        
        g_thread_pool.submit([self = shared_from_this()]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            self->close();
        });
    }
}

void Connection::handle_login_packet(Packet* packet) {
    if (auto* login_start = dynamic_cast<login::LoginStartPacket*>(packet)) {
        if (!utils::is_valid_username(login_start->username)) {
            LOG_WARN("Invalid username: " + login_start->username);
            close();
            return;
        }
        
        profile_.username = login_start->username;
        profile_.display_name = login_start->username;
        
        if (g_config.is_online_mode()) {
            profile_.uuid = login_start->player_uuid;
        } else {
            profile_.uuid = utils::generate_offline_uuid(login_start->username);
        }
        
        if (player::g_player_manager.get_player(profile_.uuid)) {
            LOG_WARN("Player " + profile_.username + " is already online");
            close();
            return;
        }
        
        LOG_INFO("Player " + profile_.username + " (" + utils::uuid_to_string(profile_.uuid) + ") logging in");
        
        send_packet(std::make_unique<login::LoginSuccessPacket>(profile_.uuid, profile_.username));
        state_ = ConnectionState::PLAY;
        
        g_thread_pool.submit([self = shared_from_this()]() {
            self->initialize_play_state();
        });
    }
}

void Connection::send_initial_chunks(player::PlayerPtr player) {
    if (!player) return;
    
    auto player_chunk = player->get_chunk_pos();
    i32 view_distance = player->get_view_distance();
    
    auto view_pos_packet = std::make_unique<play::UpdateViewPositionPacket>(
        player_chunk.x, player_chunk.z);
    send_packet(std::move(view_pos_packet));
    
    std::vector<world::ChunkPos> chunks_to_load;
    
    for (i32 dx = -view_distance; dx <= view_distance; ++dx) {
        for (i32 dz = -view_distance; dz <= view_distance; ++dz) {
            if (dx * dx + dz * dz <= view_distance * view_distance) {
                world::ChunkPos chunk_pos(player_chunk.x + dx, player_chunk.z + dz);
                chunks_to_load.push_back(chunk_pos);
            }
        }
    }
    
    std::sort(chunks_to_load.begin(), chunks_to_load.end(), 
        [&](const world::ChunkPos& a, const world::ChunkPos& b) {
            i32 dist_a = (a.x - player_chunk.x) * (a.x - player_chunk.x) + 
                        (a.z - player_chunk.z) * (a.z - player_chunk.z);
            i32 dist_b = (b.x - player_chunk.x) * (b.x - player_chunk.x) + 
                        (b.z - player_chunk.z) * (b.z - player_chunk.z);
            return dist_a < dist_b;
        });
    
    for (const auto& chunk_pos : chunks_to_load) {
        auto chunk = world::g_chunk_manager.get_chunk(chunk_pos);
        if (!chunk) {
            chunk = world::g_chunk_manager.load_chunk(chunk_pos);
        }
        
        if (chunk && chunk->is_loaded()) {
            send_chunk_data(chunk);
        }
    }
}

void Connection::send_chunk_data(world::ChunkPtr chunk) {
    if (!chunk || !chunk->is_loaded()) return;
    
    try {
        auto chunk_packet = std::make_unique<play::ChunkDataPacket>(
            chunk->get_position().x, chunk->get_position().z);
        
        chunk_packet->serialize_chunk(chunk);
        send_packet(std::move(chunk_packet));
        
        g_performance_monitor.record_packet(chunk_packet->chunk_data.size());
        
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to send chunk data: " + std::string(e.what()));
    }
}

}

namespace mc::player {

void Player::update_loaded_chunks() {
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
    
    std::vector<world::ChunkPos> to_unload;
    for (const auto& chunk_pos : loaded_chunks_) {
        if (needed_chunks.find(chunk_pos) == needed_chunks.end()) {
            to_unload.push_back(chunk_pos);
        }
    }
    
    for (const auto& chunk_pos : to_unload) {
        loaded_chunks_.erase(chunk_pos);
        
        if (connection_ && !connection_->is_closed()) {
            auto unload_packet = std::make_unique<network::play::UnloadChunkPacket>(
                chunk_pos.x, chunk_pos.z);
            connection_->send_packet(std::move(unload_packet));
        }
    }
    
    std::vector<world::ChunkPos> to_load;
    for (const auto& chunk_pos : needed_chunks) {
        if (loaded_chunks_.find(chunk_pos) == loaded_chunks_.end()) {
            to_load.push_back(chunk_pos);
        }
    }
    
    for (const auto& chunk_pos : to_load) {
        loaded_chunks_.insert(chunk_pos);
        
        auto chunk = world::g_chunk_manager.get_chunk(chunk_pos);
        if (!chunk) {
            chunk = world::g_chunk_manager.load_chunk(chunk_pos);
        }
        
        if (chunk && chunk->is_loaded() && connection_ && !connection_->is_closed()) {
            connection_->send_chunk_data(chunk);
        }
    }
}

}

namespace mc::world {

void Chunk::generate_flat_world() {
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
    
    LOG_DEBUG("Generated flat world chunk at " + std::to_string(position_.x) + ", " + std::to_string(position_.z));
}

void ChunkManager::cleanup_old_chunks() {
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
    
    size_t unloaded = 0;
    for (const auto& pos : to_unload) {
        unload_chunk(pos);
        unloaded++;
        
        if (unloaded >= 10) break;
    }
    
    if (unloaded > 0) {
        LOG_DEBUG("Unloaded " + std::to_string(unloaded) + " old chunks");
    }
}

}

namespace mc::server {

void MinecraftServer::print_status() const {
    auto status = get_status();
    
    std::cout << "\n=== Server Status ===" << std::endl;
    std::cout << "Running: " << (status.running ? "Yes" : "No") << std::endl;
    std::cout << "Uptime: " << utils::format_duration(static_cast<i64>(status.uptime_seconds)) << std::endl;
    std::cout << "Current TPS: " << std::fixed << std::setprecision(2) << status.current_tps << std::endl;
    std::cout << "Average TPS: " << std::fixed << std::setprecision(2) << status.performance.average_tps << std::endl;
    std::cout << "Tick: " << status.current_tick << std::endl;
    std::cout << "Players: " << status.online_players << "/" << status.max_players << std::endl;
    std::cout << "Loaded Chunks: " << status.loaded_chunks << std::endl;
    std::cout << "Entities: " << status.total_entities << std::endl;
    std::cout << "Memory Usage: " << utils::format_bytes(status.performance.memory_usage_mb * 1024 * 1024) << std::endl;
    std::cout << "Network: " << status.performance.packets_per_second << " pkt/s, " 
              << utils::format_bytes(status.performance.bytes_per_second) << "/s" << std::endl;
    std::cout << "===================\n" << std::endl;
}

void MinecraftServer::broadcast_message(const std::string& message) {
    LOG_INFO("[BROADCAST] " + message);
    
    auto players = player::g_player_manager.get_online_players();
    for (auto& player : players) {
        if (player->is_online() && player->get_connection()) {
            
        }
    }
}

}

namespace mc {

std::string get_server_info() {
    std::ostringstream info;
    info << "High Performance Minecraft Server v1.0.0\n";
    info << "Protocol: " << MINECRAFT_PROTOCOL_VERSION << " (Minecraft " << MINECRAFT_VERSION << ")\n";
    info << "Build: " << __DATE__ << " " << __TIME__ << "\n";
    info << "Threading: " << std::thread::hardware_concurrency() << " cores available\n";
    return info.str();
}

}
