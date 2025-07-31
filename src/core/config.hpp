#pragma once

#include "types.hpp"
#include <nlohmann/json.hpp>
#include <string>
#include <fstream>
#include <mutex>
#include <sstream>
#include <vector>
#include <thread>

namespace mc {

class ServerConfig {
private:
    nlohmann::json config_;
    std::string config_path_;
    mutable std::mutex config_mutex_;
    
public:
    explicit ServerConfig(const std::string& config_path = "server.json") 
        : config_path_(config_path) {
        load_defaults();
        load_from_file();
    }
    
    void load_defaults() {
        config_ = {
            {"server", {
                {"name", "High Performance Minecraft Server"},
                {"motd", "A fast C++ Minecraft server"},
                {"host", "0.0.0.0"},
                {"port", 25565},
                {"max_players", 100},
                {"view_distance", 10},
                {"simulation_distance", 10},
                {"difficulty", "normal"},
                {"gamemode", "survival"},
                {"hardcore", false},
                {"pvp", true},
                {"online_mode", false},
                {"spawn_protection", 16}
            }},
            {"world", {
                {"name", "world"},
                {"seed", 0},
                {"generator", "flat"},
                {"spawn_x", 0},
                {"spawn_y", 65},
                {"spawn_z", 0}
            }},
            {"performance", {
                {"io_threads", 4},
                {"worker_threads", 0},
                {"max_chunks_loaded", 1000},
                {"chunk_unload_timeout", 300000},
                {"auto_save_interval", 300000},
                {"compression_threshold", 256},
                {"network_buffer_size", 8192}
            }},
            {"logging", {
                {"level", "info"},
                {"file", "server.log"},
                {"console", true},
                {"max_file_size", 10485760},
                {"max_files", 5}
            }},
            {"security", {
                {"ip_forwarding", false},
                {"max_connections_per_ip", 3},
                {"connection_throttle", 4000},
                {"packet_limit_per_second", 500}
            }}
        };
    }
    
    bool load_from_file() {
        std::lock_guard<std::mutex> lock(config_mutex_);
        
        std::ifstream file(config_path_);
        if (!file.is_open()) {
            save_to_file();
            return false;
        }
        
        try {
            nlohmann::json file_config;
            file >> file_config;
            
            merge_config(config_, file_config);
            return true;
        } catch (const std::exception& e) {
            (void)e;
            return false;
        }
    }
    
    bool save_to_file() const {
        std::lock_guard<std::mutex> lock(config_mutex_);
        
        std::ofstream file(config_path_);
        if (!file.is_open()) return false;
        
        try {
            file << config_.dump(4);
            return true;
        } catch (const std::exception& e) {
            (void)e;  // suppress warning
            return false;
        }
    }
    
    template<typename T>
    T get(const std::string& path, const T& default_value = T{}) const {
        std::lock_guard<std::mutex> lock(config_mutex_);
        
        try {
            nlohmann::json current = config_;
            std::istringstream ss(path);
            std::string token;
            
            while (std::getline(ss, token, '.')) {
                if (current.contains(token)) {
                    current = current[token];
                } else {
                    return default_value;
                }
            }
            
            return current.get<T>();
        } catch (const std::exception& e) {
            (void)e;
            return default_value;
        }
    }
    
    template<typename T>
    void set(const std::string& path, const T& value) {
        std::lock_guard<std::mutex> lock(config_mutex_);
        
        nlohmann::json* current = &config_;
        std::istringstream ss(path);
        std::string token;
        
        std::vector<std::string> tokens;
        while (std::getline(ss, token, '.')) {
            tokens.push_back(token);
        }
        
        for (size_t i = 0; i < tokens.size() - 1; ++i) {
            if (!current->contains(tokens[i]) || !(*current)[tokens[i]].is_object()) {
                (*current)[tokens[i]] = nlohmann::json::object();
            }
            current = &(*current)[tokens[i]];
        }
        
        (*current)[tokens.back()] = value;
    }
    
    std::string get_server_name() const { return get<std::string>("server.name"); }
    std::string get_motd() const { return get<std::string>("server.motd"); }
    std::string get_host() const { return get<std::string>("server.host"); }
    u16 get_port() const { return get<u16>("server.port"); }
    u32 get_max_players() const { return get<u32>("server.max_players"); }
    i32 get_view_distance() const { return get<i32>("server.view_distance"); }
    i32 get_simulation_distance() const { return get<i32>("server.simulation_distance"); }
    bool is_hardcore() const { return get<bool>("server.hardcore"); }
    bool is_pvp_enabled() const { return get<bool>("server.pvp"); }
    bool is_online_mode() const { return get<bool>("server.online_mode"); }
    i32 get_spawn_protection() const { return get<i32>("server.spawn_protection"); }
    
    std::string get_world_name() const { return get<std::string>("world.name"); }
    i64 get_world_seed() const { return get<i64>("world.seed"); }
    std::string get_world_generator() const { return get<std::string>("world.generator"); }
    f64 get_spawn_x() const { return get<f64>("world.spawn_x"); }
    f64 get_spawn_y() const { return get<f64>("world.spawn_y"); }
    f64 get_spawn_z() const { return get<f64>("world.spawn_z"); }
    
    size_t get_io_threads() const { return get<size_t>("performance.io_threads"); }
    size_t get_worker_threads() const { 
        size_t threads = get<size_t>("performance.worker_threads");
        return threads == 0 ? std::thread::hardware_concurrency() : threads;
    }
    size_t get_max_chunks_loaded() const { return get<size_t>("performance.max_chunks_loaded"); }
    i64 get_chunk_unload_timeout() const { return get<i64>("performance.chunk_unload_timeout"); }
    i64 get_auto_save_interval() const { return get<i64>("performance.auto_save_interval"); }
    i32 get_compression_threshold() const { return get<i32>("performance.compression_threshold"); }
    size_t get_network_buffer_size() const { return get<size_t>("performance.network_buffer_size"); }
    
    std::string get_log_level() const { return get<std::string>("logging.level"); }
    std::string get_log_file() const { return get<std::string>("logging.file"); }
    bool is_console_logging() const { return get<bool>("logging.console"); }
    size_t get_max_log_file_size() const { return get<size_t>("logging.max_file_size"); }
    u32 get_max_log_files() const { return get<u32>("logging.max_files"); }
    
private:
    void merge_config(nlohmann::json& base, const nlohmann::json& overlay) {
        for (auto it = overlay.begin(); it != overlay.end(); ++it) {
            if (it.value().is_object() && base.contains(it.key()) && base[it.key()].is_object()) {
                merge_config(base[it.key()], it.value());
            } else {
                base[it.key()] = it.value();
            }
        }
    }
};

extern ServerConfig g_config;

}
