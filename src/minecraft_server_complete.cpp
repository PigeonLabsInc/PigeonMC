#define MC_ENABLE_MAIN
#include "final_integration.hpp"

namespace mc::network {

void Connection::handle_login_packet(Packet* packet) {
    if (auto* login_start = dynamic_cast<login::LoginStartPacket*>(packet)) {
        profile_.username = login_start->username;
        profile_.display_name = login_start->username;
        profile_.uuid = login_start->player_uuid;
        
        if (!utils::ServerUtils::is_valid_username(profile_.username)) {
            LOG_WARN("Invalid username: " + profile_.username);
            close();
            return;
        }
        
        if (player::g_player_manager.get_online_count() >= g_config.get_max_players()) {
            LOG_INFO("Server full, rejecting " + profile_.username);
            close();
            return;
        }
        
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
    
    auto spawn_location = utils::ServerUtils::find_safe_spawn_location({0, 0});
    player->set_spawn_location(spawn_location);
    player->set_location(spawn_location);
    
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
    
    auto pos_packet = std::make_unique<play::PlayerPositionAndLookPacket>();
    pos_packet->x = spawn_location.x;
    pos_packet->y = spawn_location.y;
    pos_packet->z = spawn_location.z;
    pos_packet->yaw = spawn_location.yaw;
    pos_packet->pitch = spawn_location.pitch;
    pos_packet->flags = 0;
    pos_packet->teleport_id = 1;
    
    send_packet(std::move(pos_packet));
    
    player->update_loaded_chunks();
    send_initial_chunks(player);
    start_keep_alive_timer();
    
    LOG_INFO("Player " + profile_.username + " joined the game");
    
    server::g_command_manager.execute_command(
        "say " + profile_.username + " joined the game",
        server::CommandSender::CONSOLE, "Server", nullptr
    );
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
        g_performance_monitor.record_packet(64);
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
    
    auto saved_chunk = g_world_persistence.load_chunk(pos);
    if (saved_chunk) {
        std::lock_guard<std::mutex> lock(chunk_mutex_);
        loaded_chunks_[pos] = saved_chunk;
        pending_chunks_.erase(pos);
        return saved_chunk;
    }
    
    generate_chunk_async(pos);
    return nullptr;
}

}

namespace mc::server {

void MinecraftServer::initialize_extensions() {
    ServerExtensions::initialize_server_hooks(this);
}

std::string MinecraftServer::get_server_info() const {
    std::ostringstream info;
    
    auto status = get_status();
    
    info << "Server Status:\n";
    info << "  Name: " << g_config.get_server_name() << "\n";
    info << "  Version: Minecraft " << MINECRAFT_VERSION << " (Protocol " << MINECRAFT_PROTOCOL_VERSION << ")\n";
    info << "  Running: " << (status.running ? "Yes" : "No") << "\n";
    info << "  Uptime: " << utils::ServerUtils::format_duration(static_cast<i64>(status.uptime_seconds)) << "\n";
    info << "  TPS: " << std::fixed << std::setprecision(2) << status.current_tps << "\n";
    info << "  Players: " << status.online_players << "/" << status.max_players << "\n";
    info << "  Memory: " << utils::ServerUtils::format_bytes(status.performance.memory_usage_mb * 1024 * 1024) << "\n";
    info << "  Network: " << status.performance.packets_per_second << " pkt/s, " 
         << utils::ServerUtils::format_bytes(status.performance.bytes_per_second) << "/s\n";
    info << "  Chunks: " << status.loaded_chunks << " loaded\n";
    info << "  Entities: " << status.total_entities << "\n";
    
    return info.str();
}

void MinecraftServer::emergency_shutdown(const std::string& reason) {
    LOG_FATAL("Emergency shutdown: " + reason);
    
    auto players = player::g_player_manager.get_online_players();
    for (auto& player : players) {
        player->disconnect();
    }
    
    world::g_world_persistence.save_all_chunks();
    
    stop();
}

void MinecraftServer::tick_world() {
    static u64 world_tick_counter = 0;
    world_tick_counter++;
    
    if (world_tick_counter % 20 == 0) {
        auto dirty_chunks = std::vector<world::ChunkPtr>();
        
        for (const auto& player : player::g_player_manager.get_online_players()) {
            auto chunks = world::g_chunk_manager.get_chunks_in_range(
                player->get_chunk_pos(), player->get_view_distance());
            
            for (auto& chunk : chunks) {
                if (chunk->is_dirty()) {
                    dirty_chunks.push_back(chunk);
                }
            }
        }
        
        for (auto& chunk : dirty_chunks) {
            g_world_persistence.save_chunk_async(chunk);
        }
    }
}

void MinecraftServer::perform_auto_save() {
    LOG_INFO("Performing auto-save...");
    
    auto start_time = std::chrono::steady_clock::now();
    
    g_world_persistence.save_all_chunks();
    
    size_t saved_chunks = world::g_chunk_manager.get_loaded_chunk_count();
    
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time).count();
    
    LOG_INFO("Auto-save completed: " + std::to_string(saved_chunks) + 
            " chunks saved in " + std::to_string(duration) + "ms");
}

}

