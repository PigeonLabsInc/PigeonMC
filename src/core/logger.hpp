#pragma once

#include "types.hpp"
#include "config.hpp"
#include <string>
#include <fstream>
#include <iostream>
#include <sstream>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <iomanip>
#include <algorithm>
#include <cstdio>

namespace mc {

enum class LogLevel : u8 {
    TRACE = 0,
    DEBUG = 1,
    INFO = 2,
    WARN = 3,
    ERROR = 4,
    FATAL = 5
};

struct LogEntry {
    LogLevel level;
    std::string message;
    std::string category;
    timestamp_t timestamp;
    std::thread::id thread_id;
    
    LogEntry(LogLevel lvl, const std::string& msg, const std::string& cat)
        : level(lvl), message(msg), category(cat)
        , timestamp(std::chrono::steady_clock::now())
        , thread_id(std::this_thread::get_id()) {}
};

class Logger {
private:
    std::queue<LogEntry> log_queue_;
    std::mutex queue_mutex_;
    std::condition_variable queue_cv_;
    
    std::ofstream log_file_;
    std::mutex file_mutex_;
    
    std::thread writer_thread_;
    std::atomic<bool> running_{false};
    std::atomic<LogLevel> min_level_{LogLevel::INFO};
    
    bool console_output_;
    std::string log_file_path_;
    size_t max_file_size_;
    u32 max_files_;
    size_t current_file_size_;
    
    void writer_loop() {
        while (running_.load()) {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            queue_cv_.wait(lock, [this] { return !log_queue_.empty() || !running_.load(); });
            
            std::queue<LogEntry> local_queue;
            local_queue.swap(log_queue_);
            lock.unlock();
            
            while (!local_queue.empty()) {
                process_log_entry(local_queue.front());
                local_queue.pop();
            }
        }
    }
    
    void process_log_entry(const LogEntry& entry) {
        if (entry.level < min_level_.load()) return;
        
        std::string formatted = format_log_entry(entry);
        
        if (console_output_) {
            std::cout << formatted << std::endl;
        }
        
        write_to_file(formatted);
    }
    
    std::string format_log_entry(const LogEntry& entry) {
        std::ostringstream oss;
        
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()) % 1000;
        
        oss << "[" << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S");
        oss << "." << std::setfill('0') << std::setw(3) << ms.count() << "] ";
        oss << "[" << level_to_string(entry.level) << "] ";
        
        if (!entry.category.empty()) {
            oss << "[" << entry.category << "] ";
        }
        
        oss << entry.message;
        
        return oss.str();
    }
    
    std::string level_to_string(LogLevel level) {
        switch (level) {
            case LogLevel::TRACE: return "TRACE";
            case LogLevel::DEBUG: return "DEBUG";
            case LogLevel::INFO:  return "INFO ";
            case LogLevel::WARN:  return "WARN ";
            case LogLevel::ERROR: return "ERROR";
            case LogLevel::FATAL: return "FATAL";
            default: return "UNKNOWN";
        }
    }
    
    void write_to_file(const std::string& message) {
        std::lock_guard<std::mutex> lock(file_mutex_);
        
        if (!log_file_.is_open()) return;
        
        log_file_ << message << "\n";
        log_file_.flush();
        
        current_file_size_ += message.length() + 1;
        
        if (current_file_size_ >= max_file_size_) {
            rotate_log_file();
        }
    }
    
    void rotate_log_file() {
        log_file_.close();
        
        for (u32 i = max_files_ - 1; i > 0; --i) {
            std::string old_name = log_file_path_ + "." + std::to_string(i);
            std::string new_name = log_file_path_ + "." + std::to_string(i + 1);
            std::rename(old_name.c_str(), new_name.c_str());
        }
        
        std::string backup_name = log_file_path_ + ".1";
        std::rename(log_file_path_.c_str(), backup_name.c_str());
        
        log_file_.open(log_file_path_, std::ios::out | std::ios::app);
        current_file_size_ = 0;
    }

public:
    Logger() : console_output_(true), max_file_size_(10485760), max_files_(5), current_file_size_(0) {}
    
