#include "vm.h"
#include "host_api.h"
#include <iostream>
#include <cstdarg>
#include <cstring>
#include <algorithm>
#include <cctype>
#include <charconv>
#include <chrono>

// the evil class of unredability

#ifdef DEBUG_TRACE_EXECUTION
#  undef DEBUG_TRACE_EXECUTION
#endif

namespace nightforge {
namespace nightscript {

VM::VM(HostEnvironment* host_env) {
    host_env_ = host_env;
    current_frame_ = nullptr;
    reset_stack();
    tmp_args_.reserve(16);
}

VM::~VM() {
    // cleanup if needed
}

VMResult VM::execute(const Chunk& chunk) {
    return run(chunk, nullptr);
}

VMResult VM::execute(const Chunk& chunk, const Chunk* parent_chunk) {
    return run(chunk, parent_chunk);
}

void VM::set_global(const std::string& name, const Value& value) {
    globals_[name] = value;
    uint32_t id = strings_.find_id(name);
    if (id != 0xFFFFFFFFu) globals_by_id_[id] = value;
}

Value VM::get_global(const std::string& name) {
    uint32_t id = strings_.find_id(name);
    if (id != 0xFFFFFFFFu) {
        auto it2 = globals_by_id_.find(id);
        if (it2 != globals_by_id_.end()) return it2->second;
    }
    auto it = globals_.find(name);
    if (it != globals_.end()) return it->second;
    return Value::nil();
}

void VM::print_stack() {
    std::cout << "Stack: ";
    for (Value* slot = stack_; slot < stack_top_; ++slot) {
        std::cout << "[";
        switch (slot->type()) {
            case ValueType::NIL: std::cout << "nil"; break;
            case ValueType::BOOL: std::cout << (slot->as_boolean() ? "true" : "false"); break;
            case ValueType::INT: std::cout << slot->as_integer(); break;
            case ValueType::FLOAT: std::cout << slot->as_floating(); break;
            case ValueType::STRING_ID: 
                std::cout << "\"" << strings_.get_string(slot->as_string_id()) << "\""; 
                break;
            default: std::cout << "unknown"; break;
        }
        std::cout << "]";
    }
    std::cout << std::endl;
}

void VM::push(const Value& value) {
    if (stack_top_ >= stack_ + STACK_MAX) {
        runtime_error("Stack overflow");
        has_runtime_error_ = true;
        return;
    }
    *stack_top_ = value;
    stack_top_++;
}

Value VM::pop() {
    if (stack_top_ <= stack_) {
        runtime_error("Stack underflow");
        has_runtime_error_ = true;
        return Value::nil();
    }
    stack_top_--;
    return *stack_top_;
}

Value VM::peek(int distance) {
    if (stack_top_ - 1 - distance < stack_) {
        runtime_error("Stack underflow in peek");
        has_runtime_error_ = true;
        return Value::nil();
    }
    return stack_top_[-1 - distance];
}

void VM::reset_stack() {
    stack_top_ = stack_;
    has_runtime_error_ = false;
}

VMResult VM::run(const Chunk& chunk, const Chunk* parent_chunk) {
    if (parent_chunk == nullptr) {
        reset_stack();
    }

    const uint8_t* ip = chunk.code().data();
    const uint8_t* end = ip + chunk.code().size();
    
    // Pre-declare variables that are used in computed goto blocks to avoid scope issues
    std::string func_name_lc;
    std::string func_name_lc2; // For tail call
    std::string func_name; // For host calls
    
#ifdef DEBUG_TRACE_EXECUTION
    std::cout << "== execution begin ==" << std::endl;
#endif
    
    // Computed-goto dispatch implementation (actual cancer to do jesus)
    // The dispatch table MUST exactly match the OpCode enum order.

    //I really should number these opcodes
    
    // macros
    #define DISPATCH() goto *dispatch_table[read_byte(ip)]
    #define SAFE_DISPATCH() do { if (ip >= end) return VMResult::OK; DISPATCH(); } while(0)
    #define COUNT_OPCODE(op) do { stats.op_counts[static_cast<uint8_t>(OpCode::op)]++; } while(0)

    static void* dispatch_table[] = {
        &&op_CONSTANT,        // OP_CONSTANT
        &&op_NIL,             // OP_NIL
        &&op_TRUE,            // OP_TRUE
        &&op_FALSE,           // OP_FALSE

        &&op_GET_GLOBAL,      // OP_GET_GLOBAL
        &&op_SET_GLOBAL,      // OP_SET_GLOBAL
        &&op_GET_LOCAL,       // OP_GET_LOCAL
        &&op_SET_LOCAL,       // OP_SET_LOCAL

        &&op_ADD,             // OP_ADD
        &&op_SUBTRACT,        // OP_SUBTRACT
        &&op_MULTIPLY,        // OP_MULTIPLY
        &&op_DIVIDE,          // OP_DIVIDE
        &&op_MODULO,          // OP_MODULO

        &&op_ADD_INT,         // OP_ADD_INT
        &&op_ADD_FLOAT,       // OP_ADD_FLOAT
        &&op_ADD_STRING,      // OP_ADD_STRING
        &&op_SUB_INT,         // OP_SUB_INT
        &&op_SUB_FLOAT,       // OP_SUB_FLOAT
        &&op_MUL_INT,         // OP_MUL_INT
        &&op_MUL_FLOAT,       // OP_MUL_FLOAT
        &&op_DIV_INT,         // OP_DIV_INT
        &&op_DIV_FLOAT,       // OP_DIV_FLOAT
        &&op_MOD_INT,         // OP_MOD_INT

        &&op_EQUAL,           // OP_EQUAL
        &&op_GREATER,         // OP_GREATER
        &&op_GREATER_EQUAL,   // OP_GREATER_EQUAL
        &&op_LESS_EQUAL,      // OP_LESS_EQUAL
        &&op_LESS,            // OP_LESS
        &&op_NOT,             // OP_NOT

        &&op_JUMP,            // OP_JUMP
        &&op_JUMP_IF_FALSE,   // OP_JUMP_IF_FALSE
        &&op_JUMP_BACK,       // OP_JUMP_BACK
        // OP_CALL removed - was unused
        &&op_CALL_HOST,       // OP_CALL_HOST
        &&op_TAIL_CALL,       // OP_TAIL_CALL
        &&op_RETURN,          // OP_RETURN

        &&op_POP,             // OP_POP
        &&op_PRINT,           // OP_PRINT
        &&op_PRINT_SPACE,     // OP_PRINT_SPACE

        &&op_ADD_LOCAL,       // OP_ADD_LOCAL
        &&op_ADD_FLOAT_LOCAL, // OP_ADD_FLOAT_LOCAL
        &&op_ADD_STRING_LOCAL,// OP_ADD_STRING_LOCAL

        &&op_CONSTANT_LOCAL,  // OP_CONSTANT_LOCAL
        &&op_ADD_LOCAL_CONST, // OP_ADD_LOCAL_CONST
        &&op_ADD_CONST_LOCAL, // OP_ADD_CONST_LOCAL
        &&op_ADD_LOCAL_CONST_FLOAT, // OP_ADD_LOCAL_CONST_FLOAT
        &&op_ADD_CONST_LOCAL_FLOAT, // OP_ADD_CONST_LOCAL_FLOAT
        // Arrays
        &&op_ARRAY_CREATE,    // OP_ARRAY_CREATE
        &&op_ARRAY_GET,       // OP_ARRAY_GET
        &&op_ARRAY_SET,       // OP_ARRAY_SET
        &&op_ARRAY_LENGTH,    // OP_ARRAY_LENGTH
        &&op_ARRAY_PUSH,      // OP_ARRAY_PUSH
        &&op_ARRAY_POP,       // OP_ARRAY_POP
        // Tables/Dictionaries
        &&op_TABLE_CREATE,    // OP_TABLE_CREATE
        &&op_TABLE_GET,       // OP_TABLE_GET
        &&op_TABLE_SET,       // OP_TABLE_SET
        &&op_TABLE_HAS,       // OP_TABLE_HAS
        &&op_TABLE_KEYS,      // OP_TABLE_KEYS
        &&op_TABLE_VALUES,    // OP_TABLE_VALUES
        &&op_TABLE_SIZE,      // OP_TABLE_SIZE
        &&op_TABLE_REMOVE,    // OP_TABLE_REMOVE
        // Generic indexing
        &&op_INDEX_GET,       // OP_INDEX_GET
        &&op_INDEX_SET,       // OP_INDEX_SET
    };

    constexpr size_t OPCODE_COUNT = static_cast<size_t>(OpCode::OP_INDEX_SET) + 1;
    static_assert(sizeof(dispatch_table) / sizeof(void*) == OPCODE_COUNT, "dispatch_table size must match OpCode count");

    if (ip >= end) return VMResult::OK;
    DISPATCH();

op_CONSTANT: {
    COUNT_OPCODE(OP_CONSTANT);
    Value constant = read_constant(chunk, ip);
    push(constant);
    SAFE_DISPATCH();
}

op_NIL: {
    COUNT_OPCODE(OP_NIL);
    push(Value::nil());
    SAFE_DISPATCH();
}

op_TRUE: {
    COUNT_OPCODE(OP_TRUE);
    push(Value::boolean(true));
    SAFE_DISPATCH();
}

op_FALSE: {
    COUNT_OPCODE(OP_FALSE);
    push(Value::boolean(false));
    SAFE_DISPATCH();
}

op_GET_GLOBAL: {
    COUNT_OPCODE(OP_GET_GLOBAL);
    Value variable_name = read_constant(chunk, ip);
    if (variable_name.type() != ValueType::STRING_ID) {
        runtime_error("Expected variable name");
        return VMResult::RUNTIME_ERROR;
    }
    uint32_t sid = variable_name.as_string_id();
    {
        auto it = globals_by_id_.find(sid);
        if (it != globals_by_id_.end()) {
            push(it->second);
        } else {
            std::string var_name = strings_.get_string(sid);
            push(get_global(var_name));
        }
    }
    SAFE_DISPATCH();
}

op_SET_GLOBAL: {
    COUNT_OPCODE(OP_SET_GLOBAL);
    Value variable_name = read_constant(chunk, ip);
    if (variable_name.type() != ValueType::STRING_ID) {
        runtime_error("Expected variable name");
        return VMResult::RUNTIME_ERROR;
    }
    uint32_t sid = variable_name.as_string_id();
    Value value = peek();
    {
        std::string var_name = strings_.get_string(sid);
        globals_by_id_[sid] = value;
        globals_[var_name] = value;
    }
    SAFE_DISPATCH();
}

op_GET_LOCAL: {
    COUNT_OPCODE(OP_GET_LOCAL);
    uint8_t slot = read_byte(ip);
    Value* local_ptr = get_local(slot);
    if (!local_ptr) {
        runtime_error("Local slot %d out of range", slot);
        return VMResult::RUNTIME_ERROR;
    }
    push(*local_ptr);
    SAFE_DISPATCH();
}

op_SET_LOCAL: {
    COUNT_OPCODE(OP_SET_LOCAL);
    uint8_t slot = read_byte(ip);
    Value* local_ptr = get_local(slot);
    if (!local_ptr) {
        runtime_error("Local slot %d out of range", slot);
        return VMResult::RUNTIME_ERROR;
    }
    *local_ptr = peek();  // Set local without popping
    SAFE_DISPATCH();
}

op_ADD: {
    COUNT_OPCODE(OP_ADD);
    if (!binary_op(OpCode::OP_ADD)) return VMResult::RUNTIME_ERROR;
    SAFE_DISPATCH();
}

op_SUBTRACT: {
    COUNT_OPCODE(OP_SUBTRACT);
    if (!binary_op(OpCode::OP_SUBTRACT)) return VMResult::RUNTIME_ERROR;
    SAFE_DISPATCH();
}

op_MULTIPLY: {
    COUNT_OPCODE(OP_MULTIPLY);
    if (!binary_op(OpCode::OP_MULTIPLY)) return VMResult::RUNTIME_ERROR;
    SAFE_DISPATCH();
}

op_DIVIDE: {
    COUNT_OPCODE(OP_DIVIDE);
    if (!binary_op(OpCode::OP_DIVIDE)) return VMResult::RUNTIME_ERROR;
    SAFE_DISPATCH();
}

op_MODULO: {
    COUNT_OPCODE(OP_MODULO);
    if (!binary_op(OpCode::OP_MODULO)) return VMResult::RUNTIME_ERROR;
    SAFE_DISPATCH();
}

op_ADD_INT: {
    COUNT_OPCODE(OP_ADD_INT);
    // Direct shot access (stack ahtually!! 🤓)
    int64_t a_val = stack_top_[-2].as_integer();
    int64_t b_val = stack_top_[-1].as_integer();
    stack_top_ -= 2;
    push(Value::integer(a_val + b_val));
    SAFE_DISPATCH();
}

op_ADD_FLOAT: {
    COUNT_OPCODE(OP_ADD_FLOAT);
    if (stack_top_ - stack_ >= 2) {
        double a_val = stack_top_[-2].as_floating();
        double b_val = stack_top_[-1].as_floating();
        stack_top_ -= 2;
        push(Value::floating(a_val + b_val));
    } else {
        Value b = pop(); Value a = pop();
        push(Value::floating(a.as_floating() + b.as_floating()));
    }
    SAFE_DISPATCH();
}

op_ADD_STRING: {
    COUNT_OPCODE(OP_ADD_STRING);
    Value b = pop(); Value a = pop();
    if (a.is_string_id() && b.is_string_id()) {
        uint32_t buf = buffers_.create_from_ids(a.as_string_id(), b.as_string_id(), strings_);
        push(Value::buffer_id(buf));
        bytes_allocated_since_gc_ += buffers_.get_buffer(buf).length();
        SAFE_DISPATCH();
    }
    if (a.type() == ValueType::STRING_BUFFER) {
        uint32_t buf_id = a.as_buffer_id();
        if (b.is_string_id()) buffers_.append_id(buf_id, b.as_string_id(), strings_);
        else buffers_.append_literal(buf_id, value_to_string(b));
        push(Value::buffer_id(buf_id));
        bytes_allocated_since_gc_ += 32;
        SAFE_DISPATCH();
    }
    {
        std::string str_a = value_to_string(a);
        std::string str_b = value_to_string(b);
        uint32_t buf = buffers_.create_from_two(str_a, str_b);
        buffers_.reserve(buf, str_a.length() + str_b.length() + 64);
        push(Value::buffer_id(buf));
        bytes_allocated_since_gc_ += buffers_.get_buffer(buf).length();
    }
    if (bytes_allocated_since_gc_ > GC_THRESHOLD) collect_garbage(&chunk);
    SAFE_DISPATCH();
}

op_SUB_INT: {
    COUNT_OPCODE(OP_SUB_INT);
    int64_t a_val = stack_top_[-2].as_integer();
    int64_t b_val = stack_top_[-1].as_integer();
    stack_top_ -= 2;
    push(Value::integer(a_val - b_val));
    SAFE_DISPATCH();
}

op_SUB_FLOAT: {
    COUNT_OPCODE(OP_SUB_FLOAT);
    if (stack_top_ - stack_ >= 2) {
        double a_val = stack_top_[-2].as_floating();
        double b_val = stack_top_[-1].as_floating();
        stack_top_ -= 2;
        push(Value::floating(a_val - b_val));
    } else {
        Value b = pop(); Value a = pop(); push(Value::floating(a.as_floating() - b.as_floating()));
    }
    SAFE_DISPATCH();
}

op_MUL_INT: {
    COUNT_OPCODE(OP_MUL_INT);
    int64_t a_val = stack_top_[-2].as_integer();
    int64_t b_val = stack_top_[-1].as_integer();
    stack_top_ -= 2;
    push(Value::integer(a_val * b_val));
    SAFE_DISPATCH();
}

op_MUL_FLOAT: {
    COUNT_OPCODE(OP_MUL_FLOAT);
    if (stack_top_ - stack_ >= 2) {
        double a_val = stack_top_[-2].as_floating();
        double b_val = stack_top_[-1].as_floating();
        stack_top_ -= 2;
        push(Value::floating(a_val * b_val));
    } else {
        Value b = pop(); Value a = pop(); push(Value::floating(a.as_floating() * b.as_floating()));
    }
    SAFE_DISPATCH();
}

op_DIV_INT: {
    COUNT_OPCODE(OP_DIV_INT);
    int64_t a_val = stack_top_[-2].as_integer();
    int64_t b_val = stack_top_[-1].as_integer();
    stack_top_ -= 2;
    if (b_val == 0) { runtime_error("Division by zero"); return VMResult::RUNTIME_ERROR; }
    push(Value::integer(a_val / b_val));
    SAFE_DISPATCH();
}

op_DIV_FLOAT: {
    COUNT_OPCODE(OP_DIV_FLOAT);
    if (stack_top_ - stack_ >= 2) {
        double a_val = stack_top_[-2].as_floating();
        double b_val = stack_top_[-1].as_floating();
        stack_top_ -= 2;
        if (b_val == 0.0) { runtime_error("Division by zero"); return VMResult::RUNTIME_ERROR; }
        push(Value::floating(a_val / b_val));
    } else {
        Value b = pop(); Value a = pop(); if (b.as_floating() == 0.0) { runtime_error("Division by zero"); return VMResult::RUNTIME_ERROR; } push(Value::floating(a.as_floating() / b.as_floating()));
    }
    SAFE_DISPATCH();
}

op_MOD_INT: {
    COUNT_OPCODE(OP_MOD_INT);
    int64_t a_val = stack_top_[-2].as_integer();
    int64_t b_val = stack_top_[-1].as_integer();
    stack_top_ -= 2;
    if (b_val == 0) { runtime_error("Modulo by zero"); return VMResult::RUNTIME_ERROR; }
    push(Value::integer(a_val % b_val));
    SAFE_DISPATCH();
}

op_EQUAL: {
    COUNT_OPCODE(OP_EQUAL);
    Value b = pop(); Value a = pop(); bool equal = false; if (a.type() == b.type()) { switch (a.type()) { case ValueType::NIL: equal = true; break; case ValueType::BOOL: equal = a.as_boolean() == b.as_boolean(); break; case ValueType::INT: equal = a.as_integer() == b.as_integer(); break; case ValueType::FLOAT: equal = a.as_floating() == b.as_floating(); break; case ValueType::STRING_ID: equal = a.as_string_id() == b.as_string_id(); break; default: equal = false; break; } } push(Value::boolean(equal)); SAFE_DISPATCH();
}

op_GREATER: {
    COUNT_OPCODE(OP_GREATER);
    Value b = pop(); Value a = pop(); bool result = false; if (a.type() == b.type()) { switch (a.type()) { case ValueType::INT: result = a.as_integer() > b.as_integer(); break; case ValueType::FLOAT: result = a.as_floating() > b.as_floating(); break; default: result = false; break; } } push(Value::boolean(result)); SAFE_DISPATCH();
}

op_GREATER_EQUAL: {
    COUNT_OPCODE(OP_GREATER_EQUAL);
    Value b = pop(); Value a = pop(); bool result = false; if (a.type() == b.type()) { switch (a.type()) { case ValueType::INT: result = a.as_integer() >= b.as_integer(); break; case ValueType::FLOAT: result = a.as_floating() >= b.as_floating(); break; default: result = false; break; } } push(Value::boolean(result)); SAFE_DISPATCH();
}

op_LESS_EQUAL: {
    COUNT_OPCODE(OP_LESS_EQUAL);
    Value b = pop(); Value a = pop(); bool result = false; if (a.type() == b.type()) { switch (a.type()) { case ValueType::INT: result = a.as_integer() <= b.as_integer(); break; case ValueType::FLOAT: result = a.as_floating() <= b.as_floating(); break; default: result = false; break; } } push(Value::boolean(result)); SAFE_DISPATCH();
}

op_LESS: {
    COUNT_OPCODE(OP_LESS);
    Value b = pop(); Value a = pop(); bool result = false; if (a.type() == b.type()) { switch (a.type()) { case ValueType::INT: result = a.as_integer() < b.as_integer(); break; case ValueType::FLOAT: result = a.as_floating() < b.as_floating(); break; default: result = false; break; } } push(Value::boolean(result)); SAFE_DISPATCH();
}

op_NOT: {
    COUNT_OPCODE(OP_NOT);
    Value value = pop(); bool is_falsy = (value.type() == ValueType::NIL) || (value.type() == ValueType::BOOL && !value.as_boolean()); push(Value::boolean(is_falsy)); SAFE_DISPATCH();
}

op_JUMP: {
    COUNT_OPCODE(OP_JUMP);
    uint8_t offset = read_byte(ip); ip += offset; SAFE_DISPATCH();
}

op_JUMP_IF_FALSE: {
    COUNT_OPCODE(OP_JUMP_IF_FALSE);
    Value cond = pop(); bool is_false = (cond.type() == ValueType::NIL) || (cond.type() == ValueType::BOOL && !cond.as_boolean()); uint8_t offset = read_byte(ip); if (is_false) ip += offset; SAFE_DISPATCH();
}

op_JUMP_BACK: {
    COUNT_OPCODE(OP_JUMP_BACK);
    uint8_t offset = read_byte(ip); ip -= offset; SAFE_DISPATCH();
}

op_CALL_HOST: {
    COUNT_OPCODE(OP_CALL_HOST);
    
    Value function_name = read_constant(chunk, ip);
    uint8_t arg_count = read_byte(ip);
    
    if (function_name.type() != ValueType::STRING_ID) {
        runtime_error("Expected function name");
        return VMResult::RUNTIME_ERROR;
    }
    
    uint32_t fname_sid = function_name.as_string_id();
    
    // Collect arguments from stack
    tmp_args_.clear();
    if (arg_count > 0) {
        tmp_args_.resize(arg_count);
        for (int i = arg_count - 1; i >= 0; --i) {
            tmp_args_[i] = pop();
            if (i == 0) break;
        }
    }
    
    // Convert function name to lowercase once
    func_name = strings_.get_string(fname_sid);
    func_name_lc = func_name;
    std::transform(func_name_lc.begin(), func_name_lc.end(), func_name_lc.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    
    // Try host function first (most common case for engine calls)
    if (host_env_) {
        std::optional<Value> host_result = host_env_->call_host(func_name_lc, tmp_args_);
        if (host_result.has_value()) {
            push(*host_result);
            SAFE_DISPATCH();
        }
    }
    
    // Try user-defined function in current chunk
    ssize_t func_index = chunk.get_function_index(func_name_lc);
    if (func_index >= 0) {
        const Chunk& fchunk = chunk.get_function(static_cast<size_t>(func_index));
        
        // Push arguments onto stack for function
        for (const Value& arg : tmp_args_) {
            push(arg);
        }
        
        Value* stack_before_call = stack_top_ - arg_count;
        
        push_call_frame(&fchunk, arg_count);
        VMResult r = execute(fchunk, &chunk);
        pop_call_frame();
        
        if (r == VMResult::OK) {
            // Handle return value
            if (stack_top_ > stack_before_call + 1) {
                Value return_value = stack_top_[-1];
                stack_top_ = stack_before_call;
                push(return_value);
            } else if (stack_top_ <= stack_before_call) {
                stack_top_ = stack_before_call;
                push(Value::nil());
            }
        }
        
        if (r != VMResult::OK) return r;
        SAFE_DISPATCH();
    }
    
    // Try parent chunk (for nested function calls)
    if (parent_chunk) {
        ssize_t parent_func_index = parent_chunk->get_function_index(func_name_lc);
        if (parent_func_index >= 0) {
            const Chunk& fchunk = parent_chunk->get_function(static_cast<size_t>(parent_func_index));
            
            // Push arguments onto stack
            for (const Value& arg : tmp_args_) {
                push(arg);
            }
            
            Value* stack_before_call = stack_top_ - arg_count;
            size_t initial_call_frames = call_frames_.size();
            
            push_call_frame(&fchunk, arg_count);
            VMResult r = execute(fchunk, parent_chunk);
            pop_call_frame();
            
            // Verify call frame integrity
            if (call_frames_.size() != initial_call_frames) {
                runtime_error("Call frame stack imbalance");
                return VMResult::RUNTIME_ERROR;
            }
            
            if (r == VMResult::OK) {
                // Handle return value
                if (stack_top_ > stack_before_call + 1) {
                    Value return_value = stack_top_[-1];
                    stack_top_ = stack_before_call;
                    push(return_value);
                } else if (stack_top_ <= stack_before_call) {
                    stack_top_ = stack_before_call;
                    push(Value::nil());
                }
            }
            
            if (r != VMResult::OK) return r;
            SAFE_DISPATCH();
        }
    }
    
    // Function not found anywhere
    runtime_error("Unknown function: %s", func_name_lc.c_str());
    
    #ifdef DEBUG_FUNCTION_CALLS
    std::cerr << "Available functions in chunk:\n";
    for (size_t i = 0; i < chunk.function_count(); ++i) {
        std::cerr << "  - " << chunk.function_name(i) << "\n";
    }
    if (parent_chunk) {
        std::cerr << "Available functions in parent chunk:\n";
        for (size_t i = 0; i < parent_chunk->function_count(); ++i) {
            std::cerr << "  - " << parent_chunk->function_name(i) << "\n";
        }
    }
    #endif
    
    return VMResult::RUNTIME_ERROR;
}

op_TAIL_CALL: {
    COUNT_OPCODE(OP_TAIL_CALL);

    Value function_name = read_constant(chunk, ip);
    uint8_t arg_count = read_byte(ip);

    if (function_name.type() != ValueType::STRING_ID) {
        runtime_error("Expected function name");
        return VMResult::RUNTIME_ERROR;
    }

    uint32_t fname_sid = function_name.as_string_id();

    tmp_args_.clear();
    if (arg_count > 0) {
        tmp_args_.resize(arg_count);
        for (int i = arg_count - 1; i >= 0; --i) {
            tmp_args_[i] = pop();
            if (i == 0) break;
        }
    }

    {
        std::string fn = strings_.get_string(fname_sid);
        func_name_lc2 = fn;
        std::transform(func_name_lc2.begin(), func_name_lc2.end(), func_name_lc2.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    }

    ssize_t func_index = chunk.get_function_index(func_name_lc2);
    if (func_index >= 0) {
        const Chunk& fchunk = chunk.get_function(static_cast<size_t>(func_index));

        for (const Value& arg : tmp_args_) {
            push(arg);
        }
        
        // Store stack state before call for proper cleanup
        Value* stack_before_call = stack_top_ - arg_count;
        
        push_call_frame(&fchunk, arg_count);
        VMResult r = execute(fchunk, &chunk);
        pop_call_frame();
        
        if (r == VMResult::OK) {
            if (stack_top_ > stack_before_call + 1) {
                Value return_value = stack_top_[-1];
                stack_top_ = stack_before_call;
                push(return_value);
            } else if (stack_top_ <= stack_before_call) {
                stack_top_ = stack_before_call;
                push(Value::nil());
            } 
        }
        
        if (r != VMResult::OK) return r;
        SAFE_DISPATCH();
    }

    std::optional<Value> host_result;
    if (host_env_) {
        host_result = host_env_->call_host(func_name_lc2, tmp_args_);
    }
    if (host_result.has_value()) {
        push(*host_result);
        SAFE_DISPATCH();
    }

    runtime_error("Unknown function in tail call: %s", strings_.get_string(fname_sid).c_str());
    return VMResult::RUNTIME_ERROR;
}

op_RETURN: {
    COUNT_OPCODE(OP_RETURN);
    return VMResult::OK;
}

op_POP: {
    COUNT_OPCODE(OP_POP);
    pop(); SAFE_DISPATCH();
}

op_PRINT: {
    COUNT_OPCODE(OP_PRINT);
    Value value = pop();
    if (value.type() == ValueType::STRING_BUFFER) {
        std::cout << buffers_.get_buffer(value.as_buffer_id());
    } else if (value.type() == ValueType::ARRAY) {
        uint32_t id = value.as_array_id();
        size_t len = arrays_.length(id);
        for (size_t i = 0; i < len; ++i) {
            Value elem = arrays_.get(id, static_cast<ssize_t>(i));
            if (elem.type() == ValueType::STRING_BUFFER) {
                std::cout << buffers_.get_buffer(elem.as_buffer_id());
            } else if (elem.type() == ValueType::STRING_ID) {
                std::cout << strings_.get_string(elem.as_string_id());
            } else if (elem.type() == ValueType::NIL) {
                std::cout << "nil";
            } else if (elem.type() == ValueType::BOOL) {
                std::cout << (elem.as_boolean() ? "true" : "false");
            } else if (elem.type() == ValueType::INT) {
                std::cout << elem.as_integer();
            } else if (elem.type() == ValueType::FLOAT) {
                std::cout << elem.as_floating();
            } else if (elem.type() == ValueType::ARRAY) {
                std::cout << "[... ]"; // avoid deep recursion
            } else {
                std::cout << "unknown";
            }
            if (i + 1 < len) std::cout << ' ';
        }
    } else {
        switch (value.type()) { case ValueType::NIL: std::cout << "nil"; break; case ValueType::BOOL: std::cout << (value.as_boolean() ? "true" : "false"); break; case ValueType::INT: std::cout << value.as_integer(); break; case ValueType::FLOAT: std::cout << value.as_floating(); break; case ValueType::STRING_ID: std::cout << strings_.get_string(value.as_string_id()); break; default: std::cout << "unknown"; break; }
    }
    std::cout << std::endl; SAFE_DISPATCH();
}

op_PRINT_SPACE: {
    COUNT_OPCODE(OP_PRINT_SPACE);
    Value value = pop();
    if (value.type() == ValueType::STRING_BUFFER) {
        std::cout << buffers_.get_buffer(value.as_buffer_id());
    } else if (value.type() == ValueType::ARRAY) {
        uint32_t id = value.as_array_id();
        size_t len = arrays_.length(id);
        for (size_t i = 0; i < len; ++i) {
            Value elem = arrays_.get(id, static_cast<ssize_t>(i));
            if (elem.type() == ValueType::STRING_BUFFER) {
                std::cout << buffers_.get_buffer(elem.as_buffer_id());
            } else if (elem.type() == ValueType::STRING_ID) {
                std::cout << strings_.get_string(elem.as_string_id());
            } else if (elem.type() == ValueType::NIL) {
                std::cout << "nil";
            } else if (elem.type() == ValueType::BOOL) {
                std::cout << (elem.as_boolean() ? "true" : "false");
            } else if (elem.type() == ValueType::INT) {
                std::cout << elem.as_integer();
            } else if (elem.type() == ValueType::FLOAT) {
                std::cout << elem.as_floating();
            } else if (elem.type() == ValueType::ARRAY) {
                std::cout << "[... ]";
            } else {
                std::cout << "unknown";
            }
            if (i + 1 < len) std::cout << ' ';
        }
    } else {
        switch (value.type()) { case ValueType::NIL: std::cout << "nil"; break; case ValueType::BOOL: std::cout << (value.as_boolean() ? "true" : "false"); break; case ValueType::INT: std::cout << value.as_integer(); break; case ValueType::FLOAT: std::cout << value.as_floating(); break; case ValueType::STRING_ID: std::cout << strings_.get_string(value.as_string_id()); break; default: std::cout << "unknown"; break; }
    }
    std::cout << " "; SAFE_DISPATCH();
}

op_ADD_LOCAL: {
    COUNT_OPCODE(OP_ADD_LOCAL);
    uint8_t slot_a = read_byte(ip);
    uint8_t slot_b = read_byte(ip);
    Value* local_a = get_local(slot_a);
    Value* local_b = get_local(slot_b);
    if (!local_a || !local_b) {
        runtime_error("Local slot out of range for OP_ADD_LOCAL");
        return VMResult::RUNTIME_ERROR;
    }
    const Value& a = *local_a;
    const Value& b = *local_b;
    if (a.type() == ValueType::INT && b.type() == ValueType::INT) {
        push(Value::integer(a.as_integer() + b.as_integer()));
    } else {
        double da = (a.type() == ValueType::FLOAT) ? a.as_floating() : static_cast<double>(a.as_integer());
        double db = (b.type() == ValueType::FLOAT) ? b.as_floating() : static_cast<double>(b.as_integer());
        push(Value::floating(da + db));
    }
    SAFE_DISPATCH();
}

op_ADD_FLOAT_LOCAL: {
    COUNT_OPCODE(OP_ADD_FLOAT_LOCAL);
    uint8_t slot_a = read_byte(ip);
    uint8_t slot_b = read_byte(ip);
    Value* local_a = get_local(slot_a);
    Value* local_b = get_local(slot_b);
    if (!local_a || !local_b) {
        runtime_error("Local slot out of range for OP_ADD_FLOAT_LOCAL");
        return VMResult::RUNTIME_ERROR;
    }
    const Value& va = *local_a;
    const Value& vb = *local_b;
    double da = (va.type() == ValueType::FLOAT) ? va.as_floating() : static_cast<double>(va.as_integer());
    double db = (vb.type() == ValueType::FLOAT) ? vb.as_floating() : static_cast<double>(vb.as_integer());
    push(Value::floating(da + db));
    SAFE_DISPATCH();
}

op_ADD_STRING_LOCAL: {
    COUNT_OPCODE(OP_ADD_STRING_LOCAL);
    uint8_t slot_a = read_byte(ip);
    uint8_t slot_b = read_byte(ip);
    Value* local_a = get_local(slot_a);
    Value* local_b = get_local(slot_b);
    if (!local_a || !local_b) {
        runtime_error("Local slot out of range for OP_ADD_STRING_LOCAL");
        return VMResult::RUNTIME_ERROR;
    }
    {
        std::string sa = value_to_string(*local_a);
        std::string sb = value_to_string(*local_b);
        uint32_t buf = buffers_.create_from_two(sa, sb);
        push(Value::buffer_id(buf));
        bytes_allocated_since_gc_ += buffers_.get_buffer(buf).length();
    }
    SAFE_DISPATCH();
}

op_CONSTANT_LOCAL: {
    COUNT_OPCODE(OP_CONSTANT_LOCAL);
    Value vc = read_constant(chunk, ip);
    uint8_t slot = read_byte(ip);
    Value* local_ptr = get_local(slot);
    if (!local_ptr) {
        runtime_error("Local slot %d out of range for CONSTANT_LOCAL", slot);
        return VMResult::RUNTIME_ERROR;
    }
    *local_ptr = vc;
    SAFE_DISPATCH();
}

op_ADD_LOCAL_CONST: {
    COUNT_OPCODE(OP_ADD_LOCAL_CONST);
    uint8_t slot = read_byte(ip);
    Value vc = read_constant(chunk, ip);
    Value* local_ptr = get_local(slot);
    if (!local_ptr) {
        runtime_error("Local slot out of range for OP_ADD_LOCAL_CONST");
        return VMResult::RUNTIME_ERROR;
    }
    Value va = *local_ptr;
    if (va.type() == ValueType::INT && vc.type() == ValueType::INT) {
        push(Value::integer(va.as_integer() + vc.as_integer()));
    } else if (va.type() == ValueType::FLOAT || vc.type() == ValueType::FLOAT) {
        double da = (va.type() == ValueType::FLOAT) ? va.as_floating() : static_cast<double>(va.as_integer());
        double dc = (vc.type() == ValueType::FLOAT) ? vc.as_floating() : static_cast<double>(vc.as_integer());
        push(Value::floating(da + dc));
    } else if (va.type() == ValueType::STRING_ID || vc.type() == ValueType::STRING_ID || va.type() == ValueType::STRING_BUFFER || vc.type() == ValueType::STRING_BUFFER) {
        {
            std::string sa = value_to_string(va);
            std::string sc = value_to_string(vc);
            uint32_t buf = buffers_.create_from_two(sa, sc);
            push(Value::buffer_id(buf));
            bytes_allocated_since_gc_ += buffers_.get_buffer(buf).length();
        }
    } else {
        runtime_error("ADD_LOCAL_CONST unsupported types");
        return VMResult::RUNTIME_ERROR;
    }
    SAFE_DISPATCH();
}

op_ADD_CONST_LOCAL: {
    COUNT_OPCODE(OP_ADD_CONST_LOCAL);
    Value vc = read_constant(chunk, ip);
    uint8_t slot = read_byte(ip);
    Value* local_ptr = get_local(slot);
    if (!local_ptr) {
        runtime_error("Local slot out of range for OP_ADD_CONST_LOCAL");
        return VMResult::RUNTIME_ERROR;
    }
    Value va = *local_ptr;
    if (va.type() == ValueType::INT && vc.type() == ValueType::INT) {
        push(Value::integer(vc.as_integer() + va.as_integer()));
    } else if (va.type() == ValueType::FLOAT || vc.type() == ValueType::FLOAT) {
        double da = (va.type() == ValueType::FLOAT) ? va.as_floating() : static_cast<double>(va.as_integer());
        double dc = (vc.type() == ValueType::FLOAT) ? vc.as_floating() : static_cast<double>(vc.as_integer());
        push(Value::floating(dc + da));
    } else if (va.type() == ValueType::STRING_ID || vc.type() == ValueType::STRING_ID || va.type() == ValueType::STRING_BUFFER || vc.type() == ValueType::STRING_BUFFER) {
        {
            std::string sc = value_to_string(vc);
            std::string sa = value_to_string(va);
            uint32_t buf = buffers_.create_from_two(sc, sa);
            push(Value::buffer_id(buf));
            bytes_allocated_since_gc_ += buffers_.get_buffer(buf).length();
        }
    } else {
        runtime_error("ADD_CONST_LOCAL unsupported types");
        return VMResult::RUNTIME_ERROR;
    }
    SAFE_DISPATCH();
}

op_ADD_LOCAL_CONST_FLOAT: {
    COUNT_OPCODE(OP_ADD_LOCAL_CONST_FLOAT);
    uint8_t slot = read_byte(ip);
    Value vc = read_constant(chunk, ip);
    Value* local_ptr = get_local(slot);
    if (!local_ptr) {
        runtime_error("Local slot out of range for OP_ADD_LOCAL_CONST_FLOAT");
        return VMResult::RUNTIME_ERROR;
    }
    Value va = *local_ptr;
    double da = (va.type() == ValueType::FLOAT) ? va.as_floating() : static_cast<double>(va.as_integer());
    double dc = (vc.type() == ValueType::FLOAT) ? vc.as_floating() : static_cast<double>(vc.as_integer());
    push(Value::floating(da + dc));
    SAFE_DISPATCH();
}

op_ADD_CONST_LOCAL_FLOAT: {
    COUNT_OPCODE(OP_ADD_CONST_LOCAL_FLOAT);
    Value vc = read_constant(chunk, ip);
    uint8_t slot = read_byte(ip);
    Value* local_ptr = get_local(slot);
    if (!local_ptr) {
        runtime_error("Local slot out of range for OP_ADD_CONST_LOCAL_FLOAT");
        return VMResult::RUNTIME_ERROR;
    }
    Value va = *local_ptr;
    double da = (va.type() == ValueType::FLOAT) ? va.as_floating() : static_cast<double>(va.as_integer());
    double dc = (vc.type() == ValueType::FLOAT) ? vc.as_floating() : static_cast<double>(vc.as_integer());
    push(Value::floating(dc + da));
    SAFE_DISPATCH();
}

op_ARRAY_CREATE: {
    COUNT_OPCODE(OP_ARRAY_CREATE);
    // Next byte is element count (uint8)
    uint8_t count = read_byte(ip);
    uint32_t id = arrays_.create(count);
    if (count > 0) {
        Value* tmp = static_cast<Value*>(alloca(sizeof(Value) * count));
        for (uint8_t i = 0; i < count; ++i) {
            tmp[i] = pop();
        }
        for (int i = static_cast<int>(count) - 1; i >= 0; --i) {
            arrays_.push_back(id, tmp[i]);
        }
    }
    push(Value::array_id(id));
    SAFE_DISPATCH();
}

op_ARRAY_GET: {
    COUNT_OPCODE(OP_ARRAY_GET);
    Value idxv = pop();
    Value arrv = pop();
    if (arrv.type() != ValueType::ARRAY) { runtime_error("INDEX: not an array"); return VMResult::RUNTIME_ERROR; }
    ssize_t index = 0;
    if (idxv.type() == ValueType::INT) index = static_cast<ssize_t>(idxv.as_integer());
    else if (idxv.type() == ValueType::FLOAT) index = static_cast<ssize_t>(idxv.as_floating());
    else { runtime_error("INDEX: index must be number"); return VMResult::RUNTIME_ERROR; }
    Value val = arrays_.get(arrv.as_array_id(), index);
    push(val);
    SAFE_DISPATCH();
}

op_ARRAY_SET: {
    COUNT_OPCODE(OP_ARRAY_SET);
    Value value = pop();
    Value idxv = pop();
    Value arrv = pop();
    if (arrv.type() != ValueType::ARRAY) { runtime_error("SETINDEX: not an array"); return VMResult::RUNTIME_ERROR; }
    ssize_t index = 0;
    if (idxv.type() == ValueType::INT) index = static_cast<ssize_t>(idxv.as_integer());
    else if (idxv.type() == ValueType::FLOAT) index = static_cast<ssize_t>(idxv.as_floating());
    else { runtime_error("SETINDEX: index must be number"); return VMResult::RUNTIME_ERROR; }
    arrays_.set(arrv.as_array_id(), index, value);
    push(value); // leave value on stack
    SAFE_DISPATCH();
}

op_ARRAY_LENGTH: {
    COUNT_OPCODE(OP_ARRAY_LENGTH);
    Value arrv = pop();
    if (arrv.type() != ValueType::ARRAY) { runtime_error("length: not an array"); return VMResult::RUNTIME_ERROR; }
    size_t len = arrays_.length(arrv.as_array_id());
    push(Value::integer(static_cast<int64_t>(len)));
    SAFE_DISPATCH();
}

op_ARRAY_PUSH: {
    COUNT_OPCODE(OP_ARRAY_PUSH);
    Value value = pop();
    Value arrv = pop();
    if (arrv.type() != ValueType::ARRAY) { runtime_error("push: not an array"); return VMResult::RUNTIME_ERROR; }
    arrays_.push_back(arrv.as_array_id(), value);
    push(arrv); // return the array for chaining
    SAFE_DISPATCH();
}

op_ARRAY_POP: {
    COUNT_OPCODE(OP_ARRAY_POP);
    Value arrv = pop();
    if (arrv.type() != ValueType::ARRAY) { runtime_error("pop: not an array"); return VMResult::RUNTIME_ERROR; }
    Value v = arrays_.pop_back(arrv.as_array_id());
    push(v);
    SAFE_DISPATCH();
}

op_TABLE_CREATE: {
    COUNT_OPCODE(OP_TABLE_CREATE);
    uint32_t id = tables_.create();
    push(Value::table_id(id));
    SAFE_DISPATCH();
}

op_TABLE_GET: {
    COUNT_OPCODE(OP_TABLE_GET);
    Value keyv = pop();
    Value tablev = pop();
    if (tablev.type() != ValueType::TABLE_ID) { runtime_error("TABLE_GET: not a table"); return VMResult::RUNTIME_ERROR; }
    if (keyv.type() != ValueType::STRING_ID) { runtime_error("TABLE_GET: key must be string"); return VMResult::RUNTIME_ERROR; }
    Value val = tables_.get(tablev.as_table_id(), keyv.as_string_id(), strings_);
    push(val);
    SAFE_DISPATCH();
}

op_TABLE_SET: {
    COUNT_OPCODE(OP_TABLE_SET);
    Value value = pop();
    Value keyv = pop();
    Value tablev = pop();
    if (tablev.type() != ValueType::TABLE_ID) { runtime_error("TABLE_SET: not a table"); return VMResult::RUNTIME_ERROR; }
    if (keyv.type() != ValueType::STRING_ID) { runtime_error("TABLE_SET: key must be string"); return VMResult::RUNTIME_ERROR; }
    tables_.set(tablev.as_table_id(), keyv.as_string_id(), value, strings_);
    push(tablev); // leave table on stack for chaining
    SAFE_DISPATCH();
}

op_TABLE_HAS: {
    COUNT_OPCODE(OP_TABLE_HAS);
    Value keyv = pop();
    Value tablev = pop();
    if (tablev.type() != ValueType::TABLE_ID) { runtime_error("TABLE_HAS: not a table"); return VMResult::RUNTIME_ERROR; }
    if (keyv.type() != ValueType::STRING_ID) { runtime_error("TABLE_HAS: key must be string"); return VMResult::RUNTIME_ERROR; }
    bool has = tables_.has_key(tablev.as_table_id(), keyv.as_string_id(), strings_);
    push(Value::boolean(has));
    SAFE_DISPATCH();
}

op_TABLE_SIZE: {
    COUNT_OPCODE(OP_TABLE_SIZE);
    Value tablev = pop();
    if (tablev.type() != ValueType::TABLE_ID) { runtime_error("TABLE_SIZE: not a table"); return VMResult::RUNTIME_ERROR; }
    size_t size = tables_.size(tablev.as_table_id());
    push(Value::integer(static_cast<int64_t>(size)));
    SAFE_DISPATCH();
}

op_TABLE_KEYS: {
    COUNT_OPCODE(OP_TABLE_KEYS);
    Value tablev = pop();
    if (tablev.type() != ValueType::TABLE_ID) { runtime_error("TABLE_KEYS: not a table"); return VMResult::RUNTIME_ERROR; }
    
    auto* keys_ptr_heap = new std::vector<std::string>(tables_.get_keys(tablev.as_table_id()));
    size_t keys_count = keys_ptr_heap->size();
    
    uint32_t arr_id = arrays_.create(keys_count);
    for (size_t i = 0; i < keys_count; ++i) {
        uint32_t str_id = strings_.intern((*keys_ptr_heap)[i]);
        arrays_.push_back(arr_id, Value::string_id(str_id));
    }
    delete keys_ptr_heap;
    
    push(Value::array_id(arr_id));
    SAFE_DISPATCH();
}

op_TABLE_VALUES: {
    COUNT_OPCODE(OP_TABLE_VALUES);
    Value tablev = pop();
    if (tablev.type() != ValueType::TABLE_ID) { runtime_error("TABLE_VALUES: not a table"); return VMResult::RUNTIME_ERROR; }
    
    auto* values_ptr_heap = new std::vector<Value>(tables_.get_values(tablev.as_table_id()));
    size_t values_count = values_ptr_heap->size();
    
    uint32_t arr_id = arrays_.create(values_count);
    for (size_t i = 0; i < values_count; ++i) {
        arrays_.push_back(arr_id, (*values_ptr_heap)[i]);
    }
    delete values_ptr_heap;
    
    push(Value::array_id(arr_id));
    SAFE_DISPATCH();
}

op_TABLE_REMOVE: {
    COUNT_OPCODE(OP_TABLE_REMOVE);
    Value keyv = pop();
    Value tablev = pop();
    if (tablev.type() != ValueType::TABLE_ID) { runtime_error("TABLE_REMOVE: not a table"); return VMResult::RUNTIME_ERROR; }
    if (keyv.type() != ValueType::STRING_ID) { runtime_error("TABLE_REMOVE: key must be string"); return VMResult::RUNTIME_ERROR; }
    bool removed = tables_.remove_key(tablev.as_table_id(), keyv.as_string_id(), strings_);
    push(Value::boolean(removed));
    SAFE_DISPATCH();
}

op_INDEX_GET: {
    COUNT_OPCODE(OP_INDEX_GET);
    Value keyv = pop();
    Value objv = pop();
    
    if (objv.type() == ValueType::ARRAY) {
        // Array indexing
        ssize_t index = 0;
        if (keyv.type() == ValueType::INT) index = static_cast<ssize_t>(keyv.as_integer());
        else if (keyv.type() == ValueType::FLOAT) index = static_cast<ssize_t>(keyv.as_floating());
        else { runtime_error("INDEX_GET: array index must be number"); return VMResult::RUNTIME_ERROR; }
        Value val = arrays_.get(objv.as_array_id(), index);
        push(val);
    } else if (objv.type() == ValueType::TABLE_ID) {
        // Table indexing
        if (keyv.type() != ValueType::STRING_ID) { runtime_error("INDEX_GET: table key must be string"); return VMResult::RUNTIME_ERROR; }
        Value val = tables_.get(objv.as_table_id(), keyv.as_string_id(), strings_);
        push(val);
    } else {
        runtime_error("INDEX_GET: can only index arrays and tables");
        return VMResult::RUNTIME_ERROR;
    }
    SAFE_DISPATCH();
}

op_INDEX_SET: {
    COUNT_OPCODE(OP_INDEX_SET);
    Value value = pop();
    Value keyv = pop();
    Value objv = pop();
    
    if (objv.type() == ValueType::ARRAY) {
        // Array indexing
        ssize_t index = 0;
        if (keyv.type() == ValueType::INT) index = static_cast<ssize_t>(keyv.as_integer());
        else if (keyv.type() == ValueType::FLOAT) index = static_cast<ssize_t>(keyv.as_floating());
        else { runtime_error("INDEX_SET: array index must be number"); return VMResult::RUNTIME_ERROR; }
        arrays_.set(objv.as_array_id(), index, value);
        push(value); // leave value on stack
    } else if (objv.type() == ValueType::TABLE_ID) {
        // Table indexing
        if (keyv.type() != ValueType::STRING_ID) { runtime_error("INDEX_SET: table key must be string"); return VMResult::RUNTIME_ERROR; }
        tables_.set(objv.as_table_id(), keyv.as_string_id(), value, strings_);
        push(value); // leave value on stack
    } else {
        runtime_error("INDEX_SET: can only index arrays and tables");
        return VMResult::RUNTIME_ERROR;
    }
    SAFE_DISPATCH();
}
    return VMResult::OK;
}

uint8_t VM::read_byte(const uint8_t*& ip) {
    return *ip++;
}

Value VM::read_constant(const Chunk& chunk, const uint8_t*& ip) {
    uint8_t index = read_byte(ip);
    return chunk.get_constant(index);
}

void VM::push_call_frame(const Chunk* chunk, uint8_t arg_count) {
    CallFrame frame;
    frame.base = stack_top_ - arg_count;
    frame.top = stack_top_;
    frame.return_ip = nullptr;
    frame.chunk = chunk;
    
    call_frames_.push_back(frame);
    current_frame_ = &call_frames_.back();
    
    // Reserve space for local variables (parameters become locals 0, 1, 2, ...)
}

void VM::pop_call_frame() {
    if (!call_frames_.empty()) {
        call_frames_.pop_back();
        if (call_frames_.empty()) {
            current_frame_ = nullptr;
        } else {
            current_frame_ = &call_frames_.back();
        }
    }
}

Value* VM::get_local(uint8_t slot) {
    if (!current_frame_) return nullptr;
    Value* local_ptr = current_frame_->base + slot;
    
    if (local_ptr < stack_ || local_ptr >= stack_ + STACK_MAX) {
        return nullptr;  // Out of VM stack bounds
    }
    return local_ptr;
}

bool VM::binary_op(OpCode op) {
    // Fast path: read top-of-stack values directly to avoid two pop() calls
    Value a, b;
    if (stack_top_ - stack_ >= 2) {
        a = stack_top_[-2];
        b = stack_top_[-1];
        stack_top_ -= 2;
    } else {
        b = pop();
        a = pop();
    }
    
    // Handle string concatenation for addition
    if (op == OpCode::OP_ADD && (a.type() == ValueType::STRING_ID || b.type() == ValueType::STRING_ID)) {
        std::string str_a, str_b;
        
        // Convert a to string
        if (a.type() == ValueType::STRING_ID) {
            str_a = strings_.get_string(a.as_string_id());
        } else if (a.type() == ValueType::INT) {
            char buf[32];
            auto res = std::to_chars(buf, buf + sizeof(buf), a.as_integer());
            str_a.assign(buf, res.ptr);
        } else if (a.type() == ValueType::FLOAT) {
            char buf[64];
            int n = snprintf(buf, sizeof(buf), "%g", a.as_floating());
            if (n > 0) str_a.assign(buf, static_cast<size_t>(n));
        } else if (a.type() == ValueType::BOOL) {
            str_a = a.as_boolean() ? "true" : "false";
        } else if (a.type() == ValueType::NIL) {
            str_a = "nil";
        } else {
            str_a = "unknown";
        }
        
        // Convert b to string
        if (b.type() == ValueType::STRING_ID) {
            str_b = strings_.get_string(b.as_string_id());
        } else if (b.type() == ValueType::INT) {
            char buf[32];
            auto res = std::to_chars(buf, buf + sizeof(buf), b.as_integer());
            str_b.assign(buf, res.ptr);
        } else if (b.type() == ValueType::FLOAT) {
            char buf[64];
            int n = snprintf(buf, sizeof(buf), "%g", b.as_floating());
            if (n > 0) str_b.assign(buf, static_cast<size_t>(n));
        } else if (b.type() == ValueType::BOOL) {
            str_b = b.as_boolean() ? "true" : "false";
        } else if (b.type() == ValueType::NIL) {
            str_b = "nil";
        } else {
            str_b = "unknown";
        }
        
                    uint32_t buf = buffers_.create_from_two(str_a, str_b);
                    push(Value::buffer_id(buf));
                    bytes_allocated_since_gc_ += buffers_.get_buffer(buf).length();
        return true;
    }
    
    // Type checking (keep it simple for now)
    if (a.type() == ValueType::INT && b.type() == ValueType::INT) {
        int64_t result;
        switch (op) {
            case OpCode::OP_ADD: result = a.as_integer() + b.as_integer(); break;
            case OpCode::OP_SUBTRACT: result = a.as_integer() - b.as_integer(); break;
            case OpCode::OP_MULTIPLY: result = a.as_integer() * b.as_integer(); break;
            case OpCode::OP_DIVIDE: 
                if (b.as_integer() == 0) {
                    runtime_error("Don't divide by zero.");
                    return false;
                }
                result = a.as_integer() / b.as_integer(); 
                break;
            case OpCode::OP_MODULO:
                if (b.as_integer() == 0) {
                    runtime_error("Don't modulo by zero.");
                    return false;
                }
                result = a.as_integer() % b.as_integer(); 
                break;
            default:
                runtime_error("Unknown binary operator");
                return false;
        }
        push(Value::integer(result));
        return true;
    }
    
    if (a.type() == ValueType::FLOAT || b.type() == ValueType::FLOAT) {
        double da = (a.type() == ValueType::FLOAT) ? a.as_floating() : static_cast<double>(a.as_integer());
        double db = (b.type() == ValueType::FLOAT) ? b.as_floating() : static_cast<double>(b.as_integer());
        
        double result;
        switch (op) {
            case OpCode::OP_ADD: result = da + db; break;
            case OpCode::OP_SUBTRACT: result = da - db; break;
            case OpCode::OP_MULTIPLY: result = da * db; break;
            case OpCode::OP_DIVIDE: 
                if (db == 0.0) {
                    runtime_error("Don't divide by zero.");
                    return false;
                }
                result = da / db; 
                break;
            default:
                runtime_error("Unknown binary operator");
                return false;
        }
        push(Value::floating(result));
        return true;
    }
    
    runtime_error("Operands must be numbers");
    return false;
}

void VM::runtime_error(const char* format, ...) {
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    fputs("\n", stderr);
    
    reset_stack(); // reset on error
}

void VM::collect_garbage(const Chunk* active_chunk) {
    auto start_time = std::chrono::high_resolution_clock::now();
    
    // Clear all GC marks
    strings_.clear_gc_marks();
    buffers_.clear_gc_marks();
    
    // Mark all reachable strings
    mark_reachable_strings(active_chunk);
    
    // Sweep unreachable strings
    size_t old_memory = strings_.memory_usage();
    strings_.sweep_unreachable_strings();
    size_t new_memory = strings_.memory_usage();

    // Sweep buffers
    size_t old_buf_mem = buffers_.memory_usage();
    buffers_.sweep_unreachable_buffers();
    size_t new_buf_mem = buffers_.memory_usage();
    
    // Update stats
    stats.gc_collections++;
    stats.bytes_freed += (old_memory - new_memory);
    stats.bytes_freed += (old_buf_mem - new_buf_mem);
    bytes_allocated_since_gc_ = 0;
    
    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
    stats.total_gc_time += duration.count() / 1000000.0;
    
#ifdef DEBUG_GC
    std::cout << "GC: freed " << (old_memory - new_memory) << " bytes in " 
              << duration.count() << "μs" << std::endl;
#endif
}

void VM::mark_reachable_strings(const Chunk* active_chunk) {
    // Mark strings on the VM stack
    for (Value* slot = stack_; slot < stack_top_; ++slot) {
        if (slot->type() == ValueType::STRING_ID) {
            strings_.mark_string_reachable(slot->as_string_id());
        } else if (slot->type() == ValueType::STRING_BUFFER) {
            buffers_.mark_buffer_reachable(slot->as_buffer_id());
        } else if (slot->type() == ValueType::ARRAY) {
            arrays_.mark_array_reachable(slot->as_array_id());
            arrays_.for_each(slot->as_array_id(), [this](const Value& v){
                if (v.type() == ValueType::STRING_ID) strings_.mark_string_reachable(v.as_string_id());
                else if (v.type() == ValueType::STRING_BUFFER) buffers_.mark_buffer_reachable(v.as_buffer_id());
                else if (v.type() == ValueType::ARRAY) arrays_.mark_array_reachable(v.as_array_id());
            });
        }
    }

    // Mark globals
    for (const auto& pair : globals_) {
        if (pair.second.type() == ValueType::STRING_ID) {
            strings_.mark_string_reachable(pair.second.as_string_id());
        } else if (pair.second.type() == ValueType::STRING_BUFFER) {
            buffers_.mark_buffer_reachable(pair.second.as_buffer_id());
        } else if (pair.second.type() == ValueType::ARRAY) {
            arrays_.mark_array_reachable(pair.second.as_array_id());
            arrays_.for_each(pair.second.as_array_id(), [this](const Value& v){
                if (v.type() == ValueType::STRING_ID) strings_.mark_string_reachable(v.as_string_id());
                else if (v.type() == ValueType::STRING_BUFFER) buffers_.mark_buffer_reachable(v.as_buffer_id());
                else if (v.type() == ValueType::ARRAY) arrays_.mark_array_reachable(v.as_array_id());
            });
        }
    }

    // Also mark any strings stored in the active chunk's constants (function names, string literals)
    if (active_chunk) {
        for (const auto& constant : active_chunk->constants()) {
            if (constant.type() == ValueType::STRING_ID) {
                strings_.mark_string_reachable(constant.as_string_id());
            }
        }
        for (size_t i = 0; i < active_chunk->function_count(); ++i) {
            const Chunk& f = active_chunk->get_function(i);
            for (const auto& c : f.constants()) {
                if (c.type() == ValueType::STRING_ID) strings_.mark_string_reachable(c.as_string_id());
            }
        }
    }
}

std::string VM::value_to_string(const Value& val) {
    switch (val.type()) {
        case ValueType::STRING_ID: 
            return strings_.get_string(val.as_string_id());
        case ValueType::INT: {
            char buf[32];
            auto res = std::to_chars(buf, buf + sizeof(buf), val.as_integer());
            return std::string(buf, res.ptr);
        }
        case ValueType::FLOAT: {
            char buf[64];
            snprintf(buf, sizeof(buf), "%g", val.as_floating());
            return buf;
        }
        case ValueType::BOOL: 
            return val.as_boolean() ? "true" : "false";
        case ValueType::NIL: 
            return "nil";
        case ValueType::STRING_BUFFER:
            return buffers_.get_buffer(val.as_buffer_id());
        default: 
            return "unknown";
    }
}

} // namespace nightscript
} // namespace nightforge