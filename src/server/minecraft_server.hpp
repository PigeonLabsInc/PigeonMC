#pragma once
#include "core/config.hpp"
#include "core/logger.hpp"
#include "core/thread_pool.hpp"
#include "core/performance_monitor.hpp"
#include "network/server.hpp"
#include "player/player.hpp"
#include "world/chunk.hpp"
#include <string>
#include <atomic>
#include <memory>
#include <vector>
#include <chrono>

namespace mc {
namespace server {

class MinecraftServer {
private:
    ServerConfig& config_;
    Logger& logger_;
    ThreadPool& thread_pool_;
    PerformanceMonitor& perf_;
    std::unique_ptr<mc::network::NetworkServer> network_server_;
    std::atomic<bool> running_{false};
    std::atomic<bool> initialized_{false};
    std::vector<std::thread> worker_threads_;
    std::chrono::steady_clock::time_point start_time_;
    std::atomic<u32> tick_count_{0};
    std::atomic<u32> player_count_{0};

    void main_loop() {
        using namespace std::chrono;
        auto last = steady_clock::now();
        while (running_.load()) {
            auto now = steady_clock::now();
            auto elapsed = duration_cast<milliseconds>(now - last);
            if (elapsed.count() >= 50) {
                tick();
                last = now;
            } else {
                std::this_thread::sleep_for(milliseconds(1));
            }
        }
    }

    void tick() {
        tick_count_.fetch_add(1);
        perf_.set_active_connections(network_server_ ? static_cast<u32>(network_server_->get_play_connections_count()) : 0);
    }

public:
    MinecraftServer()
        : config_(g_config), logger_(g_logger), thread_pool_(g_thread_pool), perf_(g_performance_monitor) {}

    ~MinecraftServer() {
        stop();
    }

    bool initialize() {
        if (initialized_.exchange(true)) return true;
        logger_.initialize();
        perf_.start_monitoring();
        try {
            network_server_ = std::make_unique<mc::network::NetworkServer>(config_.get_host(), config_.get_port(), config_.get_io_threads());
        } catch (...) {
            return false;
        }
        start_time_ = std::chrono::steady_clock::now();
        return true;
    }

    void start() {
        if (running_.exchange(true)) return;
        if (network_server_) network_server_->start();
        worker_threads_.emplace_back([this]() { main_loop(); });
    }

    void stop() {
        if (!running_.exchange(false)) return;
        if (network_server_) network_server_->stop();
        perf_.stop_monitoring();
        logger_.shutdown();
        for (auto& t : worker_threads_) {
            if (t.joinable()) t.join();
        }
        worker_threads_.clear();
    }

    void wait_for_shutdown() {
        while (running_.load()) std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    bool is_running() const { return running_.load(); }

    void print_status() {
        auto s = perf_.get_stats();
        logger_.info("Status: TPS=" + std::to_string(s.current_tps) + " avg=" + std::to_string(s.average_tps));
    }

    void reload_config() {
        config_.load_from_file();
        logger_.info("Configuration reloaded");
    }

    void kick_player(const std::string& username, const std::string& reason) {
        player::g_player_manager.kick_by_name(username, reason);
        logger_.info("Kicked player " + username + " reason: " + reason);
    }

    void broadcast_message(const std::string& message) {
        player::g_player_manager.broadcast_message(message);
        logger_.info("Broadcast message: " + message);
    }

    u32 get_tick_count() const { return tick_count_.load(); }
    u32 get_player_count() const { return player_count_.load(); }
    mc::network::NetworkServer* get_network_server() const { return network_server_.get(); }
};

} 
}

