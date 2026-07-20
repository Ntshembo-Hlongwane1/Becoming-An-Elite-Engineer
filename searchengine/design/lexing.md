# SonarSearch Engine's Lexer Design Documentation 

> A little brief about what is lexining: So think about the most basic example which is 2+3 and you punch that in your calculator what is happening there you is you see 5 being returned back to -- But in actual sense that is a lot that is happening under the hood in order to unerstand what you just punched in to the calculator and that is called lexing 

> So continuing with that example for you to see 5 what happens is the program in that calculator goes through character by character in order to make sense or life to what you just passed and that is lexing.


# SonarLexer

- By default this lexer will be inventory / store / ecom aware lexer


# What the lexer recognizes
- It will recongnize a field, a field in this context we referring to `ProductTitle: <Something>` or `SomeKey: <SomeValue>`

> Example suppose the lexer sees Productitle: Apples 2kg --> The lexer will produce:
```cpp
{
    TokKind -> TokField,
    strPtr -> *it
}
```


- It will reconginize a term, a term in this context we referring to individual words i.e `maize, meal, powder, etc..`

> Example suppose the lexer see the word, maize meal --> The lexer will produce
```cpp
{
    TokKind -> TokTerm,
    strPtr -> *it
}
```

- It will recongnize numbers, a number in this context we referring to `1, 1.5, 100.5, etc...`

> Example suppose the lexer see the number, 1 or 1.5 --> The lexer will produce
```cpp
{
    TokKind -> TokNum,
    intVal | floatVal -> 1 | 1.5
}
```

- It will reconginize Left and Right parenthes

> Example supose the lelxer sees the left or right parenthenses i.e '(' ')' --> The lexer will produce
```cpp
{
    TokKind -> TokLeftParen | TokRightParen,
    asciiChar -> &#40 | &#41
}
```

- It will recongnize the following punctautions `.':,`
> Example suppose the lexer sees the following punctuations depending on the type --> The lexer will produce
```cpp
{
    TokKind -> TokComma | TokColon | TokFullStop,
    asciiChar -> &#39 etc...
}
```


- It will recongize Currency Symbols `$, R, £`
> Example suppose the lexer sees the following currency symbol depending on the type --> The lexer will produce
```cpp
{
    TokKind -> TokCurrencySymbol.
    asciiChar -> &#36; etc...
}
```
- It will recongize currency abrev
> Example suppose the lexer sees the following currency symbol depending on the type --> The lexer will produce
```cpp
{
    TokKind -> TokCurrencyAbrv
    asciiChar -> ZAR, USD etc...
}
```
- It will mark end of product scanned and and EOF


# Token Representation

```cpp
struct Token {
  TokKind kind;
    union {
        unsigned int asciiChar;
        int intVal;
        char* strPtr;
    } data;
};
```

## Why the chossen structure?
- `TokKind` -> will give me the type of during eval time
- Then `union` allowing it to be various types was intentional for various reasons:

    - Firstly: Low memory footprint (max 8bytes / each line read and can be dynamically lowered to not waste)
    - Secondly: Flexibility in handling decimal numbers (prices) and letters/words
    - 


# Performance & Memory 

## Perfomance
> First iteration this document is a living doc where I'm learing

### First iteration 
- The idea for now is I will be reading 1 line a time of each document 
- Single Thread I do not know my bottle necks yet or where heavy work is being done I have some assumptions though 

    #### Assumptions for perfomance:
        - Having direct reference to line pointer 
        - So I know that I will have to step N time that line e.g a line is 'ProductTile: Cooking Oil'
        - Now I now that for each each I can do something like this below to not waste the 8bytes and use min bytes I can per token (Falls a bit in memory section but makes sense to follow on the next line)
            ```cp
                tok.data.strPtr = new char[line.length() + 1];
            ```
            > How many heap allocations does this produce over a 1.1 GB corpus?

        - Now with that said as now I have all the inf to create this Token  I need to ovoid copies which is now increasing the number of steps till I can just insert GOAL is to this is emplace/inplace  OPTION 2 is also looking at moving the token direct pointer redirect?



    #### Assumptions for memory:
        - Max 8bytes should give me a good enough low foot print read per line -- NOT SURE IF I CAN DO BETTER AND COPMPRESS?

        > Why 8bytes? We have a union which say this Token can either be an unsigned int which is 4bytes or it could be an int which is 4 bytes and we then have double and char* which is 8 bytes ==> sizeof(Token) can be max of 8bytes

        - Per max expected from looking at sample data is line being 455bytes long obvs I'm not chunking all that at once but why is this important to note? 

            > 1. Most lines won't be that long
            > 2. As we tokenize and pass this stream of token to the parser That max 455bytes/per line get freed on each iteration / line

        - So if per line our max is 455bytes where if per line we get 5 Tokens that means we have 40bytes per line in memory of the lexer for a short periof and streamed out to parser  -- COULD I DO LESS? IDK?


# Non Goal
- Phrase search
- Wildcards



# Open Questons

- Should string be interned?

- Should tokens own memory?

- Should lexer allocate memory

- Should unicode normalization happen here or later?