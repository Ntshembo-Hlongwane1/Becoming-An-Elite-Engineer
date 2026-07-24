# Operators and Operator Overloading 

**Operator Overloading:** Overloading in this sense mean giving a new meaning to x or adding params to x, you are allowed to define or change the behviour of program

**Operators:** Are just functions 

## Example
```cpp
struct Vector2{
    int float x, y

    Vector2(float x, float y) : x(x), y(y){}

    Vector2 operator+(const Vector& other) const
    Vector2 operator-(const Vector& other) const
    Vector2 operator/(const Vector& other) const
    Vector2 operator*(const Vector& other) const

}
```