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

    // this is only for debugging
    // for (const auto& t : tokens_) {
    //     std::cerr << "[tok] line=" << t.line << " type=" << static_cast<int>(t.type) << " lex='" << t.lexeme << "'\n";
    // }
    
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
    // Start with lowest precedence
    expression_precedence(0);
}

void Compiler::expression_precedence(int min_precedence) {
    // Parse primary expression (numbers, strings, parentheses, identifiers, etc)
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
    } else if (match(TokenType::IDENTIFIER)) {
        identifier();
    } else {
        error("Expected expression");
        return;
    }
    
    // Handle binary operators with precedence
    while (true) {
        Token current = current_token();
        if (!is_binary_operator(current.type)) {
            break;
        }
        
        int precedence = get_precedence(current.type);
        if (precedence < min_precedence) {
            break;
        }
        
        // Consume the operator
        advance();
        TokenType operator_type = previous_token().type;
        
        // Parse the right side expression
        expression_precedence(precedence + 1);
        
        //boom 
        OpCode op = token_to_opcode(operator_type);
        emit_byte(static_cast<uint8_t>(op));
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

void Compiler::identifier() {
    Token name = previous_token();
    uint32_t name_id = strings_->intern(name.lexeme);

    // If this identifier has a '(' treat as function call
    if (check(TokenType::LEFT_PAREN)) {
        // rewind so call_expression can see previous_token as name
        // previous_token is already the identifier
        call_expression();
        return;
    }

    // Add variable name to constants table and emit OP_GET_GLOBAL with index
    size_t name_constant = chunk_->add_constant(Value::string_id(name_id));
    emit_bytes(static_cast<uint8_t>(OpCode::OP_GET_GLOBAL), static_cast<uint8_t>(name_constant));
}

void Compiler::binary() {
    // TODO: implement proper precedence parsing
}

void Compiler::statement() {
    // Skip newlines
    if (match(TokenType::NEWLINE)) {
        return;
    }
    
    // Check for print statement
    if (check(TokenType::IDENTIFIER) && current_token().lexeme == "print") {
        advance();
        print_statement();
    // Check for if statement
    } else if (check(TokenType::IF)) {
        if_statement();
    } else if (check(TokenType::FUNCTION)) {
        function_declaration();
    // Check for assignment: identifier = expression
    } else if (check(TokenType::IDENTIFIER)) {
        // Look ahead to see if it's an assignment or a bare call
        size_t saved_current = current_;
        Token next = (current_ + 1 < tokens_.size()) ? tokens_[current_ + 1] : Token(TokenType::EOF_TOKEN, "", 0, 0);

        if (next.type == TokenType::ASSIGN) {
            // assignment
            advance(); // consume identifier
            current_ = saved_current;
            assignment_statement();
        } else if (next.type == TokenType::LEFT_PAREN) {
            // function call with parentheses
            current_ = saved_current;
            expression_statement();
        } else if (next.type == TokenType::NEWLINE || next.type == TokenType::EOF_TOKEN) {
            // bare identifier as zero-arg call
            Token name = current_token();
            advance(); // consume identifier
            uint32_t name_id = strings_->intern(name.lexeme);
            size_t name_const = chunk_->add_constant(Value::string_id(name_id));
            emit_byte(static_cast<uint8_t>(OpCode::OP_CALL_HOST));
            emit_byte(static_cast<uint8_t>(name_const));
            emit_byte(static_cast<uint8_t>(0)); // no arguments
            emit_byte(static_cast<uint8_t>(OpCode::OP_POP)); // lock OFF
        } else {
            // Rewind and parse as expression
            current_ = saved_current;
            expression_statement();
        }
    } else {
        expression_statement();
    }
}

void Compiler::expression_statement() {
    expression();
    emit_byte(static_cast<uint8_t>(OpCode::OP_POP)); // discard result
}

void Compiler::assignment_statement() {
    // We already know this is an assignment (identifier = expression)
    // The identifier should be the current token
    Token name = current_token();
    advance(); // consume identifier
    uint32_t name_id = strings_->intern(name.lexeme);
    
    if (!match(TokenType::ASSIGN)) {
        error("Expected '=' after variable name");
        return;
    }
    
    expression(); // Parse the value (leaves value on stack)
    
    // Add variable name to constants table and emit OP_SET_GLOBAL with index
    size_t name_constant = chunk_->add_constant(Value::string_id(name_id));
    emit_bytes(static_cast<uint8_t>(OpCode::OP_SET_GLOBAL), static_cast<uint8_t>(name_constant));
}

size_t Compiler::emit_jump(uint8_t instruction) {
    emit_byte(instruction);
    // placeholder for jump offset
    size_t pos = chunk_->code().size();
    emit_byte(0);
    return pos;
}

void Compiler::patch_jump(size_t jump_position) {
    size_t offset = chunk_->code().size() - (jump_position + 1);
    if (offset > 255) {
        error("Jump too large");
        offset = 255;
    }
    chunk_->patch_byte(jump_position, static_cast<uint8_t>(offset));
}

void Compiler::if_statement() {
    consume(TokenType::IF, "Expected 'if'");

    expression();

    consume(TokenType::THEN, "Expected 'then' after a condition");

    size_t jump_to_else = emit_jump(static_cast<uint8_t>(OpCode::OP_JUMP_IF_FALSE));

    // compile then branch statements until ELSE or END
    while (!check(TokenType::ELSE) && !check(TokenType::END) && !check(TokenType::EOF_TOKEN)) {
        statement();
    }

    size_t jump_over_else = emit_jump(static_cast<uint8_t>(OpCode::OP_JUMP));

    patch_jump(jump_to_else);

    if (match(TokenType::ELSE)) {
        // compile else branch
        while (!check(TokenType::END) && !check(TokenType::EOF_TOKEN)) {
            statement();
        }
    }

    consume(TokenType::END, "Expected 'end' to close an if statement");

    patch_jump(jump_over_else);
}

void Compiler::function_declaration() {
    consume(TokenType::FUNCTION, "Expected 'function'");

    // function name should be the identifier
    if (!check(TokenType::IDENTIFIER)) {
        error("Expected function name");
        return;
    }
    Token name = current_token();
    advance();
    std::string func_name = name.lexeme;

    // Optional parameter list: (parameter)
    std::string param_name = "";
    if (match(TokenType::LEFT_PAREN)) {
        if (check(TokenType::IDENTIFIER)) {
            Token p = current_token(); advance();
            param_name = p.lexeme;
        }
        consume(TokenType::RIGHT_PAREN, "No ')' after a parameter list");
    }

    // Compile function body into a new Chunk
    Chunk func_chunk;
    StringTable local_strings = *strings_;

    // Save current chunk and switch to function chunk
    Chunk* saved_chunk = chunk_;
    chunk_ = &func_chunk;

    // compile body until END
    while (!check(TokenType::END) && !check(TokenType::EOF_TOKEN)) {
        statement();
    }

    // consume the END that closes the function
    consume(TokenType::END, "Expected 'end' to close function");

    // ensure a return at end of function body
    emit_byte(static_cast<uint8_t>(OpCode::OP_RETURN));

    // restore
    chunk_ = saved_chunk;

    // register function in parent chunk
    chunk_->add_function(func_chunk, param_name, func_name);
}

void Compiler::call_expression() {
    // We're at identifier that represents a call
    Token name = previous_token();
    std::string func_name = name.lexeme;

    // Parse optional argument list
    int arg_count = 0;
    if (match(TokenType::LEFT_PAREN)) {
        if (!check(TokenType::RIGHT_PAREN)) {
            expression();
            arg_count++;
            while (match(TokenType::COMMA)) {
                expression();
                arg_count++;
            }
        }
        consume(TokenType::RIGHT_PAREN, "Expected ')' after arguments");
    }

    uint32_t name_id = strings_->intern(func_name);
    size_t name_const = chunk_->add_constant(Value::string_id(name_id));
    emit_byte(static_cast<uint8_t>(OpCode::OP_CALL_HOST));
    emit_byte(static_cast<uint8_t>(name_const));
    emit_byte(static_cast<uint8_t>(arg_count));
}

void Compiler::print_statement() {
    int expression_count = 0;
    
    do {
        expression();
        expression_count++;
        
        // Check if there's another expression coming
        bool has_more = (check(TokenType::STRING) || check(TokenType::NUMBER) || 
                        check(TokenType::BOOLEAN) || check(TokenType::NIL) || 
                        check(TokenType::LEFT_PAREN) || check(TokenType::IDENTIFIER));
        
        if (has_more) {
            emit_byte(static_cast<uint8_t>(OpCode::OP_PRINT_SPACE));
        } else {
            emit_byte(static_cast<uint8_t>(OpCode::OP_PRINT)); // Last one gets newline
        }
    } while (check(TokenType::STRING) || check(TokenType::NUMBER) || 
             check(TokenType::BOOLEAN) || check(TokenType::NIL) || 
             check(TokenType::LEFT_PAREN) || check(TokenType::IDENTIFIER));
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

int Compiler::get_precedence(TokenType type) {
    // Higher number = higher precedence (so correct order)
    switch (type) {
        case TokenType::MULTIPLY:
        case TokenType::DIVIDE:
            return 3;
        case TokenType::PLUS:
        case TokenType::MINUS:
            return 2;
        case TokenType::EQUAL:
        case TokenType::NOT_EQUAL:
        case TokenType::GREATER:
        case TokenType::GREATER_EQUAL:
        case TokenType::LESS:
        case TokenType::LESS_EQUAL:
            return 1;
        default:
            return 0;
    }
}

bool Compiler::is_binary_operator(TokenType type) {
    switch (type) {
        case TokenType::PLUS:
        case TokenType::MINUS:
        case TokenType::MULTIPLY:
        case TokenType::DIVIDE:
        case TokenType::EQUAL:
        case TokenType::NOT_EQUAL:
        case TokenType::GREATER:
        case TokenType::GREATER_EQUAL:
        case TokenType::LESS:
        case TokenType::LESS_EQUAL:
            return true;
        default:
            return false;
    }
}

OpCode Compiler::token_to_opcode(TokenType type) {
    switch (type) {
        case TokenType::PLUS: return OpCode::OP_ADD;
        case TokenType::MINUS: return OpCode::OP_SUBTRACT;
        case TokenType::MULTIPLY: return OpCode::OP_MULTIPLY;
        case TokenType::DIVIDE: return OpCode::OP_DIVIDE;
        case TokenType::EQUAL: return OpCode::OP_EQUAL;
        case TokenType::GREATER: return OpCode::OP_GREATER;
        case TokenType::LESS: return OpCode::OP_LESS;
        default:
            error("UNKNOWN binary operator");
            return OpCode::OP_ADD; // fallback
    }
}

} // namespace nightscript
} // namespace nightforge