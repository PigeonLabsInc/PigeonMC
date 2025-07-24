#pragma once

#include "core/types.hpp"
#include "core/config.hpp"
#include "core/logger.hpp"
#include "core/performance_monitor.hpp"
#include "network/server.hpp"
#include "world/chunk.hpp"
#include "player/player.hpp"
#include "entity/entity.hpp"
#include <atomic>
#include <thread>
#include <chrono>
#include <signal.h>

namespace mc::server {

class MinecraftServer {
private:
    std::unique_ptr<network::NetworkServer> network_server_;
    
    std::atomic<bool> running_{false};
    std::atomic<bool> stopping_{false};
    
    std::thread game_loop_thread_;
    std::thread auto_save_thread_;
    
    std::atomic<u64> current_tick_{0};
    std::atomic<f64> current_tps_{20.0};
    
    timestamp_t server_start_time_;
    timestamp_t last_tick_time_;
    timestamp_t last_auto_save_;
    
    static MinecraftServer* instance_;
    
    void game_loop() {
        LOG_INFO("Starting main game loop");
        
        auto target_tick_duration = std::chrono::microseconds(50000);
        last_tick_time_ = std::chrono::steady_clock::now();
        
        while (running_.load()) {
            auto tick_start = std::chrono::steady_clock::now();
            
            tick();
            
            auto tick_end = std::chrono::steady_clock::now();
            auto tick_duration = tick_end - tick_start;
            
            current_tps_.store(1000000.0 / std::chrono::duration_cast<std::chrono::microseconds>(
                tick_end - last_tick_time_).count());
            
            last_tick_time_ = tick_end;
            
            if (tick_duration < target_tick_duration) {
                std::this_thread::sleep_for(target_tick_duration - tick_duration);
            }
            
            current_tick_.fetch_add(1);
        }
        
        LOG_INFO("Game loop stopped");
    }
    
    void tick() {
        try {
            tick_players();
            tick_entities();
            tick_world();
            update_performance_stats();
            
        } catch (const std::exception& e) {
            LOG_ERROR("Exception in game tick: " + std::string(e.what()));
        }
    }
    
    void tick_players() {
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
    }
    
    void tick_entities() {
        entity::g_entity_manager.tick_all_entities();
    }
    
    void tick_world() {
        
    }
    
    void update_performance_stats() {
        g_performance_monitor.set_active_connections(
            static_cast<u32>(player::g_player_manager.get_online_count()));
    }
    
    void auto_save_loop() {
        LOG_INFO("Starting auto-save thread");
        
        auto save_interval = std::chrono::milliseconds(g_config.get_auto_save_interval());
        last_auto_save_ = std::chrono::steady_clock::now();
        
        while (running_.load()) {
            std::this_thread::sleep_for(std::chrono::seconds(30));
            
            auto now = std::chrono::steady_clock::now();
            if (now - last_auto_save_ >= save_interval) {
                perform_auto_save();
                last_auto_save_ = now;
            }
        }
        
        LOG_INFO("Auto-save thread stopped");
    }
    
    void perform_auto_save() {
        LOG_INFO("Performing auto-save...");
        
        auto start_time = std::chrono::steady_clock::now();
        size_t saved_chunks = 0;
        
        auto end_time = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
            end_time - start_time).count();
        
        LOG_INFO("Auto-save completed: " + std::to_string(saved_chunks) + 
                " chunks saved in " + std::to_string(duration) + "ms");
    }
    
    static void signal_handler(int signal) {
        LOG_INFO("Received signal " + std::to_string(signal) + ", shutting down server...");
        if (instance_) {
            instance_->stop();
        }
    }

public:
    MinecraftServer() : server_start_time_(std::chrono::steady_clock::now()) {
        instance_ = this;
        
        signal(SIGINT, signal_handler);
        signal(SIGTERM, signal_handler);
        
#ifdef SIGPIPE
        signal(SIGPIPE, SIG_IGN);
#endif
    }
    
    ~MinecraftServer() {
        stop();
        instance_ = nullptr;
    }
    
    bool initialize() {
        LOG_INFO("Initializing Minecraft Server...");
        
        try {
            g_logger.initialize();
            g_performance_monitor.start_monitoring();
            
            network_server_ = std::make_unique<network::NetworkServer>(
                g_config.get_host(), 
                g_config.get_port(),
                g_config.get_io_threads()
            );
            
            world::g_chunk_manager.set_max_loaded_chunks(g_config.get_max_chunks_loaded());
            world::g_chunk_manager.set_chunk_timeout(g_config.get_chunk_unload_timeout());
            
            LOG_INFO("Server initialization completed");
            return true;
            
        } catch (const std::exception& e) {
            LOG_FATAL("Failed to initialize server: " + std::string(e.what()));
            return false;
        }
    }
    
