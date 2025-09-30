#include "compiler.h"
#include <iostream>
#include <cstdlib>

namespace nightforge {
namespace nightscript {

Compiler::Compiler() : current_(0), chunk_(nullptr), strings_(nullptr), had_error_(false), panic_mode_(false) {
}

bool Compiler::compile(const std::string& source, Chunk& chunk, StringTable& strings) {
    Lexer lexer(source);
    tokens_ = lexer.tokenize();
    
    current_ = 0;
    chunk_ = &chunk;
    strings_ = &strings;
    had_error_ = false;
    panic_mode_ = false;
    
    // Simple statement-based compilation for now
    while (!check(TokenType::EOF_TOKEN)) {
        statement();
        if (panic_mode_) {
            // Skip to next statement boundary
            while (!check(TokenType::EOF_TOKEN) && !check(TokenType::NEWLINE)) {
                advance();
            }
            if (check(TokenType::NEWLINE)) advance();
            panic_mode_ = false;
        }
    }
    
    emit_return();
    return !had_error_;
}

Token Compiler::current_token() {
    if (current_ >= tokens_.size()) {
        return Token(TokenType::EOF_TOKEN, "", 0, 0);
    }
    return tokens_[current_];
}

Token Compiler::previous_token() {
    if (current_ == 0) {
        return Token(TokenType::EOF_TOKEN, "", 0, 0);
    }
    return tokens_[current_ - 1];
}

bool Compiler::advance() {
    if (current_ < tokens_.size()) {
        current_++;
        return true;
    }
    return false;
}

bool Compiler::check(TokenType type) {
    return current_token().type == type;
}

bool Compiler::match(TokenType type) {
    if (check(type)) {
        advance();
        return true;
    }
    return false;
}

void Compiler::consume(TokenType type, const char* message) {
    if (current_token().type == type) {
        advance();
        return;
    }
    error_at_current(message);
}

void Compiler::expression() {
    // For now just handle simple literals and grouping
    if (match(TokenType::NUMBER)) {
        number();
    } else if (match(TokenType::STRING)) {
        string();
    } else if (match(TokenType::BOOLEAN) || match(TokenType::NIL)) {
        literal();
    } else if (match(TokenType::LEFT_PAREN)) {
        grouping();
    } else if (match(TokenType::NOT)) {
        unary();
    } else {
        error("Expected expression");
    }
}

void Compiler::number() {
    Token token = previous_token();
    std::string num_str = token.lexeme;
    
    // Check if its an integer or float
    if (num_str.find('.') != std::string::npos) {
        double value = std::stod(num_str);
        emit_constant(Value::floating(value));
    } else {
        int64_t value = std::stoll(num_str);
        emit_constant(Value::integer(value));
    }
}

void Compiler::string() {
    Token token = previous_token();
    uint32_t string_id = strings_->intern(token.lexeme);
    emit_constant(Value::string_id(string_id));
}

void Compiler::literal() {
    Token token = previous_token();
    switch (token.type) {
        case TokenType::BOOLEAN:
            if (token.lexeme == "true") {
                emit_byte(static_cast<uint8_t>(OpCode::OP_TRUE));
            } else {
                emit_byte(static_cast<uint8_t>(OpCode::OP_FALSE));
            }
            break;
        case TokenType::NIL:
            emit_byte(static_cast<uint8_t>(OpCode::OP_NIL));
            break;
        default:
            error("Unknown literal");
            break;
    }
}

void Compiler::grouping() {
    expression();
    consume(TokenType::RIGHT_PAREN, "Expected ')' after expression");
}

void Compiler::unary() {
    // For now just handle 'not'
    expression();
    emit_byte(static_cast<uint8_t>(OpCode::OP_NOT));
}

void Compiler::binary() {
    // TODO: implement proper precedence parsing
}

void Compiler::statement() {
    // Skip newlines
    if (match(TokenType::NEWLINE)) {
        return;
    }
    
    // For now just print statements and expressions
    if (check(TokenType::IDENTIFIER) && current_token().lexeme == "print") {
        advance();
        print_statement();
    } else {
        expression_statement();
    }
}

void Compiler::expression_statement() {
    expression();
    emit_byte(static_cast<uint8_t>(OpCode::OP_POP)); // discard result
}

void Compiler::print_statement() {
    expression();
    emit_byte(static_cast<uint8_t>(OpCode::OP_PRINT));
}

void Compiler::emit_byte(uint8_t byte) {
    int line = current_token().line;
    chunk_->write_byte(byte, line);
}

void Compiler::emit_bytes(uint8_t byte1, uint8_t byte2) {
    emit_byte(byte1);
    emit_byte(byte2);
}

void Compiler::emit_constant(const Value& value) {
    int line = current_token().line;
    chunk_->write_constant(value, line);
}

void Compiler::emit_return() {
    emit_byte(static_cast<uint8_t>(OpCode::OP_RETURN));
}

void Compiler::error(const char* message) {
    error_at_current(message);
}

void Compiler::error_at_current(const char* message) {
    error_at(current_token(), message);
}

void Compiler::error_at(const Token& token, const char* message) {
    if (panic_mode_) return; // suppress cascading errors
    
    panic_mode_ = true;
    had_error_ = true;
    
    std::cerr << "[line " << token.line << "] Error";
    
    if (token.type == TokenType::EOF_TOKEN) {
        std::cerr << " at end";
    } else if (token.type != TokenType::UNKNOWN) {
        std::cerr << " at '" << token.lexeme << "'";
    }
    
    std::cerr << ": " << message << std::endl;
}

} // namespace nightscript
} // namespace nightforge