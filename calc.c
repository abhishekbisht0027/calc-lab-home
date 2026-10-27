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
#include <string.h>
#include <errno.h>
#include <sys/stat.h>

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

static int evaluate_input(const char *input, size_t length, FILE *output)
{
    Parser parser = {
        .lexer = {
            .text = input,
            .length = length
        }
    };

    Number *results = NULL;
    size_t count = 0;
    size_t capacity = 0;

    next_token(&parser.lexer);

    while (parser.lexer.token.type != TOKEN_END_FILE) {
        // Blank lines and skipped comment lines have no result.
        if (parser.lexer.token.type == TOKEN_END_LINE) {
            next_token(&parser.lexer);
            continue;
        }

        Number result = parse_expression(&parser);

        if (parser.error_position) {
            break;
        }

        if (parser.lexer.token.type != TOKEN_END_LINE &&
            parser.lexer.token.type != TOKEN_END_FILE) {
            // An expression must finish before another line begins.
            set_error(&parser, parser.lexer.token.position);
            break;
        }

        if (count == capacity) {
            size_t max_count = (size_t)-1 / sizeof(*results);

            if (capacity > max_count / 2) {
                fprintf(stderr, "Too many results\n");
                free(results);
                return 1;
            }

            // Growing in larger steps avoids copying after every result.
            size_t new_capacity = capacity == 0 ? 64 : capacity * 2;

            if (new_capacity > max_count) {
                fprintf(stderr, "Too many results\n");
                free(results);
                return 1;
            }

            Number *larger = realloc(results,
                                     new_capacity * sizeof(*results));

            if (larger == NULL) {
                fprintf(stderr, "Not enough memory for results\n");
                free(results);
                return 1;
            }

            results = larger;
            capacity = new_capacity;
        }

        results[count++] = result;
    }

    // Wait until the whole file is checked so errors leave no partial results.
    if (parser.error_position) {
        fprintf(output, "ERROR:%zu\n", parser.error_position);
    } else {
        for (size_t i = 0; i < count; i++) {
            if (results[i].is_float) {
                fprintf(output, "%.15g\n", results[i].decimal);
            } else {
                fprintf(output, "%lld\n", results[i].integer);
            }

            if (ferror(output)) {
                break;
            }
        }
    }

    free(results);

    if (ferror(output)) {
        fprintf(stderr, "Could not write results\n");
        return 1;
    }

    return 0;
}

static int create_output_directory(const char *path)
{
    if (mkdir(path, 0777) == 0) {
        return 0;
    }

    if (errno == EEXIST) {
        struct stat info;

        if (stat(path, &info) == 0 && S_ISDIR(info.st_mode)) {
            return 0;
        }

        fprintf(stderr, "Output path is not a directory: %s\n", path);
        return 1;
    }

    perror(path);
    return 1;
}

static int process_file(const char *input_path, const char *output_directory)
{
    size_t length;
    char *input = read_file(input_path, &length);

    if (input == NULL) {
        return 1;
    }

    const char *base = strrchr(input_path, '/');
    base = base == NULL ? input_path : base + 1;

    size_t base_length = strlen(base);

    // Remove only the final .txt, keeping the rest of the filename.
    if (base_length >= 4 &&
        strcmp(base + base_length - 4, ".txt") == 0) {
        base_length -= 4;
    }

    char *stem = malloc(base_length + 1);

    if (stem == NULL) {
        fprintf(stderr, "Not enough memory for output filename\n");
        free(input);
        return 1;
    }

    memcpy(stem, base, base_length);
    stem[base_length] = '\0';

    // Measure first so the output path has no fixed length limit.
    int needed = snprintf(NULL, 0, "%s/%s_abhishek_bisht_241ADB121.txt",
                          output_directory, stem);

    if (needed < 0) {
        fprintf(stderr, "Could not create output filename\n");
        free(stem);
        free(input);
        return 1;
    }

    char *output_path = malloc((size_t)needed + 1);

    if (output_path == NULL) {
        fprintf(stderr, "Not enough memory for output path\n");
        free(stem);
        free(input);
        return 1;
    }

    snprintf(output_path, (size_t)needed + 1,
             "%s/%s_abhishek_bisht_241ADB121.txt",
             output_directory, stem);
    free(stem);

    FILE *output = fopen(output_path, "w");

    if (output == NULL) {
        perror(output_path);
        free(output_path);
        free(input);
        return 1;
    }

    int status = evaluate_input(input, length, output);
    free(input);

    // Buffered writes can fail when the file is closed too.
    if (fclose(output) != 0) {
        perror(output_path);
        status = 1;
    }

    free(output_path);
    return status;
}

static char *default_output_directory(const char *input_path,
                                      int directory_mode)
{
    char *copy = malloc(strlen(input_path) + 1);

    if (copy == NULL) {
        fprintf(stderr, "Not enough memory for default directory\n");
        return NULL;
    }

    strcpy(copy, input_path);
    size_t length = strlen(copy);

    // Ignore trailing slashes when taking a folder's name.
    while (length > 1 && copy[length - 1] == '/') {
        copy[--length] = '\0';
    }

    char *base = strrchr(copy, '/');
    base = base == NULL ? copy : base + 1;

    size_t base_length = strlen(base);

    // Strip .txt for a file, but keep a directory's full name.
    if (!directory_mode && base_length >= 4 &&
        strcmp(base + base_length - 4, ".txt") == 0) {
        base[base_length - 4] = '\0';
    }

    int needed = snprintf(NULL, 0, "%s_abhishek_241ADB121", base);

    if (needed < 0) {
        fprintf(stderr, "Could not create default directory name\n");
        free(copy);
        return NULL;
    }

    char *directory = malloc((size_t)needed + 1);

    if (directory == NULL) {
        fprintf(stderr, "Not enough memory for default directory\n");
        free(copy);
        return NULL;
    }

    snprintf(directory, (size_t)needed + 1,
             "%s_abhishek_241ADB121", base);

    free(copy);
    return directory;
}

int main(int argc, char *argv[])
{
    const char *input_path = NULL;
    const char *output_directory = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0 ||
            strcmp(argv[i], "--output-dir") == 0) {
            if (output_directory != NULL || i + 1 >= argc) {
                fprintf(stderr, "Provide one output directory\n");
                return 1;
            }

            output_directory = argv[++i];
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            return 1;
        } else {
            if (input_path != NULL) {
                fprintf(stderr, "Provide only one input file\n");
                return 1;
            }

            input_path = argv[i];
        }
    }

    if (input_path == NULL) {
        fprintf(stderr, "Usage: %s [-o OUTDIR] input.txt\n", argv[0]);
        return 1;
    }

    char *default_directory = NULL;

    if (output_directory == NULL) {
        default_directory = default_output_directory(input_path, 0);

        if (default_directory == NULL) {
            return 1;
        }

        output_directory = default_directory;
    }

    if (create_output_directory(output_directory) != 0) {
        free(default_directory);
        return 1;
    }

    int status = process_file(input_path, output_directory);
    free(default_directory);
    return status;
}