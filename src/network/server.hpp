#pragma once

#include "connection.hpp"
#include "core/thread_pool.hpp"
#include <asio.hpp>
#include <unordered_set>
#include <mutex>
#include <atomic>

namespace mc::network {

class NetworkServer {
private:
    asio::io_context io_context_;
    tcp::acceptor acceptor_;
    std::vector<std::thread> io_threads_;
    
    std::unordered_set<ConnectionPtr> connections_;
    std::mutex connections_mutex_;
    std::atomic<u32> total_connections_{0};
    std::atomic<u32> active_connections_{0};
    
    std::atomic<bool> running_{false};
    
    void start_accept() {
        auto socket = std::make_unique<tcp::socket>(io_context_);
        auto raw_socket = socket.get();
        
        acceptor_.async_accept(*raw_socket,
            [this, socket = std::move(socket)](std::error_code ec) mutable {
                if (!ec) {
                    auto connection = std::make_shared<Connection>(std::move(*socket));
                    handle_new_connection(connection);
                }
                
                if (running_.load()) {
                    start_accept();
                }
            });
    }
    
    void handle_new_connection(ConnectionPtr connection) {
        {
            std::lock_guard<std::mutex> lock(connections_mutex_);
            connections_.insert(connection);
        }
        
        total_connections_.fetch_add(1);
        active_connections_.fetch_add(1);
        
        connection->start();
        
        g_thread_pool.submit([this, connection]() {
            monitor_connection(connection);
        });
    }
    
    void monitor_connection(ConnectionPtr connection) {
        while (!connection->is_closed() && running_.load()) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
        
        {
            std::lock_guard<std::mutex> lock(connections_mutex_);
            connections_.erase(connection);
        }
        
        active_connections_.fetch_sub(1);
    }
    
    void cleanup_connections() {
        while (running_.load()) {
            std::this_thread::sleep_for(std::chrono::seconds(30));
            
            std::vector<ConnectionPtr> to_remove;
            
            {
                std::lock_guard<std::mutex> lock(connections_mutex_);
                for (auto it = connections_.begin(); it != connections_.end();) {
                    if ((*it)->is_closed()) {
                        to_remove.push_back(*it);
                        it = connections_.erase(it);
                        active_connections_.fetch_sub(1);
                    } else {
                        ++it;
                    }
                }
            }
        }
    }

public:
    NetworkServer(const std::string& address, u16 port, size_t io_thread_count = 4)
        : acceptor_(io_context_, tcp::endpoint(asio::ip::make_address(address), port)) {
        
        acceptor_.set_option(asio::socket_base::reuse_address(true));
        
        io_threads_.reserve(io_thread_count);
        for (size_t i = 0; i < io_thread_count; ++i) {
            io_threads_.emplace_back([this]() {
                io_context_.run();
            });
        }
    }
    
    ~NetworkServer() {
        stop();
    }
    
    void start() {
        if (running_.exchange(true)) return;
        
        start_accept();
        
        g_thread_pool.submit([this]() {
            cleanup_connections();
        });
    }
    
    void stop() {
        if (!running_.exchange(false)) return;
        
        io_context_.stop();
        
        {
            std::lock_guard<std::mutex> lock(connections_mutex_);
            for (auto& connection : connections_) {
                connection->close();
            }
            connections_.clear();
        }
        
        for (auto& thread : io_threads_) {
            if (thread.joinable()) {
                thread.join();
            }
        }
    }
    
    void broadcast_packet(std::unique_ptr<Packet> packet) {
        std::lock_guard<std::mutex> lock(connections_mutex_);
        
        for (auto& connection : connections_) {
            if (connection->get_state() == ConnectionState::PLAY) {
                auto packet_copy = g_packet_manager.create_packet(
                    packet->get_state(), packet->get_direction(), packet->get_id());
                
                Buffer temp_buffer(1024);
                packet->write(temp_buffer);
                packet_copy->read(temp_buffer);
                
                connection->send_packet(std::move(packet_copy));
            }
        }
    }
    
    std::vector<ConnectionPtr> get_play_connections() const {
        std::vector<ConnectionPtr> play_connections;
        std::lock_guard<std::mutex> lock(connections_mutex_);
        
        for (const auto& connection : connections_) {
            if (connection->get_state() == ConnectionState::PLAY) {
                play_connections.push_back(connection);
            }
        }
        
        return play_connections;
    }
    
    u32 get_total_connections() const { return total_connections_.load(); }
    u32 get_active_connections() const { return active_connections_.load(); }
    u32 get_play_connections_count() const {
        u32 count = 0;
        std::lock_guard<std::mutex> lock(connections_mutex_);
        for (const auto& connection : connections_) {
            if (connection->get_state() == ConnectionState::PLAY) {
                count++;
            }
        }
        return count;
    }
    
    bool is_running() const { return running_.load(); }
};

}
