// Abhishek Bisht 241ADB121
// Compile with: gcc -O2 -Wall -Wextra -std=c17 -o calc calc.c -lm
// Resources used:
// OpenAI ChatGPT - help with design, code, explanations and testing.
// GCC manual - reference for compiler warning options:
// https://gcc.gnu.org/onlinedocs/gcc/Warning-Options.html
// GNU C Library manual - reference for converting text to double:
// https://www.gnu.org/software/libc/manual/html_node/Parsing-of-Floats.html

#include <stdio.h>
#include <stdlib.h>
#include <math.h>

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
        size_t start = lexer->index;

        while (lexer->index < lexer->length &&
               lexer->text[lexer->index] >= '0' &&
               lexer->text[lexer->index] <= '9') {
            advance(lexer);
        }

        int is_float = 0;

        if (lexer->index < lexer->length &&
            lexer->text[lexer->index] == '.') {
            size_t dot_position = lexer->index + 1;
            advance(lexer);

            // Digits are required after the dot, so 5. is invalid.
            if (lexer->index == lexer->length ||
                lexer->text[lexer->index] < '0' ||
                lexer->text[lexer->index] > '9') {
                lexer->token.type = TOKEN_INVALID;
                lexer->token.position = dot_position;
                return;
            }

            is_float = 1;

            while (lexer->index < lexer->length &&
                   lexer->text[lexer->index] >= '0' &&
                   lexer->text[lexer->index] <= '9') {
                advance(lexer);
            }
        }

        lexer->token.type = TOKEN_NUMBER;
        lexer->token.value.is_float = is_float;

        if (is_float) {
            char *end;
            double value = strtod(lexer->text + start, &end);

            // strtod accepts exponents, but our number rules do not.
            if (end != lexer->text + lexer->index) {
                lexer->token.type = TOKEN_INVALID;
                lexer->token.position = lexer->index + 1;
                return;
            }

            lexer->token.value.decimal = value;
        } else {
            // Convert only after checking for a dot: a float may exceed
            // the integer range even when its double value is valid.
            lexer->token.value.integer =
                strtoll(lexer->text + start, NULL, 10);
        }

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

static double as_double(Number number)
{
    return number.is_float ? number.decimal : (double)number.integer;
}

static Number calculate(Parser *parser, Number left, Number right,
                        TokenType operation, size_t operator_position,
                        size_t right_position)
{
    Number result = {0};

    if (operation == TOKEN_DIVIDE &&
        (right.is_float ? right.decimal == 0.0 : right.integer == 0)) {
        // Division by zero points to the start of the right operand.
        set_error(parser, right_position);
        return result;
    }

    // One float operand makes the whole operation use double arithmetic.
    if (left.is_float || right.is_float) {
        double a = as_double(left);
        double b = as_double(right);
        result.is_float = 1;

        switch (operation) {
            case TOKEN_PLUS:
                result.decimal = a + b;
                break;
            case TOKEN_MINUS:
                result.decimal = a - b;
                break;
            case TOKEN_MULTIPLY:
                result.decimal = a * b;
                break;
            case TOKEN_DIVIDE:
                result.decimal = a / b;
                break;
            default:
                break;
        }

        if (!isfinite(result.decimal)) {
            // An invalid float result is reported at its operator.
            set_error(parser, operator_position);
        }
    } else {
        // Keep integers as long long so division keeps its integer rules.
        switch (operation) {
            case TOKEN_PLUS:
                result.integer = left.integer + right.integer;
                break;
            case TOKEN_MINUS:
                result.integer = left.integer - right.integer;
                break;
            case TOKEN_MULTIPLY:
                result.integer = left.integer * right.integer;
                break;
            case TOKEN_DIVIDE:
                result.integer = left.integer / right.integer;
                break;
            default:
                break;
        }
    }

    return result;
}

