#pragma once
#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <vector>
#include <functional>

class ThreadPool {
    std::vector<std::thread> workers;
    std::queue<int> tasks;
    std::mutex mutex;
    std::condition_variable cv;
    bool stop = false;
    std::function<void(int)> handler;

public:
    ThreadPool(size_t num_threads, std::function<void(int)> handler)
        : handler(std::move(handler))
    {
        for (size_t i = 0; i < num_threads; ++i) {
            workers.emplace_back([this]() {
                while (true) {
                    int socket;
                    {
                        std::unique_lock<std::mutex> lock(this->mutex);
                        this->cv.wait(lock, [this] {
                            return this->stop || !this->tasks.empty();
                        });
                        if (this->stop && this->tasks.empty()) return;
                        socket = this->tasks.front();
                        this->tasks.pop();
                    }
                    this->handler(socket);
                }
            });
        }
    }

    void submit(int socket) {
        {
            std::lock_guard<std::mutex> lock(mutex);
            tasks.push(socket);
        }
        cv.notify_one();
    }

    ~ThreadPool() {
        {
            std::lock_guard<std::mutex> lock(mutex);
            stop = true;
        }
        cv.notify_all();
        for (auto& w : workers) {
            if (w.joinable()) w.join();
        }
    }
};
