#include "core/core.cpp"
#include "network/packet_manager.cpp"
#include "server/server_impl.cpp"

#include "network/connection.hpp"
#include "player/player.hpp"
#include "world/chunk.hpp"
#include "network/chunk_packets.hpp"

namespace mc::network {

void Connection::handle_login_packet(Packet* packet) {
    if (auto* login_start = dynamic_cast<login::LoginStartPacket*>(packet)) {
        profile_.username = login_start->username;
        profile_.display_name = login_start->username;
        profile_.uuid = login_start->player_uuid;
        
        LOG_INFO("Player " + profile_.username + " attempting to join");
        
        send_packet(std::make_unique<login::LoginSuccessPacket>(profile_.uuid, profile_.username));
        state_ = ConnectionState::PLAY;
        
        g_thread_pool.submit([self = shared_from_this()]() {
            self->initialize_play_state();
        });
    }
}

void Connection::initialize_play_state() {
    entity_id_.store(player::g_player_manager.get_next_entity_id());
    
    auto player = player::g_player_manager.create_player(
        shared_from_this(), profile_
    );
    
    if (!player) {
        LOG_ERROR("Failed to create player for " + profile_.username);
        close();
        return;
    }
    
    auto join_packet = std::make_unique<play::JoinGamePacket>();
    join_packet->entity_id = entity_id_.load();
    join_packet->world_names = {"minecraft:overworld"};
    join_packet->dimension_type = "minecraft:overworld";
    join_packet->dimension_name = "minecraft:overworld";
    join_packet->hashed_seed = g_config.get_world_seed();
    join_packet->max_players = static_cast<i32>(g_config.get_max_players());
    join_packet->view_distance = g_config.get_view_distance();
    join_packet->simulation_distance = g_config.get_simulation_distance();
    
    send_packet(std::move(join_packet));
    
    auto spawn_pos = player->get_spawn_location();
    auto pos_packet = std::make_unique<play::PlayerPositionAndLookPacket>();
    pos_packet->x = spawn_pos.x;
    pos_packet->y = spawn_pos.y;
    pos_packet->z = spawn_pos.z;
    pos_packet->yaw = spawn_pos.yaw;
    pos_packet->pitch = spawn_pos.pitch;
    pos_packet->flags = 0;
    pos_packet->teleport_id = 1;
    
    send_packet(std::move(pos_packet));
    
    player->set_location(spawn_pos);
    player->update_loaded_chunks();
    
    send_initial_chunks(player);
    start_keep_alive_timer();
    
    LOG_INFO("Player " + profile_.username + " joined the game");
}

void Connection::send_initial_chunks(player::PlayerPtr player) {
    auto player_chunk = player->get_chunk_pos();
    i32 view_distance = player->get_view_distance();
    
    auto view_pos_packet = std::make_unique<play::UpdateViewPositionPacket>(
        player_chunk.x, player_chunk.z);
    send_packet(std::move(view_pos_packet));
    
    for (i32 dx = -view_distance; dx <= view_distance; ++dx) {
        for (i32 dz = -view_distance; dz <= view_distance; ++dz) {
            if (dx * dx + dz * dz <= view_distance * view_distance) {
                world::ChunkPos chunk_pos(player_chunk.x + dx, player_chunk.z + dz);
                
                auto chunk = world::g_chunk_manager.get_chunk(chunk_pos);
                if (!chunk) {
                    chunk = world::g_chunk_manager.load_chunk(chunk_pos);
                }
                
                if (chunk && chunk->is_loaded()) {
                    send_chunk_data(chunk);
                }
            }
        }
    }
}

void Connection::send_chunk_data(world::ChunkPtr chunk) {
    auto chunk_packet = std::make_unique<play::ChunkDataPacket>(
        chunk->get_position().x, chunk->get_position().z);
    
    chunk_packet->serialize_chunk(chunk);
    send_packet(std::move(chunk_packet));
}

void Connection::handle_play_packet(Packet* packet) {
    auto player = player::g_player_manager.get_player(profile_.uuid);
    if (!player) return;
    
    if (auto* keep_alive = dynamic_cast<play::KeepAlivePacket*>(packet)) {
        last_keep_alive_.store(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
        player->update_activity();
        
    } else if (auto* pos = dynamic_cast<play::PlayerPositionPacket*>(packet)) {
        Location new_location(pos->x, pos->y, pos->z);
        
        auto old_chunk = player->get_chunk_pos();
        player->set_location(new_location);
        auto new_chunk = player->get_chunk_pos();
        
        if (!(old_chunk == new_chunk)) {
            player->update_loaded_chunks();
            
            auto view_pos_packet = std::make_unique<play::UpdateViewPositionPacket>(
                new_chunk.x, new_chunk.z);
            send_packet(std::move(view_pos_packet));
            
            send_chunk_updates(player, old_chunk, new_chunk);
        }
        
        player->update_activity();
    }
}

void Connection::send_chunk_updates(player::PlayerPtr player, 
                                   const world::ChunkPos& old_chunk, 
                                   const world::ChunkPos& new_chunk) {
    i32 view_distance = player->get_view_distance();
    
    for (i32 dx = -view_distance; dx <= view_distance; ++dx) {
        for (i32 dz = -view_distance; dz <= view_distance; ++dz) {
            if (dx * dx + dz * dz > view_distance * view_distance) continue;
            
            world::ChunkPos chunk_pos(new_chunk.x + dx, new_chunk.z + dz);
            
            i32 old_dx = chunk_pos.x - old_chunk.x;
            i32 old_dz = chunk_pos.z - old_chunk.z;
            bool was_loaded = (old_dx * old_dx + old_dz * old_dz <= view_distance * view_distance);
            
            if (!was_loaded) {
                auto chunk = world::g_chunk_manager.get_chunk(chunk_pos);
                if (!chunk) {
                    chunk = world::g_chunk_manager.load_chunk(chunk_pos);
                }
                
                if (chunk && chunk->is_loaded()) {
                    send_chunk_data(chunk);
                }
            }
        }
    }
    
    for (i32 dx = -view_distance; dx <= view_distance; ++dx) {
        for (i32 dz = -view_distance; dz <= view_distance; ++dz) {
            if (dx * dx + dz * dz > view_distance * view_distance) continue;
            
            world::ChunkPos chunk_pos(old_chunk.x + dx, old_chunk.z + dz);
            
            i32 new_dx = chunk_pos.x - new_chunk.x;
            i32 new_dz = chunk_pos.z - new_chunk.z;
            bool should_be_loaded = (new_dx * new_dx + new_dz * new_dz <= view_distance * view_distance);
            
            if (!should_be_loaded) {
                auto unload_packet = std::make_unique<play::UnloadChunkPacket>(
                    chunk_pos.x, chunk_pos.z);
                send_packet(std::move(unload_packet));
            }
        }
    }
}

}

namespace mc::player {

u32 PlayerManager::get_next_entity_id() {
    return next_entity_id_.fetch_add(1);
}

PlayerPtr PlayerManager::create_player(network::ConnectionPtr connection, const GameProfile& profile) {
    if (get_online_count() >= g_config.get_max_players()) {
        LOG_WARN("Server full, rejecting player " + profile.username);
        return nullptr;
    }
    
    if (get_player(profile.uuid)) {
        LOG_WARN("Player " + profile.username + " already online");
        return nullptr;
    }
    
    u32 entity_id = get_next_entity_id();
    auto player = std::make_shared<Player>(connection, profile, entity_id);
    
    Location spawn_location(
        g_config.get_spawn_x(),
        g_config.get_spawn_y(), 
        g_config.get_spawn_z()
    );
    player->set_spawn_location(spawn_location);
    player->set_location(spawn_location);
    
    std::lock_guard<std::mutex> lock(players_mutex_);
    players_by_uuid_[profile.uuid] = player;
    players_by_name_[profile.username] = player;
    players_by_entity_id_[entity_id] = player;
    
    LOG_INFO("Created player " + profile.username + " with entity ID " + std::to_string(entity_id));
    
    return player;
}

}

namespace mc::world {

void ChunkManager::generate_chunk_async(const ChunkPos& pos) {
    g_thread_pool.submit([this, pos]() {
        auto chunk = std::make_shared<Chunk>(pos);
        
        std::string generator = g_config.get_world_generator();
        if (generator == "flat") {
            chunk->generate_flat_world();
        } else {
            chunk->generate_flat_world();
        }
        
        {
            std::lock_guard<std::mutex> lock(chunk_mutex_);
            loaded_chunks_[pos] = chunk;
            pending_chunks_.erase(pos);
        }
        
        LOG_DEBUG("Generated chunk at " + std::to_string(pos.x) + ", " + std::to_string(pos.z));
        
        cleanup_old_chunks();
    });
}

ChunkPtr ChunkManager::load_chunk(const ChunkPos& pos) {
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
    
    generate_chunk_async(pos);
    return nullptr;
}

}

namespace mc::server {

void MinecraftServer::tick_players() {
    auto players = player::g_player_manager.get_online_players();
    
    for (auto& player : players) {
        if (!player->is_online()) {
            continue;
        }
        
        player->update_loaded_chunks();
        
        auto last_activity = player->get_last_activity();
        auto now = std::chrono::steady_clock::now();
        auto idle_time = std::chrono::duration_cast<std::chrono::minutes>(
            now - last_activity).count();
        
        if (idle_time > 30) {
            LOG_INFO("Kicking player " + player->get_profile().username + " for inactivity");
            player->disconnect();
        }
    }
    
    player::g_player_manager.cleanup_offline_players();
    
    g_performance_monitor.set_active_connections(
        static_cast<u32>(player::g_player_manager.get_online_count()));
}

}
