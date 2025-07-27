#pragma once

#include "types.hpp"
#include "memory_pool.hpp"
#include <vector>
#include <memory>
#include <cstring>
#include <stdexcept>

namespace mc {

class Buffer {
private:
    byte* data_;
    size_t size_;
    size_t capacity_;
    size_t read_pos_;
    size_t write_pos_;
    bool owns_memory_;
    
    void ensure_capacity(size_t needed) {
        if (capacity_ >= needed) return;
        
        size_t new_capacity = std::max(needed, capacity_ * 2);
        byte* new_data = static_cast<byte*>(g_buffer_pool.allocate(new_capacity));
        
        if (data_ && size_ > 0) {
            std::memcpy(new_data, data_, size_);
        }
        
        if (owns_memory_ && data_) {
            g_buffer_pool.deallocate(data_, capacity_);
        }
        
        data_ = new_data;
        capacity_ = new_capacity;
        owns_memory_ = true;
    }
    
public:
    Buffer() : data_(nullptr), size_(0), capacity_(0), read_pos_(0), write_pos_(0), owns_memory_(false) {}
    
    explicit Buffer(size_t initial_capacity) 
        : data_(static_cast<byte*>(g_buffer_pool.allocate(initial_capacity)))
        , size_(0), capacity_(initial_capacity), read_pos_(0), write_pos_(0), owns_memory_(true) {}
    
    Buffer(const byte* data, size_t size) 
        : data_(const_cast<byte*>(data)), size_(size), capacity_(size)
        , read_pos_(0), write_pos_(size), owns_memory_(false) {}
    
    ~Buffer() {
        if (owns_memory_ && data_) {
            g_buffer_pool.deallocate(data_, capacity_);
        }
    }
    
    Buffer(const Buffer&) = delete;
    Buffer& operator=(const Buffer&) = delete;
    
    Buffer(Buffer&& other) noexcept
        : data_(other.data_), size_(other.size_), capacity_(other.capacity_)
        , read_pos_(other.read_pos_), write_pos_(other.write_pos_)
        , owns_memory_(other.owns_memory_) {
        other.data_ = nullptr;
        other.owns_memory_ = false;
    }
    
    Buffer& operator=(Buffer&& other) noexcept {
        if (this != &other) {
            if (owns_memory_ && data_) {
                g_buffer_pool.deallocate(data_, capacity_);
            }
            
            data_ = other.data_;
            size_ = other.size_;
            capacity_ = other.capacity_;
            read_pos_ = other.read_pos_;
            write_pos_ = other.write_pos_;
            owns_memory_ = other.owns_memory_;
            
            other.data_ = nullptr;
            other.owns_memory_ = false;
        }
        return *this;
    }
    
    void write(const void* data, size_t len) {
        ensure_capacity(write_pos_ + len);
        std::memcpy(data_ + write_pos_, data, len);
        write_pos_ += len;
        size_ = std::max(size_, write_pos_);
    }
    
    void write_byte(byte value) {
        write(&value, 1);
    }
    
    void write_varint(i32 value) {
        u32 uvalue = static_cast<u32>(value);
        while (uvalue >= 0x80) {
            write_byte(static_cast<byte>(uvalue | 0x80));
            uvalue >>= 7;
        }
        write_byte(static_cast<byte>(uvalue));
    }
    
    void write_varlong(i64 value) {
        u64 uvalue = static_cast<u64>(value);
        while (uvalue >= 0x80) {
            write_byte(static_cast<byte>(uvalue | 0x80));
            uvalue >>= 7;
        }
        write_byte(static_cast<byte>(uvalue));
    }
    
    void write_string(const std::string& str) {
        write_varint(static_cast<i32>(str.length()));
        write(str.data(), str.length());
    }
    
    template<typename T>
    void write_be(T value) {
        static_assert(std::is_integral_v<T>, "Type must be integral");
        if constexpr (sizeof(T) == 1) {
            write_byte(static_cast<byte>(value));
        } else {
            for (int i = sizeof(T) - 1; i >= 0; --i) {
                write_byte(static_cast<byte>(value >> (i * 8)));
            }
        }
    }
    
    size_t read(void* dest, size_t len) {
        size_t available = size_ - read_pos_;
        size_t to_read = std::min(len, available);
        
        if (to_read > 0) {
            std::memcpy(dest, data_ + read_pos_, to_read);
            read_pos_ += to_read;
        }
        
        return to_read;
    }
    
    byte read_byte() {
        if (read_pos_ >= size_) throw std::runtime_error("Buffer underflow");
        return data_[read_pos_++];
    }
    
    i32 read_varint() {
        i32 result = 0;
        int shift = 0;
        byte b;
        
        do {
            if (shift >= 32) throw std::runtime_error("VarInt too big");
            b = read_byte();
            result |= (b & 0x7F) << shift;
            shift += 7;
        } while (b & 0x80);
        
        return result;
    }
    
    i64 read_varlong() {
        i64 result = 0;
        int shift = 0;
        byte b;
        
        do {
            if (shift >= 64) throw std::runtime_error("VarLong too big");
            b = read_byte();
            result |= static_cast<i64>(b & 0x7F) << shift;
            shift += 7;
        } while (b & 0x80);
        
        return result;
    }
    
    std::string read_string() {
        i32 length = read_varint();
        if (length < 0 || length > 32767) {
            throw std::runtime_error("Invalid string length");
        }
        
        std::string result(length, '\0');
        read(result.data(), length);
        return result;
    }
    
    template<typename T>
    T read_be() {
        static_assert(std::is_integral_v<T>, "Type must be integral");
        if constexpr (sizeof(T) == 1) {
            return static_cast<T>(read_byte());
        } else {
            T result = 0;
            for (size_t i = 0; i < sizeof(T); ++i) {
                result = (result << 8) | read_byte();
            }
            return result;
        }
    }
    
    byte* data() { return data_; }
    const byte* data() const { return data_; }
    size_t size() const { return size_; }
    size_t readable() const { return size_ - read_pos_; }
    size_t writable() const { return capacity_ - write_pos_; }
    
    void reset() { read_pos_ = write_pos_ = size_ = 0; }
    void clear() { reset(); }
    
    void reserve(size_t capacity) {
        if (capacity > capacity_) {
            ensure_capacity(capacity);
        }
    }
};

}
