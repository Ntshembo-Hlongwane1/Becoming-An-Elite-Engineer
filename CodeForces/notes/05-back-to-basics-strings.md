# Back to basics strings

## Character and strings Types

- 'h'
    - Read-only char literal
    - Integral value of type **char**

- "hi"
    - Read-only string literal
    - Type: **const char[3]**
        - Fixed array of constant characters
        - Including **'\0'** at the end
        - Often used as const char* 
        > [] -->['h']['i']['\0']

- std::string
    - Class type of dybamnic sequence of characters
    - Can be modified (variable value and length)
    - Can be empty (default value)

- std::string_view (since c++17)
    - Class type with read-only string API
    - Point to character sequence with size


## Raw string literal

- String value with backslash and 'n' between quotes
> "\"\\n""

- Can be specified as raw string literal:
> R"{"\n"}"

