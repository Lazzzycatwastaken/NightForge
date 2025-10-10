#include "compiler.h"
#include <iostream>
#include <cstdlib>
#include <fstream>
#include <algorithm>
#include <sys/stat.h>
#include <ctime>

namespace nightforge {
namespace nightscript {

Compiler::Compiler() : current_(0), chunk_(nullptr), strings_(nullptr), 
                      last_expression_type_(InferredType::UNKNOWN),
                      had_error_(false), panic_mode_(false) {
}

bool Compiler::compile(const std::string& source, Chunk& chunk, StringTable& strings) {
    Lexer lexer(source);
    tokens_ = lexer.tokenize();
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
    thread_jumps();
    return !had_error_;
}

void Compiler::lower_stack_to_registers() {
    const auto& src = chunk_->code();
    std::vector<uint8_t> out;
    size_t i = 0;
    // uint8_t next_reg = 0; // unused
    
    size_t local_local_opts = 0;
    size_t local_const_opts = 0;
    size_t const_local_opts = 0;
    
    while (i < src.size()) {
        // uint8_t op = src[i]; // unused
        if (i + 4 < src.size()) {
            if (src[i] == static_cast<uint8_t>(OpCode::OP_GET_LOCAL) && src[i+2] == static_cast<uint8_t>(OpCode::OP_GET_LOCAL)) {
                uint8_t idx_a = src[i+1];
                uint8_t idx_b = src[i+3];
                uint8_t add_op = src[i+4];
                bool handled = false;

                if (add_op == static_cast<uint8_t>(OpCode::OP_ADD_INT)) {
                    out.push_back(static_cast<uint8_t>(OpCode::OP_ADD_LOCAL)); out.push_back(idx_a); out.push_back(idx_b);
                    i += 5; handled = true; local_local_opts++;
                } else if (add_op == static_cast<uint8_t>(OpCode::OP_ADD_FLOAT)) {
                    out.push_back(static_cast<uint8_t>(OpCode::OP_ADD_FLOAT_LOCAL)); out.push_back(idx_a); out.push_back(idx_b);
                    i += 5; handled = true; local_local_opts++;
                } else if (add_op == static_cast<uint8_t>(OpCode::OP_ADD_STRING)) {
                    out.push_back(static_cast<uint8_t>(OpCode::OP_ADD_STRING_LOCAL)); out.push_back(idx_a); out.push_back(idx_b);
                    i += 5; handled = true; local_local_opts++;
                }

                if (handled) continue;
            }

            if (src[i] == static_cast<uint8_t>(OpCode::OP_GET_LOCAL) && src[i+2] == static_cast<uint8_t>(OpCode::OP_CONSTANT)) {
                uint8_t idx_a = src[i+1];
                uint8_t const_idx = src[i+3];
                uint8_t add_op = src[i+4];
                bool handled = false;
                if (add_op == static_cast<uint8_t>(OpCode::OP_ADD_INT)) {
                    out.push_back(static_cast<uint8_t>(OpCode::OP_ADD_LOCAL_CONST)); out.push_back(idx_a); out.push_back(const_idx);
                    i += 5; handled = true; local_const_opts++;
                } else if (add_op == static_cast<uint8_t>(OpCode::OP_ADD_FLOAT)) {
                    out.push_back(static_cast<uint8_t>(OpCode::OP_ADD_LOCAL_CONST_FLOAT)); out.push_back(idx_a); out.push_back(const_idx);
                    i += 5; handled = true; local_const_opts++;
                } else if (add_op == static_cast<uint8_t>(OpCode::OP_ADD_STRING)) {
                    out.push_back(static_cast<uint8_t>(OpCode::OP_ADD_LOCAL_CONST)); out.push_back(idx_a); out.push_back(const_idx);
                    i += 5; handled = true; local_const_opts++;
                }
                if (handled) continue;
            }

            if (src[i] == static_cast<uint8_t>(OpCode::OP_CONSTANT) && src[i+2] == static_cast<uint8_t>(OpCode::OP_GET_LOCAL)) {
                uint8_t const_idx = src[i+1];
                uint8_t idx_a = src[i+3];
                uint8_t add_op = src[i+4];
                bool handled = false;
                if (add_op == static_cast<uint8_t>(OpCode::OP_ADD_INT)) {
                    out.push_back(static_cast<uint8_t>(OpCode::OP_ADD_CONST_LOCAL)); out.push_back(const_idx); out.push_back(idx_a);
                    i += 5; handled = true; const_local_opts++;
                } else if (add_op == static_cast<uint8_t>(OpCode::OP_ADD_FLOAT)) {
                    out.push_back(static_cast<uint8_t>(OpCode::OP_ADD_CONST_LOCAL_FLOAT)); out.push_back(const_idx); out.push_back(idx_a);
                    i += 5; handled = true; const_local_opts++;
                } else if (add_op == static_cast<uint8_t>(OpCode::OP_ADD_STRING)) {
                    out.push_back(static_cast<uint8_t>(OpCode::OP_ADD_CONST_LOCAL)); out.push_back(const_idx); out.push_back(idx_a);
                    i += 5; handled = true; const_local_opts++;
                }
                if (handled) continue;
            }
        }

        out.push_back(src[i]);
        ++i;
    }

    auto& code_mut = const_cast<std::vector<uint8_t>&>(chunk_->code());
    code_mut = std::move(out);
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
    if (try_length_of_expression()) {
        last_expression_type_ = InferredType::INTEGER;
    } else {
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
    } else if (match(TokenType::MINUS)) {
        size_t zero_idx = chunk_->add_constant(Value::integer(0));
        emit_bytes(static_cast<uint8_t>(OpCode::OP_CONSTANT), static_cast<uint8_t>(zero_idx));
        expression_precedence(3);
        emit_byte(static_cast<uint8_t>(OpCode::OP_SUBTRACT));
        last_expression_type_ = InferredType::INTEGER; // best effort
    } else if (match(TokenType::IDENTIFIER)) {
        identifier();
        while (match(TokenType::LEFT_BRACKET)) {
            expression();
            consume(TokenType::RIGHT_BRACKET, "Expected ']' after index");
            emit_byte(static_cast<uint8_t>(OpCode::OP_INDEX_GET));
            last_expression_type_ = InferredType::UNKNOWN;
        }
    } else if (match(TokenType::LEFT_BRACE)) {
        // Could be Array: { a, b, c } or Dictionary: { key: value, key2: value2 }
        // Look ahead to determine which one
        if (check(TokenType::RIGHT_BRACE)) {
            advance();
            emit_byte(static_cast<uint8_t>(OpCode::OP_ARRAY_CREATE));
            emit_byte(static_cast<uint8_t>(0));
            last_expression_type_ = InferredType::UNKNOWN;
        } else {
            // size_t saved_current = current_; unused for now
            expression();
            
            if (match(TokenType::COLON)) {
                expression();

                emit_byte(static_cast<uint8_t>(OpCode::OP_TABLE_CREATE));
                
                advance();
                error("Dictionary literals not yet fully implemented - use table syntax");
                return;
            } else {
                int count = 1;
                while (match(TokenType::COMMA)) {
                    if (check(TokenType::RIGHT_BRACE)) break;
                    expression();
                    count++;
                }
                consume(TokenType::RIGHT_BRACE, "Expected '}' to close array literal");
                emit_byte(static_cast<uint8_t>(OpCode::OP_ARRAY_CREATE));
                emit_byte(static_cast<uint8_t>(count));
            }
            last_expression_type_ = InferredType::UNKNOWN;
        }
    } else {
        error("Expected expression");
        return;
    }
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
        
        // Store left operand type for optimization
        InferredType left_type = last_expression_type_;
        
        // Consume the operator
        advance();
        TokenType operator_type = previous_token().type;
        
        // Parse the right side expression
        expression_precedence(precedence + 1);
        
        InferredType right_type = last_expression_type_;
        emit_optimized_binary_op(operator_type, left_type, right_type);
        
        if (operator_type == TokenType::PLUS && 
            (left_type == InferredType::STRING || right_type == InferredType::STRING)) {
            last_expression_type_ = InferredType::STRING;
        } else if (left_type == InferredType::INTEGER && right_type == InferredType::INTEGER) {
            last_expression_type_ = InferredType::INTEGER;
        } else if ((left_type == InferredType::FLOAT || right_type == InferredType::FLOAT) &&
                   (left_type != InferredType::STRING && right_type != InferredType::STRING)) {
            last_expression_type_ = InferredType::FLOAT;
        } else {
            last_expression_type_ = InferredType::UNKNOWN;
        }
    }
}

void Compiler::number() {
    Token token = previous_token();
    std::string num_str = token.lexeme;
    
    // Check if its an integer or float
    if (num_str.find('.') != std::string::npos) {
        double value = std::stod(num_str);
        emit_constant(Value::floating(value));
        last_expression_type_ = InferredType::FLOAT;
    } else {
        int64_t value = std::stoll(num_str);
        emit_constant(Value::integer(value));
        last_expression_type_ = InferredType::INTEGER;
    }
}

void Compiler::string() {
    Token token = previous_token();
    uint32_t string_id = strings_->intern(token.lexeme);
    emit_constant(Value::string_id(string_id));
    last_expression_type_ = InferredType::STRING;
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
            last_expression_type_ = InferredType::BOOLEAN;
            break;
        case TokenType::NIL:
            emit_byte(static_cast<uint8_t>(OpCode::OP_NIL));
            last_expression_type_ = InferredType::NIL;
            break;
        default:
            error("Unknown literal");
            last_expression_type_ = InferredType::UNKNOWN;
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

    std::vector<std::string> combined_locals;
    combined_locals.reserve(current_local_params_.size() + current_local_locals_.size());
    for (const auto &p : current_local_params_) combined_locals.push_back(p);
    for (const auto &l : current_local_locals_) combined_locals.push_back(l);

    auto it = std::find(combined_locals.begin(), combined_locals.end(), name.lexeme);
    if (it != combined_locals.end()) {
        size_t idx = static_cast<size_t>(std::distance(combined_locals.begin(), it));
        emit_byte(static_cast<uint8_t>(OpCode::OP_GET_LOCAL));
        emit_byte(static_cast<uint8_t>(idx));
    } else {
        // Add variable name to constants table and emit OP_GET_GLOBAL with index
        size_t name_constant = chunk_->add_constant(Value::string_id(name_id));
        emit_bytes(static_cast<uint8_t>(OpCode::OP_GET_GLOBAL), static_cast<uint8_t>(name_constant));
    }

    last_expression_type_ = infer_variable_type(name.lexeme);
}

void Compiler::statement() {
    // Skip newlines
    if (match(TokenType::NEWLINE)) {
        return;
    }

    if (try_sugar_statement()) {
        return;
    }
    
    // Check for print statement
    if (check(TokenType::IDENTIFIER) && current_token().lexeme == "print") {
        advance();
        print_statement();
    // Check for if statement
    } else if (check(TokenType::IF)) {
        if_statement();
    // Check for while statement
    } else if (check(TokenType::WHILE)) {
        while_statement();
    } else if (check(TokenType::FOR)) {
        for_statement();
    } else if (check(TokenType::RETURN)) {
        return_statement();
    } else if (check(TokenType::FUNCTION)) {
        function_declaration();
    } else if (check(TokenType::LOCAL)) {
        advance();
        if (!check(TokenType::IDENTIFIER)) {
            error("Expected local variable name");
            return;
        }
        Token name = current_token(); advance();
        current_local_locals_.push_back(name.lexeme);
        while (match(TokenType::COMMA)) {
            if (check(TokenType::IDENTIFIER)) {
                Token n = current_token(); advance();
                current_local_locals_.push_back(n.lexeme);
            } else {
                error("Expected local variable name");
                break;
            }
        }
        // local declarations do not emit runtime ops; they just reserve names
        return;
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
        } else if (next.type == TokenType::LEFT_BRACKET) {
            advance();
            Token nameTok = previous_token();
            std::vector<std::string> combined_locals;
            combined_locals.reserve(current_local_params_.size() + current_local_locals_.size());
            for (const auto &p : current_local_params_) combined_locals.push_back(p);
            for (const auto &l : current_local_locals_) combined_locals.push_back(l);
            auto it = std::find(combined_locals.begin(), combined_locals.end(), nameTok.lexeme);
            if (it != combined_locals.end()) {
                size_t idx = static_cast<size_t>(std::distance(combined_locals.begin(), it));
                emit_byte(static_cast<uint8_t>(OpCode::OP_GET_LOCAL));
                emit_byte(static_cast<uint8_t>(idx));
            } else {
                uint32_t name_id = strings_->intern(nameTok.lexeme);
                size_t name_constant = chunk_->add_constant(Value::string_id(name_id));
                emit_bytes(static_cast<uint8_t>(OpCode::OP_GET_GLOBAL), static_cast<uint8_t>(name_constant));
            }

            consume(TokenType::LEFT_BRACKET, "Expected '[' after variable name");
            expression();
            consume(TokenType::RIGHT_BRACKET, "Expected ']' after index");
            consume(TokenType::ASSIGN, "Expected '=' after index expression");
            expression(); // RHS
            emit_byte(static_cast<uint8_t>(OpCode::OP_INDEX_SET));
            emit_byte(static_cast<uint8_t>(OpCode::OP_POP));
            return;
        } else if (next.type == TokenType::LEFT_PAREN) {
            // function call with parentheses
            current_ = saved_current;
            expression_statement();
        } else if (next.type == TokenType::STRING || next.type == TokenType::NUMBER || next.type == TokenType::BOOLEAN || next.type == TokenType::NIL || next.type == TokenType::IDENTIFIER) {
            // Bare function call with one or more expressions as arguments ex. `Wait 1`
            Token name = current_token();
            advance();

            int arg_count = 0;
            while (check(TokenType::STRING) || check(TokenType::NUMBER) || check(TokenType::BOOLEAN) || check(TokenType::NIL) || check(TokenType::LEFT_PAREN) || check(TokenType::IDENTIFIER)) {
                expression();
                arg_count++;
            }

            uint32_t name_id = strings_->intern(name.lexeme);
            size_t name_const = chunk_->add_constant(Value::string_id(name_id));
            emit_byte(static_cast<uint8_t>(OpCode::OP_CALL_HOST));
            emit_byte(static_cast<uint8_t>(name_const));
            emit_byte(static_cast<uint8_t>(arg_count));
            emit_byte(static_cast<uint8_t>(OpCode::OP_POP)); // discard call result
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

bool Compiler::try_length_of_expression() {
    if (!(check(TokenType::IDENTIFIER) && current_token().lexeme == "length")) return false;
    if (current_ + 1 >= tokens_.size()) return false;
    const Token &next = tokens_[current_ + 1];
    if (!(next.type == TokenType::IDENTIFIER && next.lexeme == "of")) return false;

    advance();
    advance();

    if (match(TokenType::LEFT_PAREN)) {
        grouping();
    } else if (match(TokenType::IDENTIFIER)) {
        identifier();
        while (match(TokenType::LEFT_BRACKET)) {
            expression();
            consume(TokenType::RIGHT_BRACKET, "Expected ']' after index");
            emit_byte(static_cast<uint8_t>(OpCode::OP_ARRAY_GET));
        }
    } else if (match(TokenType::STRING) || match(TokenType::NUMBER) || match(TokenType::BOOLEAN) || match(TokenType::NIL)) {
        current_--;
        if (match(TokenType::STRING)) string();
        else if (match(TokenType::NUMBER)) number();
        else if (match(TokenType::BOOLEAN) || match(TokenType::NIL)) literal();
    } else {
        error("Expected a value after 'length of'");
        return true; 
    }

    uint32_t name_id = strings_->intern("length");
    size_t name_const = chunk_->add_constant(Value::string_id(name_id));
    emit_byte(static_cast<uint8_t>(OpCode::OP_CALL_HOST));
    emit_byte(static_cast<uint8_t>(name_const));
    emit_byte(static_cast<uint8_t>(1));

    return true;
}

bool Compiler::try_sugar_statement() {
    if (!check(TokenType::IDENTIFIER)) return false;
    std::string kw = current_token().lexeme;
    if (kw == "add") {
        advance();

        expression();

        if (!(check(TokenType::IDENTIFIER) && current_token().lexeme == "to")) {
            error("Expected 'to' after value in 'add' statement");
            return true; // handled
        }
        advance();

        if (match(TokenType::LEFT_PAREN)) {
            grouping();
        } else if (match(TokenType::IDENTIFIER)) {
            identifier();
            while (match(TokenType::LEFT_BRACKET)) {
                expression();
                consume(TokenType::RIGHT_BRACKET, "Expected ']' after index");
                emit_byte(static_cast<uint8_t>(OpCode::OP_INDEX_GET));
            }
        } else {
            error("Expected a list after 'to' in 'add' statement");
            return true;
        }

        uint32_t name_id = strings_->intern("add");
        size_t name_const = chunk_->add_constant(Value::string_id(name_id));
        emit_byte(static_cast<uint8_t>(OpCode::OP_CALL_HOST));
        emit_byte(static_cast<uint8_t>(name_const));
        emit_byte(static_cast<uint8_t>(2));
        emit_byte(static_cast<uint8_t>(OpCode::OP_POP));
        return true;
    }
    if (kw == "remove") {
        advance(); // consume 'remove'
        if (!check(TokenType::IDENTIFIER)) { error("Expected list name after 'remove'"); return true; }
        Token nameTok = current_token(); advance();

        std::vector<std::string> combined_locals;
        combined_locals.reserve(current_local_params_.size() + current_local_locals_.size());
        for (const auto &p : current_local_params_) combined_locals.push_back(p);
        for (const auto &l : current_local_locals_) combined_locals.push_back(l);
        auto it = std::find(combined_locals.begin(), combined_locals.end(), nameTok.lexeme);
        if (it != combined_locals.end()) {
            size_t idx = static_cast<size_t>(std::distance(combined_locals.begin(), it));
            emit_byte(static_cast<uint8_t>(OpCode::OP_GET_LOCAL));
            emit_byte(static_cast<uint8_t>(idx));
        } else {
            uint32_t name_id = strings_->intern(nameTok.lexeme);
            size_t name_constant = chunk_->add_constant(Value::string_id(name_id));
            emit_bytes(static_cast<uint8_t>(OpCode::OP_GET_GLOBAL), static_cast<uint8_t>(name_constant));
        }

        consume(TokenType::LEFT_BRACKET, "Expected '[' after list name");
        expression();
        consume(TokenType::RIGHT_BRACKET, "Expected ']' after index");

        uint32_t name_id2 = strings_->intern("remove");
        size_t name_const2 = chunk_->add_constant(Value::string_id(name_id2));
        emit_byte(static_cast<uint8_t>(OpCode::OP_CALL_HOST));
        emit_byte(static_cast<uint8_t>(name_const2));
        emit_byte(static_cast<uint8_t>(2));
        emit_byte(static_cast<uint8_t>(OpCode::OP_POP));
        return true;
    }
    if (kw == "clear") {
        advance();
        if (!check(TokenType::IDENTIFIER)) { error("Expected list name after 'clear'"); return true; }
        Token nameTok = current_token(); advance();

        std::vector<std::string> combined_locals;
        combined_locals.reserve(current_local_params_.size() + current_local_locals_.size());
        for (const auto &p : current_local_params_) combined_locals.push_back(p);
        for (const auto &l : current_local_locals_) combined_locals.push_back(l);
        auto it = std::find(combined_locals.begin(), combined_locals.end(), nameTok.lexeme);
        if (it != combined_locals.end()) {
            size_t idx = static_cast<size_t>(std::distance(combined_locals.begin(), it));
            emit_byte(static_cast<uint8_t>(OpCode::OP_GET_LOCAL));
            emit_byte(static_cast<uint8_t>(idx));
        } else {
            uint32_t name_id = strings_->intern(nameTok.lexeme);
            size_t name_constant = chunk_->add_constant(Value::string_id(name_id));
            emit_bytes(static_cast<uint8_t>(OpCode::OP_GET_GLOBAL), static_cast<uint8_t>(name_constant));
        }

        uint32_t name_id3 = strings_->intern("clear");
        size_t name_const3 = chunk_->add_constant(Value::string_id(name_id3));
        emit_byte(static_cast<uint8_t>(OpCode::OP_CALL_HOST));
        emit_byte(static_cast<uint8_t>(name_const3));
        emit_byte(static_cast<uint8_t>(1));
        emit_byte(static_cast<uint8_t>(OpCode::OP_POP));
        return true;
    }
    return false;
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
    
    set_variable_type(name.lexeme, last_expression_type_);
    
    std::vector<std::string> combined_locals;
    combined_locals.reserve(current_local_params_.size() + current_local_locals_.size());
    for (const auto &p : current_local_params_) combined_locals.push_back(p);
    for (const auto &l : current_local_locals_) combined_locals.push_back(l);

    auto it = std::find(combined_locals.begin(), combined_locals.end(), name.lexeme);
    if (it != combined_locals.end()) {
        size_t idx = static_cast<size_t>(std::distance(combined_locals.begin(), it));
        emit_byte(static_cast<uint8_t>(OpCode::OP_SET_LOCAL));
        emit_byte(static_cast<uint8_t>(idx));
    } else {
        size_t name_constant = chunk_->add_constant(Value::string_id(name_id));
        emit_bytes(static_cast<uint8_t>(OpCode::OP_SET_GLOBAL), static_cast<uint8_t>(name_constant));
    }
    emit_byte(static_cast<uint8_t>(OpCode::OP_POP));
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

void Compiler::while_statement() {
    consume(TokenType::WHILE, "Expected 'while'");
    
    size_t loop_start = chunk_->code().size();
    
    expression();
    
    consume(TokenType::DO, "Expected 'do' after while condition");
    
    size_t exit_jump = emit_jump(static_cast<uint8_t>(OpCode::OP_JUMP_IF_FALSE));
    
    while (!check(TokenType::END) && !check(TokenType::EOF_TOKEN)) {
        statement();
    }
    
    consume(TokenType::END, "Expected 'end' to close while loop");
    
    size_t jump_back_offset = chunk_->code().size() - loop_start + 2;
    if (jump_back_offset > 255) {
        error("Loop body too large");
        jump_back_offset = 255;
    }
    emit_byte(static_cast<uint8_t>(OpCode::OP_JUMP_BACK));
    emit_byte(static_cast<uint8_t>(jump_back_offset));
    
    patch_jump(exit_jump);
}

void Compiler::for_statement() {
    consume(TokenType::FOR, "Expected 'for'");

    // loop variable
    if (!check(TokenType::IDENTIFIER)) {
        error("Expected loop variable name");
        return;
    }
    Token name = current_token();
    advance();

    consume(TokenType::ASSIGN, "Expected '=' after loop variable");

    expression();
    uint32_t name_id = strings_->intern(name.lexeme);
    size_t name_const = chunk_->add_constant(Value::string_id(name_id));
    emit_bytes(static_cast<uint8_t>(OpCode::OP_SET_GLOBAL), static_cast<uint8_t>(name_const));

    consume(TokenType::COMMA, "Expected ',' after start value");
    
    expression();
    std::string end_var_name = "__for_end_" + name.lexeme;
    uint32_t end_name_id = strings_->intern(end_var_name);
    size_t end_name_const = chunk_->add_constant(Value::string_id(end_name_id));
    emit_bytes(static_cast<uint8_t>(OpCode::OP_SET_GLOBAL), static_cast<uint8_t>(end_name_const));
    emit_byte(static_cast<uint8_t>(OpCode::OP_POP)); // pop the assignment result
    
    size_t loop_start = chunk_->code().size();

    emit_bytes(static_cast<uint8_t>(OpCode::OP_GET_GLOBAL), static_cast<uint8_t>(name_const));

    emit_bytes(static_cast<uint8_t>(OpCode::OP_GET_GLOBAL), static_cast<uint8_t>(end_name_const));

    emit_byte(static_cast<uint8_t>(OpCode::OP_LESS_EQUAL));

    consume(TokenType::DO, "Expected a 'do' after for header");

    size_t exit_jump = emit_jump(static_cast<uint8_t>(OpCode::OP_JUMP_IF_FALSE));

    // body
    while (!check(TokenType::END) && !check(TokenType::EOF_TOKEN)) {
        statement();
    }

    consume(TokenType::END, "Expected 'end' to close for loop");

    // increment variable by 1
    // get var
    emit_bytes(static_cast<uint8_t>(OpCode::OP_GET_GLOBAL), static_cast<uint8_t>(name_const));
    // push constant 1
    size_t one_const = chunk_->add_constant(Value::integer(1));
    emit_bytes(static_cast<uint8_t>(OpCode::OP_CONSTANT), static_cast<uint8_t>(one_const));
    // add
    emit_byte(static_cast<uint8_t>(OpCode::OP_ADD));
    // store back
    emit_bytes(static_cast<uint8_t>(OpCode::OP_SET_GLOBAL), static_cast<uint8_t>(name_const));
    // pop the assignment result since it's not needed
    emit_byte(static_cast<uint8_t>(OpCode::OP_POP));

    size_t jump_back_offset = chunk_->code().size() - loop_start + 2;
    if (jump_back_offset > 255) {
        error("Loop body too large");
        jump_back_offset = 255;
    }
    emit_byte(static_cast<uint8_t>(OpCode::OP_JUMP_BACK));
    emit_byte(static_cast<uint8_t>(jump_back_offset));

    patch_jump(exit_jump);
}

void Compiler::function_declaration() {
    consume(TokenType::FUNCTION, "Expected 'function'");

    if (!check(TokenType::IDENTIFIER)) {
        error("Expected function name");
        return;
    }
    Token name = current_token();
    advance();
    std::string func_name = name.lexeme;

    std::vector<std::string> param_names;
    if (match(TokenType::LEFT_PAREN)) {
        if (check(TokenType::IDENTIFIER)) {
            Token p = current_token(); advance();
            param_names.push_back(p.lexeme);
            while (match(TokenType::COMMA)) {
                if (check(TokenType::IDENTIFIER)) {
                    Token param = current_token(); advance();
                    param_names.push_back(param.lexeme);
                } else {
                    error("Expected parameter name");
                    break;
                }
            }
        }
        consume(TokenType::RIGHT_PAREN, "No ')' after a parameter list");
    }

    // Compile function body into a new Chunk
    Chunk func_chunk;
    StringTable local_strings = *strings_;

    // Save current chunk and switch to function chunk
    Chunk* saved_chunk = chunk_;
    chunk_ = &func_chunk;
    current_local_params_ = param_names;
    current_local_locals_.clear();

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

    // Combine params + locals for chunk storage (params first, then locals)
    std::vector<std::string> combined;
    combined.reserve(current_local_params_.size() + current_local_locals_.size());
    for (const auto &p : current_local_params_) combined.push_back(p);
    for (const auto &l : current_local_locals_) combined.push_back(l);

    std::string func_name_lc = func_name;
    for (auto &c : func_name_lc) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    chunk_->add_function(func_chunk, param_names, combined, func_name_lc);

    current_local_params_.clear();
    current_local_locals_.clear();
}

/*
void Compiler::table_declaration() {
    consume(TokenType::TABLE, "Expected 'table'");
    
    // Table name
    if (!check(TokenType::IDENTIFIER)) {
        error("Expected table name");
        return;
    }
    Token name = current_token();
    advance();
    
    consume(TokenType::LEFT_BRACE, "Expected '{' after table name");
    
    uint32_t table_id = static_cast<uint32_t>(chunk_->constants().size());
    Value table_value = Value::table_id(table_id);
    size_t constant_index = chunk_->add_constant(table_value);
    
    // Parse table elements (skip newlines)
    int element_count = 0;
    
    // Skip initial newlines
    while (match(TokenType::NEWLINE)) {}
    
    if (!check(TokenType::RIGHT_BRACE)) {
        expression();
        element_count++;
        
        // Parse remaining elements (comma-separated newlines allowed)
        while (true) {
            while (match(TokenType::NEWLINE)) {}
            
            if (check(TokenType::RIGHT_BRACE)) break;
            
            if (match(TokenType::COMMA)) {
                while (match(TokenType::NEWLINE)) {}
                
                if (check(TokenType::RIGHT_BRACE)) break; // trailing comma
                
                expression();
                element_count++;
            } else {
                break;
            }
        }
    }
    
    while (match(TokenType::NEWLINE)) {}
    
    consume(TokenType::RIGHT_BRACE, "Expected '}' after table elements");
    
    (void)element_count;
    
    emit_byte(static_cast<uint8_t>(OpCode::OP_CONSTANT));
    emit_byte(static_cast<uint8_t>(constant_index));
    
    uint32_t name_id = strings_->intern(name.lexeme);
    Value name_value = Value::string_id(name_id);
    size_t name_constant = chunk_->add_constant(name_value);
    
    emit_byte(static_cast<uint8_t>(OpCode::OP_SET_GLOBAL));
    emit_byte(static_cast<uint8_t>(name_constant));
}
*/

void Compiler::call_expression() {
    Token name = previous_token();
    std::string func_name = name.lexeme;

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

        bool has_more = (check(TokenType::STRING) || check(TokenType::NUMBER) || 
                        check(TokenType::BOOLEAN) || check(TokenType::NIL) || 
                        check(TokenType::LEFT_PAREN) || check(TokenType::IDENTIFIER));
        
        if (has_more) {
            emit_byte(static_cast<uint8_t>(OpCode::OP_PRINT_SPACE));
        } else {
            emit_byte(static_cast<uint8_t>(OpCode::OP_PRINT));
        }
    } while (check(TokenType::STRING) || check(TokenType::NUMBER) || 
             check(TokenType::BOOLEAN) || check(TokenType::NIL) || 
             check(TokenType::LEFT_PAREN) || check(TokenType::IDENTIFIER));
    
    // Suppress unused variable warning for expression_count
    (void)expression_count;
}

void Compiler::return_statement() {
    consume(TokenType::RETURN, "Expected 'return'");

    if (check(TokenType::NEWLINE) || check(TokenType::END) || check(TokenType::EOF_TOKEN)) {
        emit_byte(static_cast<uint8_t>(OpCode::OP_NIL));
    } else {
        expression();
    }
    emit_byte(static_cast<uint8_t>(OpCode::OP_RETURN));
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

void Compiler::thread_jumps() {
    const auto& code = chunk_->code();
    size_t n = code.size();
    
    // Lua 5.4 style aggressive jump threading
    // Can optimize both unconditional jumps and conditional jumps leading to jumps
    // Problem with this is that the compiler already builds very efficient jumps so this doesn't get used much, wow.
    size_t total_jumps_found = 0;
    for (size_t i = 0; i + 1 < n; ++i) {
        uint8_t instr = code[i];
        if (instr != static_cast<uint8_t>(OpCode::OP_JUMP) && 
            instr != static_cast<uint8_t>(OpCode::OP_JUMP_IF_FALSE)) {
            continue;
        }
        
        total_jumps_found++;
        
        // Debug: print jump details
        // std::cout << "Jump at " << i << ": " 
        //          << (instr == static_cast<uint8_t>(OpCode::OP_JUMP) ? "OP_JUMP" : "OP_JUMP_IF_FALSE");

        size_t offset_idx = i + 1;
        if (offset_idx >= n) continue;
        uint8_t off = code[offset_idx];
        size_t dest = offset_idx + static_cast<size_t>(off);
        
        std::cout << " -> " << dest;
        if (dest < n) {
            uint8_t target_instr = code[dest];
            if (target_instr == static_cast<uint8_t>(OpCode::OP_JUMP)) {
                std::cout << " (target: OP_JUMP)";
            } else if (target_instr == static_cast<uint8_t>(OpCode::OP_JUMP_IF_FALSE)) {
                std::cout << " (target: OP_JUMP_IF_FALSE)";
            } else {
                std::cout << " (target: op" << static_cast<int>(target_instr) << ")";
            }
        }
        std::cout << std::endl;

        int follow = 0;
        size_t original_dest = dest;
        
        while (dest < n && follow < 64) {
            uint8_t target_instr = code[dest];
            
            if (target_instr == static_cast<uint8_t>(OpCode::OP_JUMP)) {
                if (dest + 1 >= n) break;
                uint8_t next_off = code[dest + 1];
                size_t next_dest = dest + 1 + static_cast<size_t>(next_off);
                if (next_dest == dest) break; // Avoid infinite loops
                dest = next_dest;
                ++follow;
                continue;
            }
            
            if (target_instr == static_cast<uint8_t>(OpCode::OP_JUMP_IF_FALSE)) {
                
                if (instr == static_cast<uint8_t>(OpCode::OP_JUMP_IF_FALSE)) {
                    if (dest + 1 >= n) break;
                    uint8_t next_off = code[dest + 1];
                    size_t next_dest = dest + 1 + static_cast<size_t>(next_off);
                    if (next_dest == dest) break;
                    dest = next_dest;
                    ++follow;
                    continue;
                }

                if (dest + 2 < n) {
                    size_t cond_target = dest + 1 + static_cast<size_t>(code[dest + 1]);
                    if (cond_target < n && code[cond_target] == static_cast<uint8_t>(OpCode::OP_JUMP)) {
                        if (cond_target + 1 < n) {
                            size_t final_target = cond_target + 1 + static_cast<size_t>(code[cond_target + 1]);
                            if (final_target != dest && final_target < n) {
                                dest = final_target;
                                ++follow;
                                continue;
                            }
                        }
                    }
                }
            }
            
            if (target_instr == static_cast<uint8_t>(OpCode::OP_POP)) {
                dest++;
                ++follow;
                continue;
            }
            
            if (target_instr == static_cast<uint8_t>(OpCode::OP_RETURN)) {
                break;
            }
            
            break;
        }

        if (dest != original_dest && dest < n) {
            if (dest >= offset_idx) {
                size_t new_off_sz = dest - offset_idx;
                uint8_t new_off = (new_off_sz > 255) ? 255 : static_cast<uint8_t>(new_off_sz);
                chunk_->patch_byte(offset_idx, new_off);
                stats_.jump_threads_applied++;
                
                #ifdef DEBUG_JUMP_THREADING
                std::cout << "Jump threading: " << i << " -> " << dest << " (saved " << (dest - original_dest) << " steps)" << std::endl;
                #endif
            }
        }
    }
    
    // Debug output
    std::cout << "Jump threading analysis: " << total_jumps_found << " jumps found, " 
              << stats_.jump_threads_applied << " optimizations applied" << std::endl;
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
        case TokenType::MODULO:
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
        case TokenType::MODULO:
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
        case TokenType::MODULO: return OpCode::OP_MODULO;
        case TokenType::EQUAL: return OpCode::OP_EQUAL;
        case TokenType::GREATER: return OpCode::OP_GREATER;
        case TokenType::GREATER_EQUAL: return OpCode::OP_GREATER_EQUAL;
        case TokenType::LESS: return OpCode::OP_LESS;
        case TokenType::LESS_EQUAL: return OpCode::OP_LESS_EQUAL;
        default:
            error("UNKNOWN binary operator");
            return OpCode::OP_ADD; // fallback
    }
}

InferredType Compiler::infer_literal_type(const Token& token) {
    switch (token.type) {
        case TokenType::NUMBER:
            return token.lexeme.find('.') != std::string::npos ? 
                   InferredType::FLOAT : InferredType::INTEGER;
        case TokenType::STRING:
            return InferredType::STRING;
        case TokenType::BOOLEAN:
            return InferredType::BOOLEAN;
        case TokenType::NIL:
            return InferredType::NIL;
        default:
            return InferredType::UNKNOWN;
    }
}

InferredType Compiler::infer_variable_type(const std::string& name) {
    auto it = variable_types_.find(name);
    return (it != variable_types_.end()) ? it->second : InferredType::UNKNOWN;
}

void Compiler::set_variable_type(const std::string& name, InferredType type) {
    variable_types_[name] = type;
}

OpCode Compiler::get_specialized_opcode(TokenType op, InferredType left_type, InferredType right_type) {
    // For unknown types try to pick the most likely specialized opcode
    // This prevents falling back to broken generic opcodes
    
    if (op == TokenType::PLUS && 
        (left_type == InferredType::STRING || right_type == InferredType::STRING)) {
        return OpCode::OP_ADD_STRING;
    }
    
    // Optimize for same-type operations
    if (left_type == right_type && left_type != InferredType::UNKNOWN) {
        switch (op) {
            case TokenType::PLUS:
                if (left_type == InferredType::INTEGER) return OpCode::OP_ADD_INT;
                if (left_type == InferredType::FLOAT) return OpCode::OP_ADD_FLOAT;
                break;
            case TokenType::MINUS:
                if (left_type == InferredType::INTEGER) return OpCode::OP_SUB_INT;
                if (left_type == InferredType::FLOAT) return OpCode::OP_SUB_FLOAT;
                break;
            case TokenType::MULTIPLY:
                if (left_type == InferredType::INTEGER) return OpCode::OP_MUL_INT;
                if (left_type == InferredType::FLOAT) return OpCode::OP_MUL_FLOAT;
                break;
            case TokenType::DIVIDE:
                if (left_type == InferredType::INTEGER) return OpCode::OP_DIV_INT;
                if (left_type == InferredType::FLOAT) return OpCode::OP_DIV_FLOAT;
                break;
            case TokenType::MODULO:
                if (left_type == InferredType::INTEGER) return OpCode::OP_MOD_INT;
                break;
            default:
                // Other token types are not specialized here; fall through to caller
                break;
        }
    }
    
    // If we couldnt decide on a specialized opcode, fall back to the
    // generic opcode mapping. Emitting an integer specialized opcode when
    // the runtime values are floats can cause incorrect behavior (reading
    // the float bit pattern as an integer). The safer default is generic
    // bytecode which performs proper runtime type checks
    return token_to_opcode(op);
}

void Compiler::emit_optimized_binary_op(TokenType op, InferredType left_type, InferredType right_type) {
    OpCode specialized_op = get_specialized_opcode(op, left_type, right_type);
    
    if (specialized_op != token_to_opcode(op)) {
        stats_.specialized_ops_emitted++;
    } else {
        stats_.generic_ops_emitted++;
    }
    
    const auto& code = chunk_->code();
    if (code.size() >= 4) {
        size_t n = code.size();
        uint8_t b3 = code[n-4];
        uint8_t b2 = code[n-3];
        uint8_t b1 = code[n-2];
        uint8_t b0 = code[n-1];

        if (static_cast<OpCode>(b3) == OpCode::OP_CONSTANT && static_cast<OpCode>(b1) == OpCode::OP_CONSTANT) {
            uint8_t idx_a = b2;
            uint8_t idx_b = b0;
            Value a = chunk_->get_constant(idx_a);
            Value b = chunk_->get_constant(idx_b);
            bool foldable = false;
            Value result = Value::nil();
            if ((op == TokenType::PLUS || op == TokenType::MINUS || op == TokenType::MULTIPLY || op == TokenType::DIVIDE || op == TokenType::MODULO)) {
                if ((a.type() == ValueType::INT || a.type() == ValueType::FLOAT) && (b.type() == ValueType::INT || b.type() == ValueType::FLOAT)) {
                    double da = (a.type() == ValueType::FLOAT) ? a.as_floating() : static_cast<double>(a.as_integer());
                    double db = (b.type() == ValueType::FLOAT) ? b.as_floating() : static_cast<double>(b.as_integer());
                    double r = 0.0;
                    switch (op) {
                        case TokenType::PLUS: r = da + db; break;
                        case TokenType::MINUS: r = da - db; break;
                        case TokenType::MULTIPLY: r = da * db; break;
                        case TokenType::DIVIDE: r = da / db; break;
                        case TokenType::MODULO: {
                            if (a.type() == ValueType::INT && b.type() == ValueType::INT) {
                                int64_t ri = a.as_integer() % b.as_integer();
                                result = Value::integer(ri);
                                foldable = true;
                            }
                            break;
                        }
                        default: break;
                    }
                    if (!foldable) {
                        if (a.type() == ValueType::FLOAT || b.type() == ValueType::FLOAT) {
                            result = Value::floating(r);
                            foldable = true;
                        } else {
                            int64_t ri = static_cast<int64_t>(r);
                            result = Value::integer(ri);
                            foldable = true;
                        }
                    }
                }
            }

            if (foldable) {
                auto& code_mut = const_cast<std::vector<uint8_t>&>(chunk_->code());
                code_mut.resize(code_mut.size() - 4);
                emit_constant(result);
                stats_.constant_folds++;
                return;
            }
        }
        if (code.size() >= 4) {
            size_t n2 = code.size();
            uint8_t p3 = code[n2-4];
            uint8_t p2 = code[n2-3];
            uint8_t p1 = code[n2-2];
            uint8_t p0 = code[n2-1];

            if (static_cast<OpCode>(p3) == OpCode::OP_GET_LOCAL && static_cast<OpCode>(p1) == OpCode::OP_GET_LOCAL) {
                uint8_t idx_a = p2;
                uint8_t idx_b = p0;
                OpCode fused = OpCode::OP_ADD_LOCAL;
                if (specialized_op == OpCode::OP_ADD_FLOAT) fused = OpCode::OP_ADD_FLOAT_LOCAL;
                else if (specialized_op == OpCode::OP_ADD_STRING) fused = OpCode::OP_ADD_STRING_LOCAL;

                auto& code_mut = const_cast<std::vector<uint8_t>&>(chunk_->code());
                code_mut.resize(code_mut.size() - 4);
                chunk_->write_byte(static_cast<uint8_t>(fused), current_token().line);
                chunk_->write_byte(idx_a, current_token().line);
                chunk_->write_byte(idx_b, current_token().line);
                stats_.specialized_ops_emitted++;
                return;
            }

            if (static_cast<OpCode>(p3) == OpCode::OP_GET_LOCAL && static_cast<OpCode>(p1) == OpCode::OP_CONSTANT) {
                uint8_t idx_a = p2;
                uint8_t const_idx = p0;
                OpCode fused = OpCode::OP_ADD_LOCAL_CONST;
                if (specialized_op == OpCode::OP_ADD_FLOAT) fused = OpCode::OP_ADD_LOCAL_CONST_FLOAT;

                auto& code_mut = const_cast<std::vector<uint8_t>&>(chunk_->code());
                code_mut.resize(code_mut.size() - 4);
                chunk_->write_byte(static_cast<uint8_t>(fused), current_token().line);
                chunk_->write_byte(idx_a, current_token().line);
                chunk_->write_byte(const_idx, current_token().line);
                stats_.specialized_ops_emitted++;
                return;
            }

            if (static_cast<OpCode>(p3) == OpCode::OP_CONSTANT && static_cast<OpCode>(p1) == OpCode::OP_GET_LOCAL) {
                uint8_t const_idx = p2;
                uint8_t idx_a = p0;
                OpCode fused = OpCode::OP_ADD_CONST_LOCAL;
                if (specialized_op == OpCode::OP_ADD_FLOAT) fused = OpCode::OP_ADD_CONST_LOCAL_FLOAT;

                auto& code_mut = const_cast<std::vector<uint8_t>&>(chunk_->code());
                code_mut.resize(code_mut.size() - 4);
                chunk_->write_byte(static_cast<uint8_t>(fused), current_token().line);
                chunk_->write_byte(const_idx, current_token().line);
                chunk_->write_byte(idx_a, current_token().line);
                stats_.specialized_ops_emitted++;
                return;
            }
        }
    }

    emit_byte(static_cast<uint8_t>(specialized_op));
}

bool Compiler::load_cached_bytecode(const std::string& source_path, Chunk& chunk, StringTable& strings) {
    std::string cache_path = source_path + ".nsc"; // NightScript Compiled
    
    std::ifstream cache_file(cache_path, std::ios::binary);
    if (!cache_file.is_open()) {
        return false;
    }
    
    uint32_t magic;
    uint16_t version;
    uint64_t cached_timestamp;
    
    cache_file.read(reinterpret_cast<char*>(&magic), sizeof(magic));
    cache_file.read(reinterpret_cast<char*>(&version), sizeof(version));
    cache_file.read(reinterpret_cast<char*>(&cached_timestamp), sizeof(cached_timestamp));
    
    if (magic != 0x4E534300 || version != 2) { // "NSC\0"
        return false; // Invalid cache
    }
    
    // Check if source file is newer than cache
    struct stat source_stat;
    if (stat(source_path.c_str(), &source_stat) == 0) {
        if (static_cast<uint64_t>(source_stat.st_mtime) > cached_timestamp) {
            return false; // Source is newer cache is old
        }
    }
    
    // Load bytecode
    try {
        // Read constants count
        uint32_t constants_count;
        cache_file.read(reinterpret_cast<char*>(&constants_count), sizeof(constants_count));
        
        for (uint32_t i = 0; i < constants_count; ++i) {
            uint8_t type;
            cache_file.read(reinterpret_cast<char*>(&type), sizeof(type));

            switch (static_cast<ValueType>(type)) {
                case ValueType::NIL: {
                    chunk.add_constant(Value::nil());
                    break;
                }
                case ValueType::BOOL: {
                    uint8_t b;
                    cache_file.read(reinterpret_cast<char*>(&b), sizeof(b));
                    chunk.add_constant(Value::boolean(b != 0));
                    break;
                }
                case ValueType::INT: {
                    int64_t v;
                    cache_file.read(reinterpret_cast<char*>(&v), sizeof(v));
                    chunk.add_constant(Value::integer(v));
                    break;
                }
                case ValueType::FLOAT: {
                    double d;
                    cache_file.read(reinterpret_cast<char*>(&d), sizeof(d));
                    chunk.add_constant(Value::floating(d));
                    break;
                }
                case ValueType::STRING_ID: {
                    uint32_t str_len;
                    cache_file.read(reinterpret_cast<char*>(&str_len), sizeof(str_len));
                    std::string str(str_len, '\0');
                    cache_file.read(&str[0], str_len);
                    uint32_t id = strings.intern(str);
                    chunk.add_constant(Value::string_id(id));
                    break;
                }
                default: {
                    return false;
                }
            }
        }
        
        // Read bytecode
        uint32_t code_size;
        cache_file.read(reinterpret_cast<char*>(&code_size), sizeof(code_size));
        
        for (uint32_t i = 0; i < code_size; ++i) {
            uint8_t byte;
            cache_file.read(reinterpret_cast<char*>(&byte), sizeof(byte));
            chunk.write_byte(byte, 1); // Line numbers not cached for simplicity
        }

        // Read functions (top-level)
        uint32_t functions_count = 0;
        cache_file.read(reinterpret_cast<char*>(&functions_count), sizeof(functions_count));
        for (uint32_t fi = 0; fi < functions_count; ++fi) {
            // Function name
            uint32_t fname_len;
            cache_file.read(reinterpret_cast<char*>(&fname_len), sizeof(fname_len));
            std::string fname(fname_len, '\0');
            cache_file.read(&fname[0], fname_len);

            // Parameters
            uint32_t param_count;
            cache_file.read(reinterpret_cast<char*>(&param_count), sizeof(param_count));
            std::vector<std::string> param_names;
            for (uint32_t pi = 0; pi < param_count; ++pi) {
                uint32_t plen;
                cache_file.read(reinterpret_cast<char*>(&plen), sizeof(plen));
                std::string pname(plen, '\0');
                cache_file.read(&pname[0], plen);
                param_names.push_back(pname);
            }

            // lol i forgor
            uint32_t local_count = 0;
            cache_file.read(reinterpret_cast<char*>(&local_count), sizeof(local_count));
            std::vector<std::string> local_names;
            for (uint32_t li = 0; li < local_count; ++li) {
                uint32_t llen;
                cache_file.read(reinterpret_cast<char*>(&llen), sizeof(llen));
                std::string lname(llen, '\0');
                cache_file.read(&lname[0], llen);
                local_names.push_back(lname);
            }

            // Function constants
            uint32_t fconst_count;
            cache_file.read(reinterpret_cast<char*>(&fconst_count), sizeof(fconst_count));
            Chunk fchunk;
            for (uint32_t ci = 0; ci < fconst_count; ++ci) {
                uint8_t type;
                cache_file.read(reinterpret_cast<char*>(&type), sizeof(type));
                switch (static_cast<ValueType>(type)) {
                    case ValueType::NIL:
                        fchunk.add_constant(Value::nil());
                        break;
                    case ValueType::BOOL: {
                        uint8_t b; cache_file.read(reinterpret_cast<char*>(&b), sizeof(b));
                        fchunk.add_constant(Value::boolean(b != 0));
                        break;
                    }
                    case ValueType::INT: {
                        int64_t v; cache_file.read(reinterpret_cast<char*>(&v), sizeof(v));
                        fchunk.add_constant(Value::integer(v));
                        break;
                    }
                    case ValueType::FLOAT: {
                        double d; cache_file.read(reinterpret_cast<char*>(&d), sizeof(d));
                        fchunk.add_constant(Value::floating(d));
                        break;
                    }
                    case ValueType::STRING_ID: {
                        uint32_t sl; cache_file.read(reinterpret_cast<char*>(&sl), sizeof(sl));
                        std::string s(sl, '\0'); cache_file.read(&s[0], sl);
                        uint32_t id = strings.intern(s);
                        fchunk.add_constant(Value::string_id(id));
                        break;
                    }
                    default:
                        return false;
                }
            }

            // Function code
            uint32_t fcode_size;
            cache_file.read(reinterpret_cast<char*>(&fcode_size), sizeof(fcode_size));
            for (uint32_t bi = 0; bi < fcode_size; ++bi) {
                uint8_t byte; cache_file.read(reinterpret_cast<char*>(&byte), sizeof(byte));
                fchunk.write_byte(byte, 1);
            }

            // Add function to parent chunk (with locals)
            chunk.add_function(fchunk, param_names, local_names, fname);
        }

        return true;
    } catch (...) {
        return false; // Error reading cache
    }
}

void Compiler::save_bytecode_cache(const std::string& source_path, const Chunk& chunk, const StringTable& strings) {
    std::string cache_path = source_path + ".nsc";
    
    std::ofstream cache_file(cache_path, std::ios::binary);
    if (!cache_file.is_open()) {
        return; // Can't create cache file
    }
    
    // Write header
    uint32_t magic = 0x4E534300; // "NSC\0"
    uint16_t version = 2;
    uint64_t timestamp = static_cast<uint64_t>(time(nullptr));
    
    cache_file.write(reinterpret_cast<const char*>(&magic), sizeof(magic));
    cache_file.write(reinterpret_cast<const char*>(&version), sizeof(version));
    cache_file.write(reinterpret_cast<const char*>(&timestamp), sizeof(timestamp));
    
    // Write constants
    const auto& constants = chunk.constants();
    uint32_t constants_count = static_cast<uint32_t>(constants.size());
    cache_file.write(reinterpret_cast<const char*>(&constants_count), sizeof(constants_count));
    
    for (const auto& constant : constants) {
        uint8_t type = static_cast<uint8_t>(constant.type());
        cache_file.write(reinterpret_cast<const char*>(&type), sizeof(type));

        switch (constant.type()) {
            case ValueType::NIL: {
                // nothing more to write
                break;
            }
            case ValueType::BOOL: {
                uint8_t b = constant.as_boolean() ? 1 : 0;
                cache_file.write(reinterpret_cast<const char*>(&b), sizeof(b));
                break;
            }
            case ValueType::INT: {
                int64_t v = constant.as_integer();
                cache_file.write(reinterpret_cast<const char*>(&v), sizeof(v));
                break;
            }
            case ValueType::FLOAT: {
                double d = constant.as_floating();
                cache_file.write(reinterpret_cast<const char*>(&d), sizeof(d));
                break;
            }
            case ValueType::STRING_ID: {
                const std::string& str = strings.get_string(constant.as_string_id());
                uint32_t str_len = static_cast<uint32_t>(str.length());
                cache_file.write(reinterpret_cast<const char*>(&str_len), sizeof(str_len));
                cache_file.write(str.c_str(), str_len);
                break;
            }
            default: {
                // skip (shouldn't occur)
                break;
            }
        }
    }
    
    // Write bytecode
    const auto& code = chunk.code();
    uint32_t code_size = static_cast<uint32_t>(code.size());
    cache_file.write(reinterpret_cast<const char*>(&code_size), sizeof(code_size));
    cache_file.write(reinterpret_cast<const char*>(code.data()), code_size);

    // Write functions (top-level only)
    uint32_t functions_count = static_cast<uint32_t>(chunk.function_count());
    cache_file.write(reinterpret_cast<const char*>(&functions_count), sizeof(functions_count));

    for (uint32_t fi = 0; fi < functions_count; ++fi) {
        const Chunk& fchunk = chunk.get_function(fi);
        const auto& param_names = chunk.get_function_param_names(fi);
        const std::string& fname = chunk.function_name(fi);

        // Function name
        uint32_t fname_len = static_cast<uint32_t>(fname.length());
        cache_file.write(reinterpret_cast<const char*>(&fname_len), sizeof(fname_len));
        cache_file.write(fname.c_str(), fname_len);

        // Parameters
        uint32_t param_count = static_cast<uint32_t>(param_names.size());
        cache_file.write(reinterpret_cast<const char*>(&param_count), sizeof(param_count));
        for (const auto& p : param_names) {
            uint32_t plen = static_cast<uint32_t>(p.length());
            cache_file.write(reinterpret_cast<const char*>(&plen), sizeof(plen));
            cache_file.write(p.c_str(), plen);
        }

        // Locals (combined names stored in parent chunk as function_local_names)
        const auto& local_names = chunk.get_function_local_names(fi);
        uint32_t local_count = static_cast<uint32_t>(local_names.size());
        cache_file.write(reinterpret_cast<const char*>(&local_count), sizeof(local_count));
        for (const auto& l : local_names) {
            uint32_t llen = static_cast<uint32_t>(l.length());
            cache_file.write(reinterpret_cast<const char*>(&llen), sizeof(llen));
            cache_file.write(l.c_str(), llen);
        }

        // Function constants
        const auto& fconsts = fchunk.constants();
        uint32_t fconst_count = static_cast<uint32_t>(fconsts.size());
        cache_file.write(reinterpret_cast<const char*>(&fconst_count), sizeof(fconst_count));
        for (const auto& constant : fconsts) {
            uint8_t type = static_cast<uint8_t>(constant.type());
            cache_file.write(reinterpret_cast<const char*>(&type), sizeof(type));
            switch (constant.type()) {
                case ValueType::NIL: break;
                case ValueType::BOOL: {
                    uint8_t b = constant.as_boolean() ? 1 : 0;
                    cache_file.write(reinterpret_cast<const char*>(&b), sizeof(b));
                    break;
                }
                case ValueType::INT: {
                    int64_t v = constant.as_integer();
                    cache_file.write(reinterpret_cast<const char*>(&v), sizeof(v));
                    break;
                }
                case ValueType::FLOAT: {
                    double d = constant.as_floating();
                    cache_file.write(reinterpret_cast<const char*>(&d), sizeof(d));
                    break;
                }
                case ValueType::STRING_ID: {
                    const std::string& str = strings.get_string(constant.as_string_id());
                    uint32_t str_len = static_cast<uint32_t>(str.length());
                    cache_file.write(reinterpret_cast<const char*>(&str_len), sizeof(str_len));
                    cache_file.write(str.c_str(), str_len);
                    break;
                }
                default: break;
            }
        }

        // Function code
        const auto& fcode = fchunk.code();
        uint32_t fcode_size = static_cast<uint32_t>(fcode.size());
        cache_file.write(reinterpret_cast<const char*>(&fcode_size), sizeof(fcode_size));
        if (fcode_size > 0) cache_file.write(reinterpret_cast<const char*>(fcode.data()), fcode_size);
    }
}

} // namespace nightscript
} // namespace nightforge