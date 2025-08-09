#pragma once
#include "core/config.hpp"
#include "core/logger.hpp"
#include "core/performance_monitor.hpp"
#include "network/network_server.hpp"
#include "core/thread_pool.hpp"
#include <memory>
#include <atomic>
#include <thread>
#include <condition_variable>
#include <string>

namespace mc::server {

class MinecraftServer {
private:
    std::unique_ptr<network::NetworkServer> network_server_;
    std::atomic<bool> running_{false};
    std::thread background_thread_;
    std::mutex shutdown_mutex_;
    std::condition_variable shutdown_cv_;
    std::atomic<bool> shutdown_requested_{false};

    void run_background() {
        while (running_.load() && !shutdown_requested_.load()) {
            std::this_thread::sleep_for(std::chrono::seconds(5));
            g_performance_monitor.set_active_connections(network::g_packet_manager.create_packet ? 0 : 0);
        }
    }

public:
    MinecraftServer() = default;
    ~MinecraftServer() {
        stop();
    }

    bool initialize() {
        try {
            g_logger.initialize();
            g_performance_monitor.start_monitoring();
            return true;
        } catch (...) {
            return false;
        }
    }

    void start() {
        if (running_.exchange(true)) return;
        network_server_ = std::make_unique<network::NetworkServer>(g_config.get_host(), g_config.get_port(), g_config.get_io_threads());
        network_server_->start();
        background_thread_ = std::thread(&MinecraftServer::run_background, this);
        g_logger.info(std::string("Server started on ") + g_config.get_host() + ":" + std::to_string(g_config.get_port()));
    }

    void stop() {
        if (!running_.exchange(false)) return;
        shutdown_requested_.store(true);
        if (network_server_) {
            network_server_->stop();
            network_server_.reset();
        }
        g_performance_monitor.stop_monitoring();
        g_logger.info("Server stopping");
        shutdown_cv_.notify_all();
        if (background_thread_.joinable()) background_thread_.join();
    }

    void wait_for_shutdown() {
        std::unique_lock<std::mutex> lk(shutdown_mutex_);
        shutdown_cv_.wait(lk, [this] { return shutdown_requested_.load() || !running_.load(); });
    }

    bool is_running() const {
        return running_.load();
    }

    void reload_config() {
        if (g_config.load_from_file()) {
            g_logger.info("Configuration reloaded");
        } else {
            g_logger.warn("Failed to reload configuration");
        }
    }

    void broadcast_message(const std::string& message) {
        g_logger.info(std::string("[Broadcast] ") + message);
    }

    void kick_player(const std::string& username, const std::string& reason) {
        g_logger.info(std::string("Kicking player ") + username + " Reason: " + reason);
    }

    void print_status() {
        auto stats = g_performance_monitor.get_stats();
        g_logger.info(std::string("TPS: ") + std::to_string(stats.current_tps) + " AvgTPS: " + std::to_string(stats.average_tps));
        g_logger.info(std::string("Memory MB: ") + std::to_string(stats.memory_usage_mb) + " Active connections: " + std::to_string(stats.active_connections));
    }
};

extern std::unique_ptr<MinecraftServer> g_minecraft_server;

}
