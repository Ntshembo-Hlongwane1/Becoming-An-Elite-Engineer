#pragma once
#include <iostream>

enum class TokKind : int {
    TokField,
    TokTerm,
    TokNum,
    TokLeftParen,
    TokRightParen,
    TokComma,
    TokColon,
    TokFullStop,
    TokCurrencySymbol,
    TokCurrencyAbrv,
    TokEOF
};

struct Token {
  TokKind kind;
    union {
        unsigned int asciiChar;
        int intVal;
        double floatVal;
        char* strPtr;
    } data;
};