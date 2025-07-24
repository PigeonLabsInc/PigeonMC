#include "server/minecraft_server.hpp"
#include "core/config.hpp"
#include "core/logger.hpp"
#include <iostream>
#include <csignal>
#include <thread>

using namespace mc;
using namespace mc::server;

int main(int argc, char* argv[]) {
    std::cout << "High Performance Minecraft Server v1.0.0" << std::endl;
    std::cout << "Protocol Version: " << MINECRAFT_PROTOCOL_VERSION << std::endl;
    std::cout << "Minecraft Version: " << MINECRAFT_VERSION << std::endl;
    std::cout << std::endl;
    
    try {
        MinecraftServer server;
        
        if (!server.initialize()) {
            std::cerr << "Failed to initialize server" << std::endl;
            return 1;
        }
        
        server.start();
        
        std::cout << "Server is running. Type 'help' for commands or 'stop' to shutdown." << std::endl;
        
        std::thread command_thread([&server]() {
            std::string command;
            while (server.is_running() && std::getline(std::cin, command)) {
                if (command.empty()) continue;
                
                if (command == "stop" || command == "shutdown") {
                    server.stop();
                    break;
                } else if (command == "status") {
                    server.print_status();
                } else if (command == "help") {
                    std::cout << "Available commands:" << std::endl;
                    std::cout << "  status  - Show server status" << std::endl;
                    std::cout << "  stop    - Stop the server" << std::endl;
                    std::cout << "  reload  - Reload configuration" << std::endl;
                    std::cout << "  help    - Show this help" << std::endl;
                } else if (command == "reload") {
                    server.reload_config();
                } else if (command.substr(0, 5) == "kick " && command.length() > 5) {
                    std::string username = command.substr(5);
                    server.kick_player(username, "Kicked by console");
                } else if (command.substr(0, 4) == "say " && command.length() > 4) {
                    std::string message = command.substr(4);
                    server.broadcast_message(message);
                } else {
                    std::cout << "Unknown command: " << command << std::endl;
                    std::cout << "Type 'help' for available commands." << std::endl;
                }
            }
        });
        
        server.wait_for_shutdown();
        
        if (command_thread.joinable()) {
            command_thread.join();
        }
        
        std::cout << "Server shutdown complete." << std::endl;
        return 0;
        
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    }
}