namespace mc {

int ServerBootstrap::run_server(int argc, char* argv[]) {
    try {
        utils::ServerUtils::print_server_banner();
        
        if (argc > 1) {
            std::string config_path = argv[1];
            LOG_INFO("Using configuration file: " + config_path);
            g_config = ServerConfig(config_path);
        }
        
        server::MinecraftServer server;
        
        if (!server.initialize()) {
            LOG_FATAL("Failed to initialize server");
            return 1;
        }
        
        server.initialize_extensions();
        server.start();
        
        LOG_INFO("Server is running. Type 'help' for commands or 'stop' to shutdown.");
        std::cout << utils::ServerUtils::get_system_info() << std::endl;
        
        setup_console_handler(&server);
        server.wait_for_shutdown();
        
        LOG_INFO("Server shutdown complete.");
        return 0;
        
    } catch (const std::exception& e) {
        LOG_FATAL("Fatal error: " + std::string(e.what()));
        return 1;
    }
}

void ServerBootstrap::setup_console_handler(server::MinecraftServer* server) {
    std::thread console_thread([server]() {
        std::string command_line;
        while (server->is_running() && std::getline(std::cin, command_line)) {
            if (command_line.empty()) continue;
            
            if (!server::g_command_manager.execute_command(
                command_line, server::CommandSender::CONSOLE, "Console", server)) {
                
                if (command_line == "stop" || command_line == "shutdown" || command_line == "exit") {
                    server->stop();
                    break;
                }
            }
            
            if (!server->is_running()) break;
        }
    });
    
    console_thread.detach();
}

}

#include <sstream>
#include <iomanip>

namespace mc::utils {

std::string ServerUtils::format_duration(i64 seconds) {
    i64 days = seconds / 86400;
    seconds %= 86400;
    i64 hours = seconds / 3600;
    seconds %= 3600;
    i64 minutes = seconds / 60;
    seconds %= 60;
    
    std::string result;
    if (days > 0) result += std::to_string(days) + "d ";
    if (hours > 0) result += std::to_string(hours) + "h ";
    if (minutes > 0) result += std::to_string(minutes) + "m ";
    result += std::to_string(seconds) + "s";
    
    return result;
}

std::string ServerUtils::format_bytes(u64 bytes) {
    const char* units[] = {"B", "KB", "MB", "GB", "TB"};
    int unit_index = 0;
    double size = static_cast<double>(bytes);
    
    while (size >= 1024.0 && unit_index < 4) {
        size /= 1024.0;
        unit_index++;
    }
    
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2) << size << " " << units[unit_index];
    return oss.str();
}

std::string ServerUtils::get_system_info() {
    std::ostringstream info;
    
    info << "System Information:\n";
    info << "  CPU Cores: " << std::thread::hardware_concurrency() << "\n";
    info << "  Thread Pool Size: " << g_thread_pool.size() << "\n";
    info << "  Memory Usage: " << format_bytes(g_performance_monitor.get_memory_usage_mb() * 1024 * 1024) << "\n";
    info << "  Uptime: " << format_duration(static_cast<i64>(g_performance_monitor.get_uptime_seconds())) << "\n";
    
    return info.str();
}

void ServerUtils::print_server_banner() {
    std::cout << R"(
    ╗██████╗  ███╗   ███╗ ██████╗
    ║██╔══██╗ ████╗ ████║██╔════╝
    ║██████╔╝ ██╔████╔██║██║     
    ║██╔═══╝  ██║╚██╔╝██║██║     
    ║██║       ██║ ╚═╝ ██║╚██████╗
    ╚═╝        ╚═╝     ╚═╝ ╚═════╝
    
    Pigeon Minecraft Server - Beta Version Used
    ==========================================
)" << std::endl;
}

bool ServerUtils::is_valid_username(const std::string& username) {
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

std::string ServerUtils::generate_uuid_string() {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 15);
    
    std::string uuid;
    uuid.reserve(36);
    
    for (int i = 0; i < 8; ++i) {
        uuid += "0123456789abcdef"[dis(gen)];
    }
    uuid += '-';
    
    for (int i = 0; i < 4; ++i) {
        uuid += "0123456789abcdef"[dis(gen)];
    }
    uuid += '-';
    
    for (int i = 0; i < 4; ++i) {
        uuid += "0123456789abcdef"[dis(gen)];
    }
    uuid += '-';
    
    for (int i = 0; i < 4; ++i) {
        uuid += "0123456789abcdef"[dis(gen)];
    }
    uuid += '-';
    
    for (int i = 0; i < 12; ++i) {
        uuid += "0123456789abcdef"[dis(gen)];
    }
    
    return uuid;
}

Location ServerUtils::find_safe_spawn_location(const world::ChunkPos& around) {
    for (i32 y = 100; y >= 60; --y) {
        Position pos(around.x * 16 + 8, y, around.z * 16 + 8);
        auto block = world::g_chunk_manager.get_block(pos);
        
        if (!block.is_air()) {
            Position above_pos(pos.x, pos.y + 1, pos.z);
            auto above_block = world::g_chunk_manager.get_block(above_pos);
            
            Position above2_pos(pos.x, pos.y + 2, pos.z);
            auto above2_block = world::g_chunk_manager.get_block(above2_pos);
            
            if (above_block.is_air() && above2_block.is_air()) {
                return Location(pos.x + 0.5, pos.y + 1, pos.z + 0.5);
            }
        }
    }
    
    return Location(around.x * 16 + 8.5, 65, around.z * 16 + 8.5);
}

}
