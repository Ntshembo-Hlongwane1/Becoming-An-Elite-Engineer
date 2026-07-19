#pragma once

enum class TokKind : int {
    TokNum,
    TokPlus,
    TokMinus,
    TokStar,
    TokSlash,
    TokLParen,
    TokRParen,
    TokEOF
};

struct Token {
    TokKind kind;
    double val;
};