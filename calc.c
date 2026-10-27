// Abhishek Bisht 241ADB121
// Compile with: gcc -O2 -Wall -Wextra -std=c17 -o calc calc.c -lm
// Resources used:
// OpenAI ChatGPT - help with design, code, explanations and testing.
// GCC manual - reference for compiler warning options:
// https://gcc.gnu.org/onlinedocs/gcc/Warning-Options.html
// GNU C Library manual - reference for converting text to double:
// https://www.gnu.org/software/libc/manual/html_node/Parsing-of-Floats.html

#include <stdio.h>

typedef enum {
    TOKEN_NUMBER,
    TOKEN_PLUS,
    TOKEN_MINUS,
    TOKEN_MULTIPLY,
    TOKEN_DIVIDE,
    TOKEN_POWER,
    TOKEN_LEFT_PAREN,
    TOKEN_RIGHT_PAREN,
    TOKEN_END_LINE,
    TOKEN_END_FILE,
    TOKEN_INVALID
} TokenType;

typedef struct {
    int is_float;
    long long integer;
    double decimal;
} Number;

typedef struct {
    TokenType type;
    Number value;
    // Save where the token starts so we can report the error position later.
    size_t position;
} Token;

int main(void)
{
    return 0;
}