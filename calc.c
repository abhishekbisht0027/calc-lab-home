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

typedef struct {
    const char *text;
    size_t length;
    size_t index;
    int line_has_token;
    Token token;
} Lexer;

static void advance(Lexer *lexer)
{
    if (lexer->index < lexer->length) {
        lexer->index++;
    }
}

static void next_token(Lexer *lexer)
{
    while (lexer->index < lexer->length &&
           (lexer->text[lexer->index] == ' ' ||
            lexer->text[lexer->index] == '\t')) {
        advance(lexer);
    }

        // Skip comment text, but still count its characters.
    if (lexer->index < lexer->length &&
        lexer->text[lexer->index] == '#' &&
        !lexer->line_has_token) {
        while (lexer->index < lexer->length &&
               lexer->text[lexer->index] != '\n') {
            advance(lexer);
        }
    }

    lexer->token = (Token){0};

    // The index starts at 0, but file positions start at 1.
    lexer->token.position = lexer->index + 1;

    if (lexer->index == lexer->length) {
        lexer->token.type = TOKEN_END_FILE;
        return;
    }

    char c = lexer->text[lexer->index];

    // Reset at each newline so the next line can start with a comment.
    if (c == '\n') {
        lexer->line_has_token = 0;
    } else {
        lexer->line_has_token = 1;
    }

    if (c >= '0' && c <= '9') {
        long long value = 0;

        while (lexer->index < lexer->length) {
            c = lexer->text[lexer->index];

            if (c < '0' || c > '9') {
                break;
            }

            value = value * 10 + (c - '0');
            advance(lexer);
        }

        lexer->token.type = TOKEN_NUMBER;
        lexer->token.value.integer = value;
        return;
    }

    advance(lexer);

    switch (c) {
        case '+':
            lexer->token.type = TOKEN_PLUS;
            break;
        case '-':
            lexer->token.type = TOKEN_MINUS;
            break;
        case '*':
            // Only two adjacent stars form the power operator.
            if (lexer->index < lexer->length &&
                lexer->text[lexer->index] == '*') {
                advance(lexer);
                lexer->token.type = TOKEN_POWER;
            } else {
                lexer->token.type = TOKEN_MULTIPLY;
            }
            break;
        case '/':
            lexer->token.type = TOKEN_DIVIDE;
            break;
        case '(':
            lexer->token.type = TOKEN_LEFT_PAREN;
            break;
        case ')':
            lexer->token.type = TOKEN_RIGHT_PAREN;
            break;
        case '\n':
            lexer->token.type = TOKEN_END_LINE;
            break;
        default:
            lexer->token.type = TOKEN_INVALID;
            break;
    }
}

typedef struct {
    Lexer lexer;
    size_t error_position;
} Parser;

static void set_error(Parser *parser, size_t position)
{
    // Keep the first error so later checks do not replace it.
    if (parser->error_position == 0) {
        parser->error_position = position;
    }
}

static Number parse_expression(Parser *parser);

static Number parse_primary(Parser *parser)
{
    Number result = {0};

    if (parser->lexer.token.type == TOKEN_NUMBER) {
        result = parser->lexer.token.value;
        next_token(&parser->lexer);
        return result;
    }

    if (parser->lexer.token.type == TOKEN_LEFT_PAREN) {
        next_token(&parser->lexer);

        // Read the whole expression inside the parentheses first.
        result = parse_expression(parser);

        if (parser->error_position) {
            return result;
        }

        if (parser->lexer.token.type != TOKEN_RIGHT_PAREN) {
            // Report the token where the closing parenthesis was needed.
            set_error(parser, parser->lexer.token.position);
            return result;
        }

        next_token(&parser->lexer);
        return result;
    }

    // A number or opening parenthesis was needed at this position.
    set_error(parser, parser->lexer.token.position);
    return result;
}

static Number parse_power(Parser *parser)
{
    Number result = parse_primary(parser);
    if (parser->error_position) {
        return result;
    }

    if (parser->lexer.token.type == TOKEN_POWER) {
        next_token(&parser->lexer);
        size_t exponent_position = parser->lexer.token.position;

        // Calling this function again makes 2**3**2 mean 2**(3**2).
        Number exponent = parse_power(parser);

        if (parser->error_position) {
            return result;
        }

        if (exponent.integer < 0) {
            // A negative integer exponent is reported where it starts.
            set_error(parser, exponent_position);
            return result;
        }

        long long base = result.integer;
        long long power = exponent.integer;
        long long answer = 1;

        // Squaring the base avoids multiplying once for every exponent step.
        while (power > 0) {
            if (power % 2 != 0) {
                answer *= base;
            }

            power /= 2;

            // No final square is needed once the exponent is used up.
            if (power > 0) {
                base *= base;
            }
        }

        // Starting at 1 also gives the required answer for 0**0.
        result.integer = answer;
    }

    return result;
}

static Number parse_term(Parser *parser)
{
    // Evaluate powers before multiplication and division.
    Number result = parse_power(parser);

    // Work left to right so 20 / 2 / 2 gives 5.
    while (!parser->error_position &&
           (parser->lexer.token.type == TOKEN_MULTIPLY ||
            parser->lexer.token.type == TOKEN_DIVIDE)) {
        TokenType operation = parser->lexer.token.type;
        next_token(&parser->lexer);

        size_t right_position = parser->lexer.token.position;
        Number right = parse_power(parser);

        if (parser->error_position) {
            return result;
        }

        if (operation == TOKEN_MULTIPLY) {
            result.integer *= right.integer;
        } else {
            if (right.integer == 0) {
                // The assignment puts this error at the right operand.
                set_error(parser, right_position);
                return result;
            }

            // Integer division in C already truncates toward zero.
            result.integer /= right.integer;
        }
    }

    return result;
}

static Number parse_expression(Parser *parser)
{
    // Finish each multiplication or division before adding or subtracting.
    Number result = parse_term(parser);

    // Work left to right because 7 - 2 - 1 means (7 - 2) - 1.
    while (!parser->error_position &&
           (parser->lexer.token.type == TOKEN_PLUS ||
            parser->lexer.token.type == TOKEN_MINUS)) {
        TokenType operation = parser->lexer.token.type;
        next_token(&parser->lexer);

        Number right = parse_term(parser);

        if (parser->error_position) {
            return result;
        }

        if (operation == TOKEN_PLUS) {
            result.integer += right.integer;
        } else {
            result.integer -= right.integer;
        }
    }

    return result;
}

int main(void)
{
    const char input[] = "5 ** 0 + 0 ** 0";
    Parser parser = {
        .lexer = {
            .text = input,
            .length = sizeof(input) - 1
        }
    };

    next_token(&parser.lexer);
    Number result = parse_expression(&parser);

    if (!parser.error_position &&
        parser.lexer.token.type != TOKEN_END_FILE) {
        // Any leftover token means the expression is not complete.
        set_error(&parser, parser.lexer.token.position);
    }

    if (parser.error_position) {
        printf("ERROR:%zu\n", parser.error_position);
    } else {
        printf("%lld\n", result.integer);
    }

    return 0;
}