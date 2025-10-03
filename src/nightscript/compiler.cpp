#include "compiler.h"
#include <iostream>
#include <cstdlib>
#include <fstream>
#include <sys/stat.h>
#include <ctime>

namespace nightforge {
namespace nightscript {

Compiler::Compiler() : current_(0), chunk_(nullptr), strings_(nullptr), 
                      last_expression_type_(InferredType::UNKNOWN), is_tail_position_(false),
                      had_error_(false), panic_mode_(false) {
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

    // Add variable name to constants table and emit OP_GET_GLOBAL with index
    size_t name_constant = chunk_->add_constant(Value::string_id(name_id));
    emit_bytes(static_cast<uint8_t>(OpCode::OP_GET_GLOBAL), static_cast<uint8_t>(name_constant));
    
    last_expression_type_ = infer_variable_type(name.lexeme);
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
    // Check for while statement
    } else if (check(TokenType::WHILE)) {
        while_statement();
    } else if (check(TokenType::FOR)) {
        for_statement();
    } else if (check(TokenType::RETURN)) {
        return_statement();
    } else if (check(TokenType::FUNCTION)) {
        function_declaration();
    } else if (check(TokenType::TABLE)) {
        table_declaration();
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
    
    // Add variable name to constants table and emit OP_SET_GLOBAL with index
    size_t name_constant = chunk_->add_constant(Value::string_id(name_id));
    emit_bytes(static_cast<uint8_t>(OpCode::OP_SET_GLOBAL), static_cast<uint8_t>(name_constant));
    // This is a statement-level assignment; discard the resulting value
    // so it doesn't accumulate on the VM stack across iterations.
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

    std::string func_name_lc = func_name;
    for (auto &c : func_name_lc) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    chunk_->add_function(func_chunk, param_names, func_name_lc);
}

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
    
    emit_byte(static_cast<uint8_t>(OpCode::OP_CONSTANT));
    emit_byte(static_cast<uint8_t>(constant_index));
    
    uint32_t name_id = strings_->intern(name.lexeme);
    Value name_value = Value::string_id(name_id);
    size_t name_constant = chunk_->add_constant(name_value);
    
    emit_byte(static_cast<uint8_t>(OpCode::OP_SET_GLOBAL));
    emit_byte(static_cast<uint8_t>(name_constant));
}

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
    
    emit_byte(static_cast<uint8_t>(specialized_op));
}

void Compiler::emit_tail_call_optimized(const std::string& func_name, uint8_t arg_count) {
    // For tail calls, we can reuse the current stack frame
    // Instead of pushing a new frame replace the current one
    
    if (is_tail_position_) {
        size_t name_const = chunk_->add_constant(Value::string_id(strings_->intern(func_name)));
        emit_bytes(static_cast<uint8_t>(OpCode::OP_CALL_HOST), static_cast<uint8_t>(name_const));
        emit_byte(arg_count);
        emit_byte(1);
        stats_.tail_calls_optimized++;
    } else {
        size_t name_const = chunk_->add_constant(Value::string_id(strings_->intern(func_name)));
        emit_bytes(static_cast<uint8_t>(OpCode::OP_CALL_HOST), static_cast<uint8_t>(name_const));
        emit_byte(arg_count);
    }
}

bool Compiler::load_cached_bytecode(const std::string& source_path, Chunk& chunk) {
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
    
    if (magic != 0x4E534300 || version != 1) { // "NSC\0" (losing it)
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
        
        // Read constants (simplified only strings for now)
        for (uint32_t i = 0; i < constants_count; ++i) {
            uint8_t type;
            cache_file.read(reinterpret_cast<char*>(&type), sizeof(type));
            
            if (type == static_cast<uint8_t>(ValueType::STRING_ID)) {
                uint32_t str_len;
                cache_file.read(reinterpret_cast<char*>(&str_len), sizeof(str_len));
                std::string str(str_len, '\0');
                cache_file.read(&str[0], str_len);
                uint32_t id = strings_->intern(str);
                chunk.add_constant(Value::string_id(id));
            }
            // TODO: Add other value types
        }
        
        // Read bytecode
        uint32_t code_size;
        cache_file.read(reinterpret_cast<char*>(&code_size), sizeof(code_size));
        
        for (uint32_t i = 0; i < code_size; ++i) {
            uint8_t byte;
            cache_file.read(reinterpret_cast<char*>(&byte), sizeof(byte));
            chunk.write_byte(byte, 1); // Line numbers not cached for simplicity
        }
        
        return true;
    } catch (...) {
        return false; // Error reading cache
    }
}

void Compiler::save_bytecode_cache(const std::string& source_path, const Chunk& chunk) {
    std::string cache_path = source_path + ".nsc";
    
    std::ofstream cache_file(cache_path, std::ios::binary);
    if (!cache_file.is_open()) {
        return; // Can't create cache file
    }
    
    // Write header
    uint32_t magic = 0x4E534300; // "NSC\0"
    uint16_t version = 1;
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
        
        if (constant.type() == ValueType::STRING_ID) {
            const std::string& str = strings_->get_string(constant.as_string_id());
            uint32_t str_len = static_cast<uint32_t>(str.length());
            cache_file.write(reinterpret_cast<const char*>(&str_len), sizeof(str_len));
            cache_file.write(str.c_str(), str_len);
        }
        // TODO: Add other value types
    }
    
    // Write bytecode
    const auto& code = chunk.code();
    uint32_t code_size = static_cast<uint32_t>(code.size());
    cache_file.write(reinterpret_cast<const char*>(&code_size), sizeof(code_size));
    cache_file.write(reinterpret_cast<const char*>(code.data()), code_size);
}

} // namespace nightscript
} // namespace nightforge