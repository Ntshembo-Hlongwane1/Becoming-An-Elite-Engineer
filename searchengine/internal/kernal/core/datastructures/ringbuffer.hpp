#pragma once
#include <atomic>
#include <cstddef>

template<typename T, size_t SIZE>
class RingBuffer{
    private:
        static_assert((SIZE & (SIZE - 1)), "SIZE must be power of 2");
        alignas(64) std::atomic<size_t> write_index(0);
        alignas(64) std::atomic<size_t> read_index(0);
        alignas(64) T slots[SIZE];


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

        

};