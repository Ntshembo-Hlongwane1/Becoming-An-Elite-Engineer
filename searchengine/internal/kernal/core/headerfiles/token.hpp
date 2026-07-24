#pragma once
#include <iostream>
#include <string_view>


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
    std::string_view value;
};