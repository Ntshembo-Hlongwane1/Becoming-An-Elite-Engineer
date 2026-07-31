#include <iostream>
#include <cassert>
#include "../ringbuffer.hpp"
#include "../../utils/logger.hpp"

void test_ringbuffer_push_pop() {
    Log("RingBuffer Test", "Running test_ringbuffer_push_pop...");
    RingBuffer<int, 4> rb;
    
    assert(rb.empty());
    assert(!rb.full());
    
    assert(rb.push(10));
    assert(rb.push(20));
    assert(rb.push(30));
    assert(rb.push(40));
    
    assert(rb.full());
    assert(!rb.push(50)); // Should fail as full
    
    int val = 0;
    assert(rb.pop(val) && val == 10);
    assert(rb.pop(val) && val == 20);
    assert(!rb.full());
    
    assert(rb.push(50));
    assert(rb.pop(val) && val == 30);
    assert(rb.pop(val) && val == 40);
    assert(rb.pop(val) && val == 50);
    
    assert(rb.empty());
    
    Log("RingBuffer Test", "  [PASSED] RingBuffer Push and Pop");
}

int main() {
    Log("RingBuffer Test", "========================================");
    Log("RingBuffer Test", "     RingBuffer Unit Test Suite        ");
    Log("RingBuffer Test", "========================================");

    test_ringbuffer_push_pop();

    Log("RingBuffer Test", "All RingBuffer tests passed successfully!");
    return 0;
}
