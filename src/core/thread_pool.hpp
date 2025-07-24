#pragma once

#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <atomic>
#include <future>
#include <random>

namespace mc {

class ThreadPool {
private:
    struct WorkerData {
        std::queue<std::function<void()>> queue;
        std::mutex mutex;
        std::condition_variable cv;
        std::atomic<bool> shutdown{false};
    };
    
    std::vector<std::unique_ptr<WorkerData>> workers_;
    std::vector<std::thread> threads_;
    std::atomic<size_t> next_worker_{0};
    std::atomic<bool> shutdown_{false};
    
    void worker_thread(size_t worker_id) {
        auto& worker = *workers_[worker_id];
        std::random_device rd;
        std::mt19937 gen(rd());
        
        while (!shutdown_.load()) {
            std::function<void()> task;
            
            {
                std::unique_lock<std::mutex> lock(worker.mutex);
                worker.cv.wait(lock, [&] {
                    return !worker.queue.empty() || worker.shutdown.load();
                });
                
                if (worker.shutdown.load() && worker.queue.empty()) {
                    break;
                }
                
                if (!worker.queue.empty()) {
                    task = std::move(worker.queue.front());
                    worker.queue.pop();
                }
            }
            
            if (task) {
                task();
                continue;
            }
            
            bool found_work = false;
            std::uniform_int_distribution<size_t> dist(0, workers_.size() - 1);
            
            for (size_t attempts = 0; attempts < workers_.size(); ++attempts) {
                size_t target = dist(gen);
                if (target == worker_id) continue;
                
                auto& target_worker = *workers_[target];
                std::unique_lock<std::mutex> lock(target_worker.mutex, std::try_to_lock);
                
                if (lock.owns_lock() && !target_worker.queue.empty()) {
                    task = std::move(target_worker.queue.front());
                    target_worker.queue.pop();
                    found_work = true;
                    break;
                }
            }
            
            if (found_work && task) {
                task();
            }
        }
    }
    
public:
    explicit ThreadPool(size_t num_threads = std::thread::hardware_concurrency()) {
        workers_.reserve(num_threads);
        threads_.reserve(num_threads);
        
        for (size_t i = 0; i < num_threads; ++i) {
            workers_.emplace_back(std::make_unique<WorkerData>());
            threads_.emplace_back(&ThreadPool::worker_thread, this, i);
        }
    }
    
    ~ThreadPool() {
        shutdown();
    }
    
    template<typename F, typename... Args>
    auto submit(F&& f, Args&&... args) -> std::future<std::invoke_result_t<F, Args...>> {
        using return_type = std::invoke_result_t<F, Args...>;
        
        auto task = std::make_shared<std::packaged_task<return_type()>>(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...)
        );
        
        std::future<return_type> result = task->get_future();
        
        size_t worker_id = next_worker_.fetch_add(1) % workers_.size();
        auto& worker = *workers_[worker_id];
        
        {
            std::lock_guard<std::mutex> lock(worker.mutex);
            if (worker.shutdown.load()) {
                throw std::runtime_error("ThreadPool is shutting down");
            }
            worker.queue.emplace([task] { (*task)(); });
        }
        
        worker.cv.notify_one();
        return result;
    }
    
    void shutdown() {
        if (shutdown_.exchange(true)) return;
        
        for (auto& worker : workers_) {
            worker->shutdown.store(true);
            worker->cv.notify_all();
        }
        
        for (auto& thread : threads_) {
            if (thread.joinable()) {
                thread.join();
            }
        }
    }
    
    size_t size() const { return threads_.size(); }
};

extern ThreadPool g_thread_pool;

}
