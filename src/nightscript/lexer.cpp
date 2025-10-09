#include "lexer.h"
#include <iostream>
#include <cctype>

namespace nightforge {
namespace nightscript {

// Keywords map (lua but simpler cause WE'RE braindead)
std::unordered_map<std::string, TokenType> Lexer::keywords_ = {
    {"scene", TokenType::SCENE}, //these are in-engine only
    {"character", TokenType::CHARACTER},
    {"dialogue", TokenType::DIALOGUE},
    {"table", TokenType::TABLE},
    {"for", TokenType::FOR},
    {"if", TokenType::IF},
    {"elseif", TokenType::ELSEIF},
    {"else", TokenType::ELSE},
    {"while", TokenType::WHILE},
    {"do", TokenType::DO},
    {"choice", TokenType::CHOICE}, // same
    {"set", TokenType::SET},
    {"call", TokenType::CALL}, // this too
    {"return", TokenType::RETURN},
    {"end", TokenType::END},
    {"on_enter", TokenType::ON_ENTER}, // this too
    {"then", TokenType::THEN},
    {"function", TokenType::FUNCTION},
    {"local", TokenType::LOCAL},
    {"and", TokenType::AND},
    {"or", TokenType::OR},
    {"not", TokenType::NOT},
    {"true", TokenType::BOOLEAN},
    {"false", TokenType::BOOLEAN},
    {"nil", TokenType::NIL},
    {"is", TokenType::EQUAL} //Sure why not lol
};

Lexer::Lexer(const std::string& source) 
    : source_(source), current_(0), line_(1), column_(1) {
}

std::vector<Token> Lexer::tokenize() {
    std::vector<Token> tokens;
    tokens.reserve(128);
    
    while (!is_at_end()) {
        Token token = next_token();
        if (token.type != TokenType::UNKNOWN) {
            tokens.push_back(token);
        }
    }
    
    tokens.emplace_back(TokenType::EOF_TOKEN, "", line_, column_);
    return tokens;
}

Token Lexer::next_token() {
    // Skip whitespace (except newlines cause they matter sometimes)
    while (!is_at_end() && (source_[current_] == ' ' || source_[current_] == '\t' || source_[current_] == '\r')) {
        advance();
    }
    
    if (is_at_end()) {
        return Token(TokenType::EOF_TOKEN, "", line_, column_);
    }
    
    char c = advance();
    
    // Comments
    if (c == '#') {
        skip_comment();
        return next_token(); // recursion
    }
    
    // Newlines
    if (c == '\n') {
        Token token = make_token(TokenType::NEWLINE, "\\n");
        line_++;
        column_ = 1;
        return token;
    }
    
    // String literals (double or single quotes)
    if (c == '"' || c == '\'') {
        return string_token(c);
    }

    // Numbers
    if (is_digit(c)) {
        return number_token(c);
    }

    // Identifiers and keywords
    if (is_alpha(c)) {
        return identifier_token(c);
    }
    
    // Two-character operators
    if (c == '=' && peek() == '=') {
        advance();
        return make_token(TokenType::EQUAL, "==");
    }
    if (c == '!' && peek() == '=') {
        advance();
        return make_token(TokenType::NOT_EQUAL, "!=");
    }
    if (c == '<' && peek() == '=') {
        advance();
        return make_token(TokenType::LESS_EQUAL, "<=");
    }
    if (c == '>' && peek() == '=') {
        advance();
        return make_token(TokenType::GREATER_EQUAL, ">=");
    }
    if (c == '-' && peek() == '>') {
        advance();
        return make_token(TokenType::ARROW, "->");
    }
    
    // Single-character tokens
    switch (c) {
        case '=': return make_token(TokenType::ASSIGN);
        case '+': return make_token(TokenType::PLUS);
        case '-': return make_token(TokenType::MINUS);
        case '*': return make_token(TokenType::MULTIPLY);
        case '/': return make_token(TokenType::DIVIDE);
    case '%': return make_token(TokenType::MODULO);
        case '<': return make_token(TokenType::LESS);
        case '>': return make_token(TokenType::GREATER);
        case '!': return make_token(TokenType::NOT);
        case '{': return make_token(TokenType::LEFT_BRACE);
        case '}': return make_token(TokenType::RIGHT_BRACE);
        case '(': return make_token(TokenType::LEFT_PAREN);
        case ')': return make_token(TokenType::RIGHT_PAREN);
        case ',': return make_token(TokenType::COMMA);
        case '.': return make_token(TokenType::DOT);
        case '[': return make_token(TokenType::LEFT_BRACKET);
        case ']': return make_token(TokenType::RIGHT_BRACKET);
        default:
            // Unknown character
            return Token(TokenType::UNKNOWN, std::string(1, c), line_, column_ - 1);
    }
}

char Lexer::advance() {
    if (is_at_end()) return '\0';
    column_++;
    return source_[current_++];
}

char Lexer::peek() {
    if (is_at_end()) return '\0';
    return source_[current_];
}

char Lexer::peek_next() {
    if (current_ + 1 >= source_.length()) return '\0';
    return source_[current_ + 1];
}

bool Lexer::is_at_end() {
    return current_ >= source_.length();
}

bool Lexer::is_alpha(char c) {
    return std::isalpha(c) || c == '_';
}

bool Lexer::is_digit(char c) {
    return std::isdigit(c);
}

bool Lexer::is_alphanumeric(char c) {
    return is_alpha(c) || is_digit(c);
}

Token Lexer::make_token(TokenType type) {
    return Token(type, std::string(1, source_[current_ - 1]), line_, column_ - 1);
}

Token Lexer::make_token(TokenType type, const std::string& lexeme) {
    return Token(type, lexeme, line_, column_ - lexeme.length());
}

Token Lexer::string_token(char quote_char) {
    std::string value;
    
    while (!is_at_end() && peek() != quote_char) {
        if (peek() == '\n') {
            line_++;
            column_ = 0; // will be incremented by advance()
        }
        if (peek() == '\\') {
            advance(); // consume backslash
            if (!is_at_end()) {
                char escaped = advance();
                switch (escaped) {
                    case 'n': value += '\n'; break;
                    case 't': value += '\t'; break;
                    case 'r': value += '\r'; break;
                    case '\\': value += '\\'; break;
                    case '"': value += '"'; break;
                    case '\'': value += '\''; break;
                    default: 
                        value += '\\';
                        value += escaped;
                        break;
                }
            }
        } else {
            value += advance();
        }
    }
    
    if (is_at_end()) {
        // Unterminated string (could error but lets be lenient)
        return Token(TokenType::STRING, value, line_, column_ - value.length() - 1);
    }
    
    // consume closing quote
    advance();
    
    return Token(TokenType::STRING, value, line_, column_ - value.length() - 2);
}

Token Lexer::number_token(char first) {
    int start_col = column_ - 1; // first already consumed
    std::string value;
    value.push_back(first);

    while (!is_at_end() && is_digit(peek())) {
        value += advance();
    }

    // Check for decimal point
    if (!is_at_end() && peek() == '.' && is_digit(peek_next())) {
        value += advance(); // consume dot
        while (!is_at_end() && is_digit(peek())) {
            value += advance();
        }
    }

    return Token(TokenType::NUMBER, std::move(value), line_, start_col);
}

Token Lexer::identifier_token(char first) {
    int start_col = column_ - 1;
    std::string value;
    value.push_back(first);

    while (!is_at_end() && is_alphanumeric(peek())) {
        value += advance();
    }

    // Check if its a keyword
    auto it = keywords_.find(value);
    TokenType type = (it != keywords_.end()) ? it->second : TokenType::IDENTIFIER;

    return Token(type, std::move(value), line_, start_col);
}

void Lexer::skip_comment() {
    // Skip until end of line
    while (!is_at_end() && peek() != '\n') {
        advance();
    }
}

} // namespace nightscript
} // namespace nightforge