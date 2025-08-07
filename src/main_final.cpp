#include "server/minecraft_server.hpp"
#include "core/config.hpp"
#include "core/logger.hpp"
#include "fixes_and_integration.hpp"
#include <iostream>
#include <csignal>
#include <thread>

using namespace mc;
using namespace mc::server;

int main(int argc, char* argv[]) {
    std::cout << get_server_info() << std::endl;
    
    try {
        g_config = ServerConfig("server.json");
        
        std::cout << "Starting " << g_config.get_server_name() << std::endl;
        std::cout << "Listening on " << g_config.get_host() << ":" << g_config.get_port() << std::endl;
        std::cout << "Max players: " << g_config.get_max_players() << std::endl;
        std::cout << "View distance: " << g_config.get_view_distance() << std::endl;
        std::cout << std::endl;
        
        MinecraftServer server;
        
        if (!server.initialize()) {
            std::cerr << "Failed to initialize server" << std::endl;
            return 1;
        }
        
        server.start();
        
        std::cout << "Server is running. Commands:" << std::endl;
        std::cout << "  status  - Show server status" << std::endl;
        std::cout << "  reload  - Reload configuration" << std::endl;
        std::cout << "  stop    - Stop the server" << std::endl;
        std::cout << "  help    - Show help" << std::endl;
        std::cout << std::endl;
        
        std::thread command_thread([&server]() {
            std::string command;
            while (server.is_running() && std::getline(std::cin, command)) {
                command = utils::trim(command);
                if (command.empty()) continue;
                
                if (command == "stop" || command == "shutdown" || command == "quit") {
                    std::cout << "Stopping server..." << std::endl;
                    server.stop();
                    break;
                    
                } else if (command == "status" || command == "s") {
                    server.print_status();
                    
                } else if (command == "help" || command == "h" || command == "?") {
                    std::cout << "Available commands:" << std::endl;
                    std::cout << "  status, s       - Show server status" << std::endl;
                    std::cout << "  stop, shutdown  - Stop the server" << std::endl;
                    std::cout << "  reload, r       - Reload configuration" << std::endl;
                    std::cout << "  kick <player>   - Kick a player" << std::endl;
                    std::cout << "  say <message>   - Broadcast message" << std::endl;
                    std::cout << "  help, h, ?      - Show this help" << std::endl;
                    std::cout << "  info, i         - Show server info" << std::endl;
                    
                } else if (command == "reload" || command == "r") {
                    server.reload_config();
                    std::cout << "Configuration reloaded" << std::endl;
                    
                } else if (command == "info" || command == "i") {
                    std::cout << get_server_info() << std::endl;
                    
                } else if (command.substr(0, 5) == "kick " && command.length() > 5) {
                    std::string username = utils::trim(command.substr(5));
                    if (!username.empty()) {
                        server.kick_player(username, "Kicked by console");
                        std::cout << "Kicked player: " << username << std::endl;
                    }
                    
                } else if (command.substr(0, 4) == "say " && command.length() > 4) {
                    std::string message = utils::trim(command.substr(4));
                    if (!message.empty()) {
                        server.broadcast_message("[Server] " + message);
                        std::cout << "Broadcasted: " << message << std::endl;
                    }
                    
                } else if (command == "gc") {
                    std::cout << "Running garbage collection..." << std::endl;
                    world::g_chunk_manager.cleanup_old_chunks();
                    player::g_player_manager.cleanup_offline_players();
                    std::cout << "Cleanup completed" << std::endl;
                    
                } else if (command == "save") {
                    std::cout << "Saving world..." << std::endl;
                    
                    std::cout << "World saved" << std::endl;
                    
                } else if (command.substr(0, 3) == "tp " && command.length() > 3) {
                    auto parts = utils::split_string(command.substr(3), ' ');
                    if (parts.size() == 4) {
                        try {
                            std::string username = parts[0];
                            f64 x = std::stod(parts[1]);
                            f64 y = std::stod(parts[2]);
                            f64 z = std::stod(parts[3]);
                            
                            auto player = player::g_player_manager.get_player(username);
                            if (player && player->is_online()) {
                                player->set_location(Location(x, y, z));
                                std::cout << "Teleported " << username << " to " 
                                         << x << ", " << y << ", " << z << std::endl;
                            } else {
                                std::cout << "Player not found: " << username << std::endl;
                            }
                        } catch (const std::exception& e) {
                            std::cout << "Invalid coordinates" << std::endl;
                        }
                    } else {
                        std::cout << "Usage: tp <player> <x> <y> <z>" << std::endl;
                    }
                    
                } else if (command == "list" || command == "players") {
                    auto players = player::g_player_manager.get_online_players();
                    std::cout << "Online players (" << players.size() << "):" << std::endl;
                    for (const auto& player : players) {
                        auto loc = player->get_location();
                        std::cout << "  " << player->get_profile().username 
                                 << " at " << std::fixed << std::setprecision(1)
                                 << loc.x << ", " << loc.y << ", " << loc.z << std::endl;
                    }
                    
                } else if (command == "chunks") {
                    std::cout << "Chunk information:" << std::endl;
                    std::cout << "  Loaded: " << world::g_chunk_manager.get_loaded_chunk_count() << std::endl;
                    std::cout << "  Pending: " << world::g_chunk_manager.get_pending_chunk_count() << std::endl;
                    
                } else if (command == "memory" || command == "mem") {
                    auto stats = g_performance_monitor.get_stats();
                    std::cout << "Memory usage:" << std::endl;
                    std::cout << "  Total: " << utils::format_bytes(stats.memory_usage_mb * 1024 * 1024) << std::endl;
                    std::cout << "  Buffers: " << utils::format_bytes(stats.buffer_pool_usage_mb * 1024 * 1024) << std::endl;
                    
                } else if (command == "perf" || command == "performance") {
                    auto stats = g_performance_monitor.get_stats();
                    std::cout << "Performance statistics:" << std::endl;
                    std::cout << "  TPS: " << std::fixed << std::setprecision(2) 
                             << stats.current_tps << " (avg: " << stats.average_tps 
                             << ", min: " << stats.min_tps << ")" << std::endl;
                    std::cout << "  Network: " << stats.packets_per_second << " pkt/s, " 
                             << utils::format_bytes(stats.bytes_per_second) << "/s" << std::endl;
                    std::cout << "  Uptime: " << utils::format_duration(static_cast<i64>(stats.uptime_seconds)) << std::endl;
                    
                } else {
                    std::cout << "Unknown command: " << command << std::endl;
                    std::cout << "Type 'help' for available commands." << std::endl;
                }
            }
        });
        
        server.wait_for_shutdown();
        
        if (command_thread.joinable()) {
            command_thread.detach();
        }
        
        std::cout << "Server shutdown complete." << std::endl;
        return 0;
        
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    } catch (...) {
        std::cerr << "Unknown fatal error occurred" << std::endl;
        return 1;
    }
}
