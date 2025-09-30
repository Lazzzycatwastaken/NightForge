#pragma once
#include "lexer.h"
#include "value.h"

namespace nightforge {
namespace nightscript {

class Compiler {
public:
    Compiler();
    
    bool compile(const std::string& source, Chunk& chunk, StringTable& strings);
    
private:
    std::vector<Token> tokens_;
    size_t current_;
    Chunk* chunk_;
    StringTable* strings_;
    
    // Parser state
    Token current_token();
    Token previous_token();
    bool advance();
    bool check(TokenType type);
    bool match(TokenType type);
    void consume(TokenType type, const char* message);
    
    // Compilation
    void expression();
    void number();
    void string();
    void literal();
    void grouping();
    void unary();
    void binary();
    void statement();
    void expression_statement();
    void print_statement();
    
    // Bytecode emission
    void emit_byte(uint8_t byte);
    void emit_bytes(uint8_t byte1, uint8_t byte2);
    void emit_constant(const Value& value);
    void emit_return();
    
    // Error handling
    void error(const char* message);
    void error_at_current(const char* message);
    void error_at(const Token& token, const char* message);
    
    bool had_error_;
    bool panic_mode_;
};

} // namespace nightscript
} // namespace nightforge