    ~Logger() {
        shutdown();
    }
    
    void initialize() {
        console_output_ = g_config.is_console_logging();
        log_file_path_ = g_config.get_log_file();
        max_file_size_ = g_config.get_max_log_file_size();
        max_files_ = g_config.get_max_log_files();
        min_level_.store(string_to_level(g_config.get_log_level()));
        
        if (!log_file_path_.empty()) {
            std::lock_guard<std::mutex> lock(file_mutex_);
            log_file_.open(log_file_path_, std::ios::out | std::ios::app);
            
            if (log_file_.is_open()) {
                log_file_.seekp(0, std::ios::end);
                current_file_size_ = log_file_.tellp();
            }
        }
        
        running_.store(true);
        writer_thread_ = std::thread(&Logger::writer_loop, this);
    }
    
    void shutdown() {
        if (!running_.exchange(false)) return;
        
        queue_cv_.notify_all();
        
        if (writer_thread_.joinable()) {
            writer_thread_.join();
        }
        
        std::lock_guard<std::mutex> lock(file_mutex_);
        if (log_file_.is_open()) {
            log_file_.close();
        }
    }
    
    void log(LogLevel level, const std::string& message, const std::string& category = "") {
        if (level < min_level_.load()) return;
        
        {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            log_queue_.emplace(level, message, category);
        }
        
        queue_cv_.notify_one();
    }
    
    void trace(const std::string& message, const std::string& category = "") {
        log(LogLevel::TRACE, message, category);
    }
    
    void debug(const std::string& message, const std::string& category = "") {
        log(LogLevel::DEBUG, message, category);
    }
    
    void info(const std::string& message, const std::string& category = "") {
        log(LogLevel::INFO, message, category);
    }
    
    void warn(const std::string& message, const std::string& category = "") {
        log(LogLevel::WARN, message, category);
    }
    
    void error(const std::string& message, const std::string& category = "") {
        log(LogLevel::ERROR, message, category);
    }
    
    void fatal(const std::string& message, const std::string& category = "") {
        log(LogLevel::FATAL, message, category);
    }
    
    void set_level(LogLevel level) {
        min_level_.store(level);
    }
    
    LogLevel get_level() const {
        return min_level_.load();
    }
    
    LogLevel string_to_level(const std::string& level_str) {
        std::string lower_level = level_str;
        std::transform(lower_level.begin(), lower_level.end(), lower_level.begin(), ::tolower);
        
        if (lower_level == "trace") return LogLevel::TRACE;
        if (lower_level == "debug") return LogLevel::DEBUG;
        if (lower_level == "info") return LogLevel::INFO;
        if (lower_level == "warn" || lower_level == "warning") return LogLevel::WARN;
        if (lower_level == "error") return LogLevel::ERROR;
        if (lower_level == "fatal") return LogLevel::FATAL;
        
        return LogLevel::INFO;
    }
};

extern Logger g_logger;

#define LOG_TRACE(msg) g_logger.trace(msg)
#define LOG_DEBUG(msg) g_logger.debug(msg)
#define LOG_INFO(msg) g_logger.info(msg)
#define LOG_WARN(msg) g_logger.warn(msg)
#define LOG_ERROR(msg) g_logger.error(msg)
#define LOG_FATAL(msg) g_logger.fatal(msg)

#define LOG_CATEGORY_TRACE(cat, msg) g_logger.trace(msg, cat)
#define LOG_CATEGORY_DEBUG(cat, msg) g_logger.debug(msg, cat)
#define LOG_CATEGORY_INFO(cat, msg) g_logger.info(msg, cat)
#define LOG_CATEGORY_WARN(cat, msg) g_logger.warn(msg, cat)
#define LOG_CATEGORY_ERROR(cat, msg) g_logger.error(msg, cat)
#define LOG_CATEGORY_FATAL(cat, msg) g_logger.fatal(msg, cat)

}
