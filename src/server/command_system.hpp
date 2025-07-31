#include "../src/core/buffer.hpp"
#include "../src/core/memory_pool.hpp"
#include "../src/core/thread_pool.hpp"
#include "../src/world/chunk.hpp"
#include "../src/network/packet_types.hpp"
#include <chrono>
#include <iostream>
#include <vector>
#include <random>
#include <future>

using namespace mc;

class PerformanceBenchmark {
private:
    std::mt19937 rng_;
    
    template<typename Func>
    double measure_time(Func&& func, int iterations = 1000) {
        auto start = std::chrono::high_resolution_clock::now();
        
        for (int i = 0; i < iterations; ++i) {
            func();
        }
        
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
        
        return static_cast<double>(duration.count()) / iterations;
    }

public:
    PerformanceBenchmark() : rng_(std::random_device{}()) {}
    
    void run_all_benchmarks() {
        std::cout << "=== Minecraft Server Performance Benchmarks ===" << std::endl;
        std::cout << std::endl;
        
        test_buffer_performance();
        test_memory_pool_performance();
        test_chunk_operations();
        test_packet_serialization();
        test_threading_performance();
        
        std::cout << "=== Benchmark Complete ===" << std::endl;
    }
    
private:
    void test_buffer_performance() {
        std::cout << "Buffer Performance Tests:" << std::endl;
        
        double write_time = measure_time([this]() {
            Buffer buffer(1024);
            for (int i = 0; i < 100; ++i) {
                buffer.write_varint(rng_() % 1000000);
                buffer.write_string("test_string_" + std::to_string(i));
                buffer.write_be<i64>(rng_());
            }
        });
        
        Buffer test_buffer(8192);
        for (int i = 0; i < 100; ++i) {
            test_buffer.write_varint(rng_() % 1000000);
            test_buffer.write_string("test_string_" + std::to_string(i));
            test_buffer.write_be<i64>(rng_());
        }
        
        double read_time = measure_time([&test_buffer]() {
            Buffer read_buffer = std::move(test_buffer);
            read_buffer.reset();
            
            for (int i = 0; i < 100; ++i) {
                read_buffer.read_varint();
                read_buffer.read_string();
                read_buffer.read_be<i64>();
            }
        });
        
        std::cout << "  Write operations: " << write_time << " μs/operation" << std::endl;
        std::cout << "  Read operations:  " << read_time << " μs/operation" << std::endl;
        std::cout << std::endl;
    }
    
    void test_memory_pool_performance() {
        std::cout << "Memory Pool Performance Tests:" << std::endl;
        
        double pool_alloc_time = measure_time([this]() {
            void* ptr = g_buffer_pool.allocate(1024);
            g_buffer_pool.deallocate(ptr, 1024);
        }, 10000);
        
        double malloc_time = measure_time([]() {
            void* ptr = std::malloc(1024);
            std::free(ptr);
        }, 10000);
        
        std::cout << "  Memory pool allocation: " << pool_alloc_time << " μs/operation" << std::endl;
        std::cout << "  Standard malloc/free:   " << malloc_time << " μs/operation" << std::endl;
        std::cout << "  Performance ratio:      " << malloc_time / pool_alloc_time << "x faster" << std::endl;
        std::cout << std::endl;
    }
    
    void test_chunk_operations() {
        std::cout << "Chunk Operation Performance Tests:" << std::endl;
        
        auto chunk = std::make_shared<world::Chunk>(world::ChunkPos(0, 0));
        
        double generation_time = measure_time([&chunk]() {
            chunk->generate_flat_world();
        }, 10);
        
        double block_access_time = measure_time([&chunk, this]() {
            for (int i = 0; i < 100; ++i) {
                i32 x = rng_() % 16;
                i32 y = rng_() % 64 + 64;
                i32 z = rng_() % 16;
                
                auto block = chunk->get_block(x, y, z);
                chunk->set_block(x, y, z, world::Block(world::STONE));
            }
        });
        
        std::cout << "  Chunk generation:   " << generation_time / 1000.0 << " ms/chunk" << std::endl;
        std::cout << "  Block access:       " << block_access_time << " μs/100 operations" << std::endl;
        std::cout << std::endl;
    }
    
    void test_packet_serialization() {
        std::cout << "Packet Serialization Performance Tests:" << std::endl;
        
        auto keep_alive = std::make_unique<network::play::KeepAlivePacket>(
            12345, network::PacketDirection::CLIENTBOUND);
        
        double serialize_time = measure_time([&keep_alive]() {
            Buffer buffer(64);
            keep_alive->write(buffer);
        });
        
        Buffer test_buffer(64);
        keep_alive->write(test_buffer);
        
        double deserialize_time = measure_time([&test_buffer]() {
            Buffer read_buffer(test_buffer.data(), test_buffer.size());
            auto packet = std::make_unique<network::play::KeepAlivePacket>(
                network::PacketDirection::CLIENTBOUND);
            packet->read(read_buffer);
        });
        
        std::cout << "  Packet serialization:   " << serialize_time << " μs/packet" << std::endl;
        std::cout << "  Packet deserialization: " << deserialize_time << " μs/packet" << std::endl;
        std::cout << std::endl;
    }
    
