#pragma once
#include "lexer.h"
#include "value.h"
#include <unordered_map>

namespace nightforge {
namespace nightscript {

// Type inference for optimized opcode emission
enum class InferredType {
    UNKNOWN,
    INTEGER,
    FLOAT,
    STRING,
    BOOLEAN,
    NIL
};

class Compiler {
public:
    // Performance stats
    struct CompileStats {
        size_t specialized_ops_emitted = 0;
        size_t generic_ops_emitted = 0;
        size_t tail_calls_optimized = 0;
        size_t constant_folds = 0;
        size_t jump_threads_applied = 0;
    };

    Compiler();
    
    bool compile(const std::string& source, Chunk& chunk, StringTable& strings);
    
    bool load_cached_bytecode(const std::string& source_path, Chunk& chunk, StringTable& strings);
    void save_bytecode_cache(const std::string& source_path, const Chunk& chunk, const StringTable& strings);
    
    // Get compilation statistics
    const CompileStats& get_stats() const { return stats_; }
    
private:
    std::vector<Token> tokens_;
    size_t current_;
    Chunk* chunk_;
    StringTable* strings_;
    
    std::unordered_map<std::string, InferredType> variable_types_;
    InferredType last_expression_type_;
    std::vector<std::string> current_local_params_; // names of params when compiling a function
    std::vector<std::string> current_local_locals_;  // names of local variables declared inside current function
    
    CompileStats stats_;
    
    // Parser state
    Token current_token();
    Token previous_token();
    bool advance();
    bool check(TokenType type);
    bool match(TokenType type);
    void consume(TokenType type, const char* message);
    
    // Compilation
    void expression();
    void expression_precedence(int min_precedence);
    void number();
    void string();
    void literal();
    void identifier();
    void grouping();
    void unary();
    void array_literal();
    void postfix_access();
    bool try_length_of_expression();
    bool try_sugar_statement();
    // NOTE: full operator precedence parsing handled by expression_precedence().
    // The old `binary()` placeholder was removed (dead code)
    void statement();
    void expression_statement();
    void assignment_statement();
    void print_statement();
    void if_statement();
    void while_statement();
         void for_statement();
    void return_statement();
    size_t emit_jump(uint8_t instruction);
    void patch_jump(size_t jump_position);
    void function_declaration();
    void table_declaration();
    void call_expression();
    
    // Helper methods
    int get_precedence(TokenType type);
    bool is_binary_operator(TokenType type);
    OpCode token_to_opcode(TokenType type);
    
    InferredType infer_literal_type(const Token& token);
    InferredType infer_variable_type(const std::string& name);
    void set_variable_type(const std::string& name, InferredType type);
    OpCode get_specialized_opcode(TokenType op, InferredType left_type, InferredType right_type);
    void emit_optimized_binary_op(TokenType op, InferredType left_type, InferredType right_type);
    
    // Bytecode emission
    void emit_byte(uint8_t byte);
    void emit_bytes(uint8_t byte1, uint8_t byte2);
    void emit_constant(const Value& value);
    void emit_return();

    void thread_jumps();
    void lower_stack_to_registers();
    
    // Error handling
    void error(const char* message);
    void error_at_current(const char* message);
    void error_at(const Token& token, const char* message);
    
    bool had_error_;
    bool panic_mode_;
};

} // namespace nightscript
} // namespace nightforge