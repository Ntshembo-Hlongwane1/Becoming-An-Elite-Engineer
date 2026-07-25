#pragma once
#include <atomic>
#include <cstddef>
#include <mutex>
#include <condition_variable>

template<typename T, size_t SIZE>
class RingBuffer{
    private:
        static_assert((SIZE & (SIZE - 1)) == 0, "SIZE must be power of 2");
        alignas(64) std::atomic<size_t> write_index{0};
        alignas(64) std::atomic<size_t> read_index{0};
        alignas(64) T slots[SIZE];

        std::mutex mtx;
        std::condition_variable not_full;
        std::condition_variable not_empty;

    public:
        bool push(const T& item){
            size_t write = write_index.load(std::memory_order_relaxed);
            size_t read = read_index.load(std::memory_order_acquire);

            if ((write - read) >= SIZE){
                return false;
            };

            slots[write & (SIZE - 1)] = item;
            write_index.store(write + 1, std::memory_order_release);
            return true;
        };

        bool pop(T& item){
            size_t read = read_index.load(std::memory_order_relaxed);
            size_t write = write_index.load(std::memory_order_acquire);

            if (read >= write){
                return false;
            };

            item = slots[read & (SIZE - 1)];

            read_index.store(read + 1, std::memory_order_release);
            return true;
        };

        bool push_blocking(const T& item){
            std::unique_lock<std::mutex> lock(mtx);

            not_full.wait(lock, [this] {
                size_t w = write_index.load(std::memory_order_relaxed);
                size_t r = read_index.load(std::memory_order_acquire);

                return (w - r) < SIZE;
            });

            size_t write = write_index.load(std::memory_order_relaxed);
            slots[write & (SIZE - 1)] = item;
            write_index.store(write + 1, std::memory_order_release);

            lock.unlock();
            not_empty.notify_one();
            return true;
        };

        void pop_blocking(T& item){
            std::unique_lock<std::mutex> lock(mtx);

            not_empty.wait(lock, [this] {
                size_t r = read_index.load(std::memory_order_relaxed);
                size_t w = write_index.load(std::memory_order_acquire);
                return r < w;
            });

            size_t read = read_index.load(std::memory_order_relaxed);
            item = slots[read & (SIZE - 1)];
            read_index.store(read + 1, std::memory_order_release);

            lock.unlock();
            not_full.notify_one();
        };

        bool full() const {
            size_t w = write_index.load(std::memory_order_acquire);
            size_t r = read_index.load(std::memory_order_acquire);
            return (w - r) >= SIZE;
        };
        
        bool empty() const {
            size_t read = read_index.load(std::memory_order_acquire);
            size_t write = write_index.load(std::memory_order_acquire);
            return read >= write;
        };
};