    void test_threading_performance() {
        std::cout << "Threading Performance Tests:" << std::endl;
        
        const int num_tasks = 10000;
        std::atomic<int> counter{0};
        
        auto start = std::chrono::high_resolution_clock::now();
        
        std::vector<std::future<void>> futures;
        futures.reserve(num_tasks);
        
        for (int i = 0; i < num_tasks; ++i) {
            futures.push_back(g_thread_pool.submit([&counter]() {
                counter.fetch_add(1);
                std::this_thread::sleep_for(std::chrono::microseconds(10));
            }));
        }
        
        for (auto& future : futures) {
            future.wait();
        }
        
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
        
        std::cout << "  " << num_tasks << " tasks completed in " << duration.count() << " ms" << std::endl;
        std::cout << "  Throughput: " << (num_tasks * 1000.0) / duration.count() << " tasks/second" << std::endl;
        std::cout << "  Thread pool size: " << g_thread_pool.size() << " threads" << std::endl;
        std::cout << std::endl;
    }
};

void run_memory_stress_test() {
    std::cout << "Memory Stress Test:" << std::endl;
    
    std::vector<std::unique_ptr<Buffer>> buffers;
    buffers.reserve(10000);
    
    auto start = std::chrono::high_resolution_clock::now();
    
    for (int i = 0; i < 10000; ++i) {
        auto buffer = std::make_unique<Buffer>(1024 + (i % 4096));
        
        for (int j = 0; j < 100; ++j) {
            buffer->write_varint(i * j);
            buffer->write_string("stress_test_" + std::to_string(j));
        }
        
        buffers.push_back(std::move(buffer));
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    size_t total_memory = 0;
    for (const auto& buffer : buffers) {
        total_memory += buffer->size();
    }
    
    std::cout << "  Created 10,000 buffers in " << duration.count() << " ms" << std::endl;
    std::cout << "  Total memory used: " << total_memory / (1024 * 1024) << " MB" << std::endl;
    std::cout << "  Average buffer size: " << total_memory / buffers.size() << " bytes" << std::endl;
    std::cout << std::endl;
}

void run_concurrent_chunk_test() {
    std::cout << "Concurrent Chunk Access Test:" << std::endl;
    
    const int num_chunks = 100;
    const int num_threads = 8;
    const int operations_per_thread = 1000;
    
    std::vector<std::shared_ptr<world::Chunk>> chunks;
    chunks.reserve(num_chunks);
    
    for (int i = 0; i < num_chunks; ++i) {
        auto chunk = std::make_shared<world::Chunk>(world::ChunkPos(i % 10, i / 10));
        chunk->generate_flat_world();
        chunks.push_back(chunk);
    }
    
    std::atomic<int> total_operations{0};
    auto start = std::chrono::high_resolution_clock::now();
    
    std::vector<std::future<void>> futures;
    futures.reserve(num_threads);
    
    for (int t = 0; t < num_threads; ++t) {
        futures.push_back(g_thread_pool.submit([&chunks, &total_operations, operations_per_thread]() {
            std::mt19937 rng(std::random_device{}());
            
            for (int i = 0; i < operations_per_thread; ++i) {
                auto& chunk = chunks[rng() % chunks.size()];
                
                i32 x = rng() % 16;
                i32 y = rng() % 64 + 64;
                i32 z = rng() % 16;
                
                auto block = chunk->get_block(x, y, z);
                chunk->set_block(x, y, z, world::Block(world::STONE));
                
                total_operations.fetch_add(1);
            }
        }));
    }
    
    for (auto& future : futures) {
        future.wait();
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    std::cout << "  " << total_operations.load() << " concurrent operations in " << duration.count() << " ms" << std::endl;
    std::cout << "  Throughput: " << (total_operations.load() * 1000.0) / duration.count() << " ops/second" << std::endl;
    std::cout << "  " << num_threads << " threads, " << num_chunks << " chunks" << std::endl;
    std::cout << std::endl;
}

int main() {
    std::cout << "Minecraft Server Performance Benchmark Suite" << std::endl;
    std::cout << "=============================================" << std::endl;
    std::cout << std::endl;
    
    PerformanceBenchmark benchmark;
    benchmark.run_all_benchmarks();
    
    run_memory_stress_test();
    run_concurrent_chunk_test();
    
    std::cout << "All benchmarks completed successfully!" << std::endl;
    
    return 0;
}
