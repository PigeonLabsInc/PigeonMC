#pragma once

#include "types.hpp"
#include <atomic>
#include <chrono>
#include <thread>
#include <array>
#include <mutex>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <iostream>

namespace mc {

class PerformanceMonitor {
private:
    std::atomic<f64> current_tps_{20.0};
    std::atomic<u64> total_memory_used_{0};
    std::atomic<u64> buffer_pool_usage_{0};
    std::atomic<u32> active_connections_{0};
    std::atomic<u64> packets_per_second_{0};
    std::atomic<u64> bytes_per_second_{0};

    std::array<f64, 100> tps_history_{};
    size_t tps_history_index_{0};
    std::mutex tps_mutex_;

    std::atomic<u64> packet_count_{0};
    std::atomic<u64> byte_count_{0};
    std::chrono::steady_clock::time_point last_network_update_;
    std::mutex network_mutex_;

    std::chrono::steady_clock::time_point server_start_time_;
    std::atomic<bool> monitoring_{false};
    std::thread monitor_thread_;

    void monitor_loop() {
        auto last_tick = std::chrono::steady_clock::now();
        u32 tick_count = 0;
        while (monitoring_.load()) {
            auto start = std::chrono::steady_clock::now();
            auto sleep_duration = std::chrono::milliseconds(50) - (start - last_tick);
            if (sleep_duration > std::chrono::milliseconds(0)) {
                std::this_thread::sleep_for(sleep_duration);
            }
            auto end = std::chrono::steady_clock::now();
            auto tick_time = std::chrono::duration_cast<std::chrono::microseconds>(end - last_tick).count();
            f64 actual_tps = (tick_time == 0) ? 20.0 : (1000000.0 / static_cast<f64>(tick_time));
            current_tps_.store(std::min(actual_tps, 20.0));
            {
                std::lock_guard<std::mutex> lock(tps_mutex_);
                tps_history_[tps_history_index_] = current_tps_.load();
                tps_history_index_ = (tps_history_index_ + 1) % tps_history_.size();
            }
            if (++tick_count % 20 == 0) {
                update_network_stats();
                update_memory_stats();
            }
            last_tick = end;
        }
    }

    void update_network_stats() {
        std::lock_guard<std::mutex> lock(network_mutex_);
        auto current_time = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(current_time - last_network_update_).count();
        if (elapsed >= 1000) {
            u64 current_packets = packet_count_.exchange(0);
            u64 current_bytes = byte_count_.exchange(0);
            packets_per_second_.store((elapsed == 0) ? 0 : (current_packets * 1000 / static_cast<u64>(elapsed)));
            bytes_per_second_.store((elapsed == 0) ? 0 : (current_bytes * 1000 / static_cast<u64>(elapsed)));
            last_network_update_ = current_time;
        }
    }

    void update_memory_stats() {
        total_memory_used_.store(get_process_memory_usage());
        buffer_pool_usage_.store(get_buffer_pool_usage());
    }

    u64 get_process_memory_usage() const {
#ifdef __linux__
        std::ifstream file("/proc/self/status");
        std::string line;
        while (std::getline(file, line)) {
            if (line.rfind("VmRSS:", 0) == 0) {
                std::istringstream iss(line);
                std::string key, value, unit;
                iss >> key >> value >> unit;
                try {
                    return std::stoull(value) * 1024;
                } catch (...) {
                    return 0;
                }
            }
        }
#endif
        return 0;
    }

    u64 get_buffer_pool_usage() const {
        return 0;
    }

public:
    PerformanceMonitor()
        : server_start_time_(std::chrono::steady_clock::now())
        , last_network_update_(std::chrono::steady_clock::now()) {
        tps_history_.fill(20.0);
    }

    ~PerformanceMonitor() {
        stop_monitoring();
    }

    void start_monitoring() {
        if (monitoring_.exchange(true)) return;
        monitor_thread_ = std::thread(&PerformanceMonitor::monitor_loop, this);
    }

    void stop_monitoring() {
        if (!monitoring_.exchange(false)) return;
        if (monitor_thread_.joinable()) {
            monitor_thread_.join();
        }
    }

    void record_packet(size_t byte_size) {
        packet_count_.fetch_add(1);
        byte_count_.fetch_add(static_cast<u64>(byte_size));
    }

    void set_active_connections(u32 count) {
        active_connections_.store(count);
    }

    f64 get_current_tps() const {
        return current_tps_.load();
    }

    f64 get_average_tps() const {
        std::lock_guard<std::mutex> lock(tps_mutex_);
        f64 sum = 0.0;
        for (f64 tps : tps_history_) {
            sum += tps;
        }
        return sum / static_cast<f64>(tps_history_.size());
    }

    f64 get_min_tps() const {
        std::lock_guard<std::mutex> lock(tps_mutex_);
        f64 min_tps = 20.0;
        for (f64 tps : tps_history_) {
            min_tps = std::min(min_tps, tps);
        }
        return min_tps;
    }

    u64 get_memory_usage_mb() const {
        return total_memory_used_.load() / (1024 * 1024);
    }

    u64 get_buffer_pool_usage_mb() const {
        return buffer_pool_usage_.load() / (1024 * 1024);
    }

    u32 get_active_connections() const {
        return active_connections_.load();
    }

    u64 get_packets_per_second() const {
        return packets_per_second_.load();
    }

    u64 get_bytes_per_second() const {
        return bytes_per_second_.load();
    }

    f64 get_uptime_seconds() const {
        auto now = std::chrono::steady_clock::now();
        return std::chrono::duration<f64>(now - server_start_time_).count();
    }

    struct Stats {
        f64 current_tps;
        f64 average_tps;
        f64 min_tps;
        u64 memory_usage_mb;
        u64 buffer_pool_usage_mb;
        u32 active_connections;
        u64 packets_per_second;
        u64 bytes_per_second;
        f64 uptime_seconds;
    };

    Stats get_stats() const {
        return Stats{
            get_current_tps(),
            get_average_tps(),
            get_min_tps(),
            get_memory_usage_mb(),
            get_buffer_pool_usage_mb(),
            get_active_connections(),
            get_packets_per_second(),
            get_bytes_per_second(),
            get_uptime_seconds()
        };
    }
};

extern PerformanceMonitor g_performance_monitor;

}
