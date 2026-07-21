# Concurrency
---
## Table content:
1. What is data race and how do we fix it?
2. C++11 mutex and RAII lock types
3. condition_variables 
4. Static initialization an once_flag
5. New C++17 and C++20 primitives
6. The blue/green pattern
7. Bonus C++20


### What is concurrency?

- Concurrency means doing two things -- "running togethe." Maybe you're switching back and forth between them.

- Parallelism means doing two things in parallel -- simultaneously

> Standard C++03 didn't have "threads." You'd just use some platform specific lib, such as pthreads.

- C+11 gave us a memory model, programs consists of 1 or more threads of execution
- This being great came with its own issue that being syncronization. Every write to a single memory location must sync with all other read or writes of that memory location or else program has undefined behaviour
- Sync relationships can be established using various standard libs like ```cpp std::mutex``` and ```cppp std::atomic<T>```

### Starting a new thread
> In C+003 pthreads, you'd create a new thread by calling a third-party lib func

- In C++11, the standard lib "own" creating new threads
```cpp
std::thread threadZ = std::thread([](){
    puts("Hellow world from threadZ")
})
```

- ```.join()``` to be called once the thread function is done 
```cpp
std::thread threadZ = std::thread([](){
    puts("Hellow world from threadZ")
})
puts("Hellow world from main thread");
threadZ.join();
```

### Example of of data race on an int
```cpp
using sc = std::chrono::steady_clock;
auto deadline = sc::now() + std::chrono::seconds(10);

int counter = 0;

std::thread threadB = std::thread([&](){
    while(sc::now() < deadline){
        std::cout << ++counter << std::endl;
    };
});

while (sc::now() < deadline){
    std::cout << ++counter << std::endl;
}

threadB.join()
```
The data race is happening in the thread call back function ```++counter``` and the main thread's ```++counter```, there's is no sync between the 2 accesses

### Fixing the race via std::atomic<T>
```cpp
using sc = std::chrono::steady_clock;
auto deadline = sc::now() + std::chrono::seconds(10);

std::atomic<int> counter = 0;

std::thread threadB = std::thread([&](){
    while(sc::now() < deadline){
        std::cout << ++counter << std::endl;
    };
});

while (sc::now() < deadline){
    std::cout << ++counter << std::endl;
}

threadB.join()
```
This minor change fixes the physical data race. Every access to an atomic implicitly syncs with every other access to it

> There's still a "semantic data race" different valid executions will produce different outputs.