    void start() {
        if (running_.exchange(true)) {
            LOG_WARN("Server is already running");
            return;
        }
        
        LOG_INFO("Starting Minecraft Server on " + g_config.get_host() + 
                ":" + std::to_string(g_config.get_port()));
        
        try {
            network_server_->start();
            
            game_loop_thread_ = std::thread(&MinecraftServer::game_loop, this);
            auto_save_thread_ = std::thread(&MinecraftServer::auto_save_loop, this);
            
            LOG_INFO("Server started successfully!");
            LOG_INFO("Server name: " + g_config.get_server_name());
            LOG_INFO("MOTD: " + g_config.get_motd());
            LOG_INFO("Max players: " + std::to_string(g_config.get_max_players()));
            LOG_INFO("View distance: " + std::to_string(g_config.get_view_distance()));
            
        } catch (const std::exception& e) {
            LOG_FATAL("Failed to start server: " + std::string(e.what()));
            running_.store(false);
            throw;
        }
    }
    
    void stop() {
        if (!running_.exchange(false)) {
            return;
        }
        
        LOG_INFO("Stopping Minecraft Server...");
        stopping_.store(true);
        
        if (network_server_) {
            network_server_->stop();
        }
        
        auto players = player::g_player_manager.get_online_players();
        for (auto& player : players) {
            player->disconnect();
        }
        
        perform_auto_save();
        
        if (game_loop_thread_.joinable()) {
            game_loop_thread_.join();
        }
        
        if (auto_save_thread_.joinable()) {
            auto_save_thread_.join();
        }
        
        g_performance_monitor.stop_monitoring();
        g_logger.shutdown();
        
        LOG_INFO("Server stopped successfully");
    }
    
    void wait_for_shutdown() {
        if (!running_.load()) return;
        
        if (game_loop_thread_.joinable()) {
            game_loop_thread_.join();
        }
    }
    
    bool is_running() const { return running_.load(); }
    bool is_stopping() const { return stopping_.load(); }
    
    u64 get_current_tick() const { return current_tick_.load(); }
    f64 get_current_tps() const { return current_tps_.load(); }
    
    f64 get_uptime_seconds() const {
        auto now = std::chrono::steady_clock::now();
        return std::chrono::duration<f64>(now - server_start_time_).count();
    }
    
    network::NetworkServer* get_network_server() const {
        return network_server_.get();
    }
    
    void broadcast_message(const std::string& message) {
        LOG_INFO("[BROADCAST] " + message);
        
    }
    
    void kick_player(const std::string& username, const std::string& reason = "Kicked by server") {
        auto player = player::g_player_manager.get_player(username);
        if (player && player->is_online()) {
            LOG_INFO("Kicking player " + username + ": " + reason);
            player->disconnect();
        }
    }
    
    void reload_config() {
        LOG_INFO("Reloading server configuration...");
        
        if (g_config.load_from_file()) {
            g_logger.set_level(Logger().string_to_level(g_config.get_log_level()));
            LOG_INFO("Configuration reloaded successfully");
        } else {
            LOG_ERROR("Failed to reload configuration");
        }
    }
    
    struct ServerStatus {
        bool running;
        u64 current_tick;
        f64 current_tps;
        f64 uptime_seconds;
        size_t online_players;
        size_t max_players;
        size_t loaded_chunks;
        size_t total_entities;
        PerformanceMonitor::Stats performance;
    };
    
    ServerStatus get_status() const {
        return ServerStatus{
            running_.load(),
            current_tick_.load(),
            current_tps_.load(),
            get_uptime_seconds(),
            player::g_player_manager.get_online_count(),
            g_config.get_max_players(),
            world::g_chunk_manager.get_loaded_chunk_count(),
            entity::g_entity_manager.get_entity_count(),
            g_performance_monitor.get_stats()
        };
    }
    
    void print_status() const {
        auto status = get_status();
        
        LOG_INFO("=== Server Status ===");
        LOG_INFO("Running: " + std::string(status.running ? "Yes" : "No"));
        LOG_INFO("Uptime: " + std::to_string(static_cast<int>(status.uptime_seconds)) + " seconds");
        LOG_INFO("Current TPS: " + std::to_string(status.current_tps));
        LOG_INFO("Tick: " + std::to_string(status.current_tick));
        LOG_INFO("Players: " + std::to_string(status.online_players) + "/" + std::to_string(status.max_players));
        LOG_INFO("Loaded Chunks: " + std::to_string(status.loaded_chunks));
        LOG_INFO("Entities: " + std::to_string(status.total_entities));
        LOG_INFO("Memory Usage: " + std::to_string(status.performance.memory_usage_mb) + " MB");
        LOG_INFO("Network: " + std::to_string(status.performance.packets_per_second) + " pkt/s, " +
                std::to_string(status.performance.bytes_per_second / 1024) + " KB/s");
    }
    
    static MinecraftServer* get_instance() { return instance_; }
};

MinecraftServer* MinecraftServer::instance_ = nullptr;

}
