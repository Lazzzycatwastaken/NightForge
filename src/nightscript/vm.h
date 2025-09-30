#pragma once
#include "value.h"
#include <functional>

namespace nightforge {
namespace nightscript {

// Host function signature
using HostFunction = std::function<Value(const std::vector<Value>&)>;

// VM execution result
enum class VMResult {
    OK,
    COMPILE_ERROR,
    RUNTIME_ERROR
};

class VM {
public:
    VM();
    ~VM();
    
    // Execute bytecode chunk
    VMResult execute(const Chunk& chunk);
    
    // Register host functions
    void register_host_function(const std::string& name, HostFunction func);
    
    // String management
    StringTable& strings() { return strings_; }
    
    // Global variables
    void set_global(const std::string& name, const Value& value);
    Value get_global(const std::string& name);
    
    // Debug
    void print_stack();
    
private:
    static constexpr size_t STACK_MAX = 1024; // as per spec
    
    Value stack_[STACK_MAX];
    Value* stack_top_;
    
    std::unordered_map<std::string, Value> globals_;
    std::unordered_map<std::string, HostFunction> host_functions_;
    StringTable strings_;
    
    // Stack operations
    void push(const Value& value);
    Value pop();
    Value peek(int distance = 0);
    void reset_stack();
    
    // Execution
    VMResult run(const Chunk& chunk);
    uint8_t read_byte(const uint8_t*& ip);
    Value read_constant(const Chunk& chunk, const uint8_t*& ip);
    
    // Binary operations
    bool binary_op(OpCode op);
    
    // Debug
    void runtime_error(const char* format, ...);
};

} // namespace nightscript
} // namespace nightforge