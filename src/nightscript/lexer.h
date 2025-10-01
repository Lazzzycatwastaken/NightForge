#pragma once
#include <string>
#include <vector>
#include <unordered_map>

namespace nightforge {
namespace nightscript {

enum class TokenType {
    // Literals
    STRING,
    NUMBER,
    IDENTIFIER,
    BOOLEAN,
    NIL,
    
    // Keywords  
    SCENE,
    CHARACTER,
    DIALOGUE,
    IF,
    ELSEIF,
    ELSE,
    WHILE,
    CHOICE,
    SET,
    CALL,
    RETURN,
    END,
    ON_ENTER,
    THEN,
    FUNCTION,
    
    // Operators
    ASSIGN,      // =
    ARROW,       // ->
    PLUS,        // +
    MINUS,       // -
    MULTIPLY,    // *
    DIVIDE,      // /
    EQUAL,       // ==
    NOT_EQUAL,   // !=
    LESS,        // <
    GREATER,     // >
    LESS_EQUAL,  // <=
    GREATER_EQUAL, // >=
    AND,         // and
    OR,          // or
    NOT,         // not / !
    
    // Delimiters
    LEFT_BRACE,   // {
    RIGHT_BRACE,  // }
    LEFT_PAREN,   // (
    RIGHT_PAREN,  // )
    COMMA,        // ,
    DOT,          // .
    
    // Special
    NEWLINE,
    EOF_TOKEN,
    UNKNOWN
};

struct Token {
    TokenType type;
    std::string lexeme;
    int line;
    int column;
    
    Token(TokenType t, const std::string& lex, int ln, int col)
        : type(t), lexeme(lex), line(ln), column(col) {}
};

class Lexer {
public:
    explicit Lexer(const std::string& source);
    
    std::vector<Token> tokenize();
    Token next_token();
    
private:
    std::string source_;
    size_t current_;
    int line_;
    int column_;
    
    static std::unordered_map<std::string, TokenType> keywords_;
    
    char advance();
    char peek();
    char peek_next();
    bool is_at_end();
    bool is_alpha(char c);
    bool is_digit(char c);
    bool is_alphanumeric(char c);
    
    Token make_token(TokenType type);
    Token make_token(TokenType type, const std::string& lexeme);
    Token string_token();
    Token number_token();
    Token identifier_token();
    void skip_comment();
};

} // namespace nightscript
} // namespace nightforge