static Number parse_power(Parser *parser)
{
    Number result = parse_primary(parser);

    if (parser->error_position) {
        return result;
    }

    if (parser->lexer.token.type == TOKEN_POWER) {
        size_t operator_position = parser->lexer.token.position;
        next_token(&parser->lexer);

        size_t exponent_position = parser->lexer.token.position;

        // Calling this function again makes 2**3**2 mean 2**(3**2).
        Number exponent = parse_power(parser);

        if (parser->error_position) {
            return result;
        }

        // A float on either side means negative exponents are allowed.
        if (result.is_float || exponent.is_float) {
            double answer = pow(as_double(result), as_double(exponent));

            if (!isfinite(answer)) {
                // Infinite or NaN results point to the power operator.
                set_error(parser, operator_position);
                return result;
            }

            result.is_float = 1;
            result.decimal = answer;
            return result;
        }

        if (exponent.integer < 0) {
            // Only integer powers reject a negative exponent.
            set_error(parser, exponent_position);
            return result;
        }

        long long base = result.integer;
        long long power = exponent.integer;
        long long answer = 1;

        // Squaring avoids multiplying once for every exponent step.
        while (power > 0) {
            if (power % 2 != 0) {
                answer *= base;
            }

            power /= 2;

            // Avoid an unnecessary square after the last step.
            if (power > 0) {
                base *= base;
            }
        }

        // Starting at 1 also handles the required 0**0 case.
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
        size_t operator_position = parser->lexer.token.position;
        next_token(&parser->lexer);

        size_t right_position = parser->lexer.token.position;
        Number right = parse_power(parser);

        if (parser->error_position) {
            return result;
        }

        result = calculate(parser, result, right, operation,
                           operator_position, right_position);
    }

    return result;
}

static Number parse_expression(Parser *parser)
{
    // Finish multiplication and division before addition and subtraction.
    Number result = parse_term(parser);

    // Work left to right because 7 - 2 - 1 means (7 - 2) - 1.
    while (!parser->error_position &&
           (parser->lexer.token.type == TOKEN_PLUS ||
            parser->lexer.token.type == TOKEN_MINUS)) {
        TokenType operation = parser->lexer.token.type;
        size_t operator_position = parser->lexer.token.position;
        next_token(&parser->lexer);

        size_t right_position = parser->lexer.token.position;
        Number right = parse_term(parser);

        if (parser->error_position) {
            return result;
        }

        result = calculate(parser, result, right, operation,
                           operator_position, right_position);
    }

    return result;
}

static char *read_file(const char *path, size_t *length)
{
    FILE *file = fopen(path, "rb");

    if (file == NULL) {
        perror(path);
        return NULL;
    }

    size_t capacity = 8192;
    char *text = malloc(capacity);

    if (text == NULL) {
        fprintf(stderr, "Not enough memory to read input\n");
        fclose(file);
        return NULL;
    }

    *length = 0;

    for (;;) {
        if (*length == capacity - 1) {
            if (capacity > (size_t)-1 / 2) {
                fprintf(stderr, "Input is too large\n");
                free(text);
                fclose(file);
                return NULL;
            }

            // Double the space so large files do not need constant copying.
            size_t new_capacity = capacity * 2;
            char *larger = realloc(text, new_capacity);

            if (larger == NULL) {
                fprintf(stderr, "Not enough memory to read input\n");
                free(text);
                fclose(file);
                return NULL;
            }

            text = larger;
            capacity = new_capacity;
        }

        size_t count = fread(text + *length, 1,
                             capacity - *length - 1, file);
        *length += count;

        if (count == 0) {
            if (ferror(file)) {
                fprintf(stderr, "Could not read input: %s\n", path);
                free(text);
                fclose(file);
                return NULL;
            }

            break;
        }
    }

    // The conversion functions need a zero byte after the input.
    text[*length] = '\0';

    if (fclose(file) != 0) {
        perror(path);
        free(text);
        return NULL;
    }

    return text;
}

int main(int argc, char *argv[])
{
    if (argc != 2) {
        fprintf(stderr, "Usage: %s input.txt\n", argv[0]);
        return 1;
    }

    size_t length;
    char *input = read_file(argv[1], &length);

    if (input == NULL) {
        return 1;
    }

    Parser parser = {
        .lexer = {
            .text = input,
            .length = length
        }
    };

    next_token(&parser.lexer);
    Number result = parse_expression(&parser);

    // A final newline is allowed after the expression.
    while (!parser.error_position &&
           parser.lexer.token.type == TOKEN_END_LINE) {
        next_token(&parser.lexer);
    }

    if (!parser.error_position &&
        parser.lexer.token.type != TOKEN_END_FILE) {
        set_error(&parser, parser.lexer.token.position);
    }

    if (parser.error_position) {
        printf("ERROR:%zu\n", parser.error_position);
    } else if (result.is_float) {
        printf("%.15g\n", result.decimal);
    } else {
        printf("%lld\n", result.integer);
    }

    free(input);
    return 0;
}