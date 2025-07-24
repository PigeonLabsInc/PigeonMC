#pragma once

#include <memory>
#include <vector>
#include <stack>
#include <mutex>
#include <atomic>
#include <cstdlib>
#include <new>

namespace mc {

template<size_t BlockSize, size_t BlockCount>
class MemoryPool {
private:
    struct Block {
        alignas(std::max_align_t) char data[BlockSize];
    };
    
    std::unique_ptr<Block[]> memory_;
    std::stack<void*> free_blocks_;
    std::mutex mutex_;
    std::atomic<size_t> allocated_count_{0};
    
public:
    MemoryPool() : memory_(std::make_unique<Block[]>(BlockCount)) {
        for (size_t i = 0; i < BlockCount; ++i) {
            free_blocks_.push(&memory_[i]);
        }
    }
    
    void* allocate() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (free_blocks_.empty()) {
            return std::malloc(BlockSize);
        }
        
        void* ptr = free_blocks_.top();
        free_blocks_.pop();
        allocated_count_.fetch_add(1);
        return ptr;
    }
    
    void deallocate(void* ptr) {
        if (!ptr) return;
        
        std::lock_guard<std::mutex> lock(mutex_);
        
        bool is_pool_memory = ptr >= memory_.get() && 
                             ptr < memory_.get() + BlockCount;
        
        if (is_pool_memory) {
            free_blocks_.push(ptr);
            allocated_count_.fetch_sub(1);
        } else {
            std::free(ptr);
        }
    }
    
    size_t allocated_count() const { return allocated_count_.load(); }
    size_t available_count() const { return BlockCount - allocated_count(); }
};

class BufferPool {
private:
    MemoryPool<1024, 512> small_pool_;
    MemoryPool<4096, 256> medium_pool_;
    MemoryPool<16384, 128> large_pool_;
    
public:
    void* allocate(size_t size) {
        if (size <= 1024) return small_pool_.allocate();
        if (size <= 4096) return medium_pool_.allocate();
        if (size <= 16384) return large_pool_.allocate();
        return std::malloc(size);
    }
    
    void deallocate(void* ptr, size_t size) {
        if (!ptr) return;
        
        if (size <= 1024) small_pool_.deallocate(ptr);
        else if (size <= 4096) medium_pool_.deallocate(ptr);
        else if (size <= 16384) large_pool_.deallocate(ptr);
        else std::free(ptr);
    }
};

extern BufferPool g_buffer_pool;

}
