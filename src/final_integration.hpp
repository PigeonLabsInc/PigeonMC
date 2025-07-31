#pragma once

#include "complete_integration.cpp"

namespace mc {

namespace network {
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

namespace world {
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
}

}

namespace mc::server {

class ServerExtensions {
public:
    static void initialize_server_hooks(MinecraftServer* server) {
        LOG_INFO("Initializing server extensions...");
        
        setup_auto_restart();
        setup_performance_alerts();
        setup_player_event_handlers();
        
        LOG_INFO("Server extensions initialized");
    }
    
private:
    static void setup_auto_restart() {
        g_thread_pool.submit([]() {
            while (true) {
                std::this_thread::sleep_for(std::chrono::hours(6));
                
                if (g_performance_monitor.get_current_tps() < 15.0) {
                    LOG_WARN("Server performance degraded, consider restart");
                    
                    auto memory_usage = g_performance_monitor.get_memory_usage_mb();
                    if (memory_usage > 4096) {
                        LOG_ERROR("High memory usage detected: " + std::to_string(memory_usage) + " MB");
                    }
                }
            }
        });
    }
    
    static void setup_performance_alerts() {
        g_thread_pool.submit([]() {
            f64 last_tps = 20.0;
            
            while (true) {
                std::this_thread::sleep_for(std::chrono::seconds(30));
                
                f64 current_tps = g_performance_monitor.get_current_tps();
                
                if (current_tps < 18.0 && last_tps >= 18.0) {
                    LOG_WARN("Server TPS dropped to " + std::to_string(current_tps));
                }
                
                if (current_tps >= 19.0 && last_tps < 18.0) {
                    LOG_INFO("Server TPS recovered to " + std::to_string(current_tps));
                }
                
                last_tps = current_tps;
            }
        });
    }
    
    static void setup_player_event_handlers() {
        g_thread_pool.submit([]() {
            size_t last_player_count = 0;
            
            while (true) {
                std::this_thread::sleep_for(std::chrono::seconds(10));
                
                size_t current_count = player::g_player_manager.get_online_count();
                
                if (current_count != last_player_count) {
                    LOG_INFO("Player count changed: " + std::to_string(current_count) + 
                            " players online");
                    
                    if (current_count == 0) {
                        LOG_INFO("No players online, reducing server load");
                        world::g_chunk_manager.set_auto_unload(true);
                    } else if (last_player_count == 0) {
                        LOG_INFO("Players joined, optimizing for gameplay");
                        world::g_chunk_manager.set_auto_unload(false);
                    }
                    
                    last_player_count = current_count;
                }
            }
        });
    }
};

}

namespace mc::utils {

class ServerUtils {
public:
    static std::string format_duration(i64 seconds) {
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
    
    static std::string format_bytes(u64 bytes) {
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
    
    static std::string get_system_info() {
        std::ostringstream info;
        
        info << "System Information:\n";
        info << "  CPU Cores: " << std::thread::hardware_concurrency() << "\n";
        info << "  Thread Pool Size: " << g_thread_pool.size() << "\n";
        info << "  Memory Usage: " << format_bytes(g_performance_monitor.get_memory_usage_mb() * 1024 * 1024) << "\n";
        info << "  Uptime: " << format_duration(static_cast<i64>(g_performance_monitor.get_uptime_seconds())) << "\n";
        
        return info.str();
    }
    
    static void print_server_banner() {
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
    
    static bool is_valid_username(const std::string& username) {
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
    
    static std::string generate_uuid_string() {
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
    
    static Location find_safe_spawn_location(const world::ChunkPos& around) {
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
};

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

}

namespace mc {

class ServerBootstrap {
public:
    static int run_server(int argc, char* argv[]) {
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
    
private:
    static void setup_console_handler(server::MinecraftServer* server) {
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
};

}

#ifdef MC_ENABLE_MAIN
int main(int argc, char* argv[]) {
    return mc::ServerBootstrap::run_server(argc, argv);
}
#endif
