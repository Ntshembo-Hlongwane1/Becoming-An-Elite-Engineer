# Back To Basics Functions

An Abstraction for writing reuseable and modular code
> Something that returns a value

## Basic Function Example
```cpp
void prompt(){
    std::cout << "Hellow World" << std::endl;
};
```

### Function Call

What happens in the machine when we call a function?

```cpp
int add(int a, int b){
    return a + b;
}
```

when you call ```add(8, 1)``` at the assembly level is replace with a 'call' instruction `Z3addii`\

### Creating Libraries with Functions

**Declaration**
`int add(int a , int b)`

**Definition** (Forward declaration)
```cpp
int add(int a, int b){
    return a + b;
}
```

### Grouping Libs of functions
```cpp
namespace mynamespace{
    int add(int a, int b){
        return a + b;
    }
    int sub(int a, int b){
        return a - b;
    }
}
mynamespace::add(7, 2)
```
> Recommend to be at local scope than Global

### constexpr Functions
Compute at compile-time
```cpp
constexpr int square(int x){
    return x * x;
}
```
