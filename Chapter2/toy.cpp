#include <cstdio>

// Lexer

enum Token {
    tok_eof = -1,

    tok_def = -2,
    tok_extern = -3,
};