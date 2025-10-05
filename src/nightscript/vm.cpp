#include "vm.h"
#include "host_api.h"
#include <iostream>
#include <cstdarg>
#include <cstring>
#include <algorithm>
#include <cctype>
#include <charconv>
#include <chrono>

#ifdef DEBUG_TRACE_EXECUTION
#  undef DEBUG_TRACE_EXECUTION
#endif

namespace nightforge {
namespace nightscript {

VM::VM(HostEnvironment* host_env) {
    host_env_ = host_env;
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

// Note: host functions are provided by HostEnvironment VM no longer stores them internally

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
    has_runtime_error_ = false;  // Reset error state when resetting stack
}

VMResult VM::run(const Chunk& chunk, const Chunk* parent_chunk) {
    if (parent_chunk == nullptr) {
        reset_stack();
    }

    const uint8_t* ip = chunk.code().data();
    const uint8_t* end = ip + chunk.code().size();
    
#ifdef DEBUG_TRACE_EXECUTION
    std::cout << "== execution begin ==" << std::endl;
#endif
    
    while (ip < end) {
        // Check for runtime errors before processing next instruction
        if (has_runtime_error_) {
            return VMResult::RUNTIME_ERROR;
        }
        
    const uint8_t* ip_before = ip;
    uint8_t instruction = read_byte(ip);
    stats.op_counts[instruction]++;
        
#ifdef DEBUG_TRACE_EXECUTION
        print_stack();
        std::cout << "Instruction: " << static_cast<int>(instruction) << std::endl;
#endif
        
        switch (static_cast<OpCode>(instruction)) {
            case OpCode::OP_CONSTANT: {
                Value constant = read_constant(chunk, ip);
                push(constant);
                break;
            }
            
            case OpCode::OP_NIL:
                push(Value::nil());
                break;
                
            case OpCode::OP_TRUE:
                push(Value::boolean(true));
                break;
                
            case OpCode::OP_FALSE:
                push(Value::boolean(false));
                break;
                
            case OpCode::OP_ADD:
            case OpCode::OP_SUBTRACT:
            case OpCode::OP_MULTIPLY:
            case OpCode::OP_DIVIDE:
            case OpCode::OP_MODULO:
                if (!binary_op(static_cast<OpCode>(instruction))) {
                    return VMResult::RUNTIME_ERROR;
                }
                break;
                
            //fast arithmetic operations
            case OpCode::OP_ADD_INT: {
                Value b = pop();
                Value a = pop();
                push(Value::integer(a.as_integer() + b.as_integer()));
                break;
            }
            
            case OpCode::OP_ADD_FLOAT: {
                Value b = pop();
                Value a = pop();
                push(Value::floating(a.as_floating() + b.as_floating()));
                break;
            }
            
            case OpCode::OP_ADD_STRING: {
                Value b = pop();
                Value a = pop();
                
                // Fast path: both are string IDs - create buffer directly
                if (a.is_string_id() && b.is_string_id()) {
                    uint32_t buf = buffers_.create_from_ids(a.as_string_id(), b.as_string_id(), strings_);
                    push(Value::buffer_id(buf));
                    bytes_allocated_since_gc_ += buffers_.get_buffer(buf).length();
                    break;
                }
                
                // One is a buffer - reuse it (massive performance boost for chained concat)
                if (a.type() == ValueType::STRING_BUFFER) {
                    uint32_t buf_id = a.as_buffer_id();
                    if (b.is_string_id()) {
                        buffers_.append_id(buf_id, b.as_string_id(), strings_);
                    } else {
                        buffers_.append_literal(buf_id, value_to_string(b));
                    }
                    push(Value::buffer_id(buf_id));
                    bytes_allocated_since_gc_ += 32; // Approximate allocation for append
                    break;
                }
                
                std::string str_a = value_to_string(a);
                std::string str_b = value_to_string(b);
                uint32_t buf = buffers_.create_from_two(str_a, str_b);
                buffers_.reserve(buf, str_a.length() + str_b.length() + 64);
                push(Value::buffer_id(buf));
                bytes_allocated_since_gc_ += buffers_.get_buffer(buf).length();

                if (bytes_allocated_since_gc_ > GC_THRESHOLD) {
                    collect_garbage(&chunk);
                }
                break;
            }
            
            case OpCode::OP_SUB_INT: {
                Value b = pop();
                Value a = pop();
                push(Value::integer(a.as_integer() - b.as_integer()));
                break;
            }
            
            case OpCode::OP_SUB_FLOAT: {
                Value b = pop();
                Value a = pop();
                push(Value::floating(a.as_floating() - b.as_floating()));
                break;
            }
            
            case OpCode::OP_MUL_INT: {
                Value b = pop();
                Value a = pop();
                push(Value::integer(a.as_integer() * b.as_integer()));
                break;
            }
            
            case OpCode::OP_MUL_FLOAT: {
                Value b = pop();
                Value a = pop();
                push(Value::floating(a.as_floating() * b.as_floating()));
                break;
            }
            
            case OpCode::OP_DIV_INT: {
                Value b = pop();
                Value a = pop();
                if (b.as_integer() == 0) {
                    runtime_error("Division by zero");
                    return VMResult::RUNTIME_ERROR;
                }
                push(Value::integer(a.as_integer() / b.as_integer()));
                break;
            }
            
            case OpCode::OP_DIV_FLOAT: {
                Value b = pop();
                Value a = pop();
                if (b.as_floating() == 0.0) {
                    runtime_error("Division by zero");
                    return VMResult::RUNTIME_ERROR;
                }
                push(Value::floating(a.as_floating() / b.as_floating()));
                break;
            }
            
            case OpCode::OP_MOD_INT: {
                Value b = pop();
                Value a = pop();
                if (b.as_integer() == 0) {
                    runtime_error("Modulo by zero");
                    return VMResult::RUNTIME_ERROR;
                }
                push(Value::integer(a.as_integer() % b.as_integer()));
                break;
            }
                
            case OpCode::OP_NOT: {
                Value value = pop();
                bool is_falsy = (value.type() == ValueType::NIL) || 
                               (value.type() == ValueType::BOOL && !value.as_boolean());
                push(Value::boolean(is_falsy));
                break;
            }

            case OpCode::OP_JUMP_IF_FALSE: {
                Value cond = pop();
                bool is_false = (cond.type() == ValueType::NIL) || (cond.type() == ValueType::BOOL && !cond.as_boolean());
                uint8_t offset = read_byte(ip);
                if (is_false) {
                    ip += offset;
                }
                break;
            }

            case OpCode::OP_JUMP: {
                uint8_t offset = read_byte(ip);
                ip += offset;
                break;
            }

            case OpCode::OP_JUMP_BACK: {
                uint8_t offset = read_byte(ip);
                ip -= offset;
                break;
            }
            
            case OpCode::OP_EQUAL: {
                Value b = pop();
                Value a = pop();
                // Simple equality (could be better)
                bool equal = false;
                if (a.type() == b.type()) {
                    switch (a.type()) {
                        case ValueType::NIL: equal = true; break;
                        case ValueType::BOOL: equal = a.as_boolean() == b.as_boolean(); break;
                        case ValueType::INT: equal = a.as_integer() == b.as_integer(); break;
                        case ValueType::FLOAT: equal = a.as_floating() == b.as_floating(); break;
                        case ValueType::STRING_ID: equal = a.as_string_id() == b.as_string_id(); break;
                        default: equal = false; break;
                    }
                }
                push(Value::boolean(equal));
                break;
            }

            case OpCode::OP_GREATER: {
                Value b = pop();
                Value a = pop();
                bool result = false;
                if (a.type() == b.type()) {
                    switch (a.type()) {
                        case ValueType::INT: result = a.as_integer() > b.as_integer(); break;
                        case ValueType::FLOAT: result = a.as_floating() > b.as_floating(); break;
                        default: result = false; break;
                    }
                }
                push(Value::boolean(result));
                break;
            }

            case OpCode::OP_GREATER_EQUAL: {
                Value b = pop();
                Value a = pop();
                bool result = false;
                if (a.type() == b.type()) {
                    switch (a.type()) {
                        case ValueType::INT: result = a.as_integer() >= b.as_integer(); break;
                        case ValueType::FLOAT: result = a.as_floating() >= b.as_floating(); break;
                        default: result = false; break;
                    }
                }
                push(Value::boolean(result));
                break;
            }

            case OpCode::OP_LESS_EQUAL: {
                Value b = pop();
                Value a = pop();
                bool result = false;
                if (a.type() == b.type()) {
                    switch (a.type()) {
                        case ValueType::INT: result = a.as_integer() <= b.as_integer(); break;
                        case ValueType::FLOAT: result = a.as_floating() <= b.as_floating(); break;
                        default: result = false; break;
                    }
                }
                push(Value::boolean(result));
                break;
            }

            case OpCode::OP_LESS: {
                Value b = pop();
                Value a = pop();
                bool result = false;
                if (a.type() == b.type()) {
                    switch (a.type()) {
                        case ValueType::INT: result = a.as_integer() < b.as_integer(); break;
                        case ValueType::FLOAT: result = a.as_floating() < b.as_floating(); break;
                        default: result = false; break;
                    }
                }
                push(Value::boolean(result));
                break;
            }
            
            case OpCode::OP_PRINT: {
                Value value = pop();
                if (value.type() == ValueType::STRING_BUFFER) {
                    std::cout << buffers_.get_buffer(value.as_buffer_id());
                } else {
                switch (value.type()) {
                    case ValueType::NIL: std::cout << "nil"; break;
                    case ValueType::BOOL: std::cout << (value.as_boolean() ? "true" : "false"); break;
                    case ValueType::INT: std::cout << value.as_integer(); break;
                    case ValueType::FLOAT: std::cout << value.as_floating(); break;
                    case ValueType::STRING_ID: 
                        std::cout << strings_.get_string(value.as_string_id()); 
                        break;
                    default: std::cout << "unknown"; break;
                }
                }
                std::cout << std::endl;
                break;
            }
            
            case OpCode::OP_PRINT_SPACE: {
                Value value = pop();
                if (value.type() == ValueType::STRING_BUFFER) {
                    std::cout << buffers_.get_buffer(value.as_buffer_id());
                } else {
                switch (value.type()) {
                    case ValueType::NIL: std::cout << "nil"; break;
                    case ValueType::BOOL: std::cout << (value.as_boolean() ? "true" : "false"); break;
                    case ValueType::INT: std::cout << value.as_integer(); break;
                    case ValueType::FLOAT: std::cout << value.as_floating(); break;
                    case ValueType::STRING_ID: 
                        std::cout << strings_.get_string(value.as_string_id()); 
                        break;
                    default: std::cout << "unknown"; break;
                }
                }
                std::cout << " ";  // space instead of newline
                break;
            }

            case OpCode::OP_CONSTANT_R: {
                uint8_t dest = read_byte(ip);
                uint8_t idx = read_byte(ip);
                Value constant = chunk.get_constant(idx);
                if (dest < REG_COUNT) registers_[dest] = constant;
                break;
            }

            case OpCode::OP_GET_LOCAL_R: {
                uint8_t dest = read_byte(ip);
                uint8_t idx = read_byte(ip);
                if (local_frame_bases_.empty()) {
                    runtime_error("No local frame for GET_LOCAL_R");
                    return VMResult::RUNTIME_ERROR;
                }
                size_t base = local_frame_bases_.back();
                size_t abs = base + static_cast<size_t>(idx);
                if (abs >= param_stack_.size()) {
                    runtime_error("Local index out of range for GET_LOCAL_R");
                    return VMResult::RUNTIME_ERROR;
                }
                if (dest < REG_COUNT) registers_[dest] = param_stack_[abs];
                break;
            }

            case OpCode::OP_PUSH_REG: {
                uint8_t reg = read_byte(ip);
                if (reg >= REG_COUNT) {
                    runtime_error("Invalid register in PUSH_REG");
                    return VMResult::RUNTIME_ERROR;
                }
                push(registers_[reg]);
                break;
            }

            case OpCode::OP_ADD_INT_R: {
                uint8_t dest = read_byte(ip);
                uint8_t s1 = read_byte(ip);
                uint8_t s2 = read_byte(ip);
                int64_t a = registers_[s1].as_integer();
                int64_t b = registers_[s2].as_integer();
                if (dest < REG_COUNT) registers_[dest] = Value::integer(a + b);
                break;
            }

            case OpCode::OP_ADD_FLOAT_R: {
                uint8_t dest = read_byte(ip);
                uint8_t s1 = read_byte(ip);
                uint8_t s2 = read_byte(ip);
                double a = registers_[s1].as_floating();
                double b = registers_[s2].as_floating();
                if (dest < REG_COUNT) registers_[dest] = Value::floating(a + b);
                break;
            }

            case OpCode::OP_ADD_STRING_R: {
                uint8_t dest = read_byte(ip);
                uint8_t s1 = read_byte(ip);
                uint8_t s2 = read_byte(ip);
                // Build buffer from two register values
                std::string sa = value_to_string(registers_[s1]);
                std::string sb = value_to_string(registers_[s2]);
                uint32_t buf = buffers_.create_from_two(sa, sb);
                if (dest < REG_COUNT) registers_[dest] = Value::buffer_id(buf);
                bytes_allocated_since_gc_ += buffers_.get_buffer(buf).length();
                break;
            }

            case OpCode::OP_ADD_LOCAL: {
                uint8_t idx_a = read_byte(ip);
                uint8_t idx_b = read_byte(ip);
                if (local_frame_bases_.empty()) {
                    runtime_error("No local frame for OP_ADD_LOCAL");
                    return VMResult::RUNTIME_ERROR;
                }
                size_t base = local_frame_bases_.back();
                size_t abs_a = base + static_cast<size_t>(idx_a);
                size_t abs_b = base + static_cast<size_t>(idx_b);
                if (abs_a >= param_stack_.size() || abs_b >= param_stack_.size()) {
                    runtime_error("Local index out of range for OP_ADD_LOCAL");
                    return VMResult::RUNTIME_ERROR;
                }
                Value a = param_stack_[abs_a];
                Value b = param_stack_[abs_b];
                if (a.type() == ValueType::INT && b.type() == ValueType::INT) {
                    push(Value::integer(a.as_integer() + b.as_integer()));
                } else {
                    // Fallback to numeric addition semantics
                    double da = (a.type() == ValueType::FLOAT) ? a.as_floating() : static_cast<double>(a.as_integer());
                    double db = (b.type() == ValueType::FLOAT) ? b.as_floating() : static_cast<double>(b.as_integer());
                    push(Value::floating(da + db));
                }
                break;
            }

            case OpCode::OP_ADD_FLOAT_LOCAL: {
                uint8_t idx_a = read_byte(ip);
                uint8_t idx_b = read_byte(ip);
                if (local_frame_bases_.empty()) {
                    runtime_error("No local frame for OP_ADD_FLOAT_LOCAL");
                    return VMResult::RUNTIME_ERROR;
                }
                size_t base = local_frame_bases_.back();
                size_t abs_a = base + static_cast<size_t>(idx_a);
                size_t abs_b = base + static_cast<size_t>(idx_b);
                if (abs_a >= param_stack_.size() || abs_b >= param_stack_.size()) {
                    runtime_error("Local index out of range for OP_ADD_FLOAT_LOCAL");
                    return VMResult::RUNTIME_ERROR;
                }
                double da = (param_stack_[abs_a].type() == ValueType::FLOAT) ? param_stack_[abs_a].as_floating() : static_cast<double>(param_stack_[abs_a].as_integer());
                double db = (param_stack_[abs_b].type() == ValueType::FLOAT) ? param_stack_[abs_b].as_floating() : static_cast<double>(param_stack_[abs_b].as_integer());
                push(Value::floating(da + db));
                break;
            }

            case OpCode::OP_ADD_STRING_LOCAL: {
                uint8_t idx_a = read_byte(ip);
                uint8_t idx_b = read_byte(ip);
                if (local_frame_bases_.empty()) {
                    runtime_error("No local frame for OP_ADD_STRING_LOCAL");
                    return VMResult::RUNTIME_ERROR;
                }
                size_t base = local_frame_bases_.back();
                size_t abs_a = base + static_cast<size_t>(idx_a);
                size_t abs_b = base + static_cast<size_t>(idx_b);
                if (abs_a >= param_stack_.size() || abs_b >= param_stack_.size()) {
                    runtime_error("Local index out of range for OP_ADD_STRING_LOCAL");
                    return VMResult::RUNTIME_ERROR;
                }
                std::string sa = value_to_string(param_stack_[abs_a]);
                std::string sb = value_to_string(param_stack_[abs_b]);
                uint32_t buf = buffers_.create_from_two(sa, sb);
                push(Value::buffer_id(buf));
                bytes_allocated_since_gc_ += buffers_.get_buffer(buf).length();
                break;
            }
            
            case OpCode::OP_CALL_HOST: {
                Value function_name = read_constant(chunk, ip);
                uint8_t arg_count = read_byte(ip);

                if (function_name.type() != ValueType::STRING_ID) {
                    runtime_error("Expected function name");
                    return VMResult::RUNTIME_ERROR;
                }

                std::string func_name = strings_.get_string(function_name.as_string_id());
                // Convert to lowercase for case-insensitive lookup
                std::string func_name_lc = func_name;
                std::transform(func_name_lc.begin(), func_name_lc.end(), func_name_lc.begin(), [](unsigned char c){ return std::tolower(c); });

                tmp_args_.clear();
                if (arg_count > 0) {
                    tmp_args_.resize(arg_count);
                    for (int i = arg_count - 1; i >= 0; --i) {
                        tmp_args_[i] = pop();
                        if (i == 0) break;
                    }
                }

                if (host_env_) {
                    auto result = host_env_->call_host(func_name_lc, tmp_args_);
                    if (result.has_value()) {
                        push(*result);
                        break;
                    }
                }

                // If not a host function, check for user-defined function in the chunk
                ssize_t func_index = chunk.get_function_index(func_name_lc);
                if (func_index >= 0) {
                    const Chunk& fchunk = chunk.get_function(static_cast<size_t>(func_index));

                    // Use param stack/local frames instead of globals
                    const std::vector<std::string>& locals_combined = chunk.get_function_local_names(static_cast<size_t>(func_index));
                    push_local_frame(locals_combined, tmp_args_);

                    // Save current stack state
                    Value* saved_stack_top = stack_top_;

                    VMResult r = execute(fchunk, &chunk);

                    pop_local_frame();

                    if (r != VMResult::OK) return r;

                    if (stack_top_ <= stack_) {
                        push(Value::nil());
                    }

                    Value return_value = pop();
                    if (return_value.type() == ValueType::STRING_BUFFER) {
                        uint32_t sid = strings_.intern(buffers_.get_buffer(return_value.as_buffer_id()));
                        return_value = Value::string_id(sid);
                    }
                    stack_top_ = saved_stack_top;
                    push(return_value);
                    break;
                }

                if (parent_chunk) {
                    ssize_t parent_func_index = parent_chunk->get_function_index(func_name_lc);
                    if (parent_func_index >= 0) {
                        const Chunk& fchunk = parent_chunk->get_function(static_cast<size_t>(parent_func_index));

                        // Set function parameters as globals  
                        const std::vector<std::string>& locals_combined = parent_chunk->get_function_local_names(static_cast<size_t>(parent_func_index));
                        push_local_frame(locals_combined, tmp_args_);

                        // Save current stack state
                        Value* saved_stack_top = stack_top_;

                        // Execute function chunk pass parent chunk as parent for recursive lookups
                        VMResult r = execute(fchunk, parent_chunk);

                        pop_local_frame();

                        if (r != VMResult::OK) return r;

                        if (stack_top_ <= stack_) {
                            push(Value::nil());
                        }

                        Value return_value = pop();
                        if (return_value.type() == ValueType::STRING_BUFFER) {
                            uint32_t sid = strings_.intern(buffers_.get_buffer(return_value.as_buffer_id()));
                            return_value = Value::string_id(sid);
                        }
                        stack_top_ = saved_stack_top;
                        push(return_value);
                        break;
                    }
                }

                runtime_error("Unknown function: %s", func_name.c_str());
                // Debug
                std::cerr << "Available functions in chunk:\n";
                for (size_t i = 0; i < chunk.function_count(); ++i) {
                    std::cerr << " - " << chunk.function_name(i) << "\n";
                }
                return VMResult::RUNTIME_ERROR;
                break;
            }
            
            case OpCode::OP_TAIL_CALL: {
                // Optimized tail call (reuses current stack frame)
                Value function_name = read_constant(chunk, ip);
                uint8_t arg_count = read_byte(ip);
                
                if (function_name.type() != ValueType::STRING_ID) {
                    runtime_error("Expected function name");
                    return VMResult::RUNTIME_ERROR;
                }
                
                std::string func_name = strings_.get_string(function_name.as_string_id());
                std::string func_name_lc = func_name;
                std::transform(func_name_lc.begin(), func_name_lc.end(), func_name_lc.begin(), [](unsigned char c){ return std::tolower(c); });
                
                tmp_args_.clear();
                if (arg_count > 0) {
                    tmp_args_.resize(arg_count);
                    for (int i = arg_count - 1; i >= 0; --i) {
                        tmp_args_[i] = pop();
                        if (i == 0) break;
                    }
                }

                ssize_t func_index = chunk.get_function_index(func_name_lc);
                if (func_index >= 0) {
                    const std::vector<std::string>& locals_combined = chunk.get_function_local_names(static_cast<size_t>(func_index));

                    if (!local_frames_.empty()) pop_local_frame();
                    push_local_frame(locals_combined, tmp_args_);

                    ip = chunk.code().data();
                    continue; // Restart execution from the beginning
                }
                
                if (host_env_) {
                    auto result = host_env_->call_host(func_name_lc, tmp_args_);
                    if (result.has_value()) {
                        push(*result);
                        break;
                    }
                }

                runtime_error("Unknown function in tail call: %s", func_name.c_str());
                return VMResult::RUNTIME_ERROR;
                break;
            }
            
            case OpCode::OP_GET_GLOBAL: {
                Value variable_name = read_constant(chunk, ip);
                if (variable_name.type() != ValueType::STRING_ID) {
                    runtime_error("Expected variable name");
                    return VMResult::RUNTIME_ERROR;
                }

                uint32_t sid = variable_name.as_string_id();
                std::string var_name = strings_.get_string(sid);
                Value local_val;
                if (local_lookup(var_name, local_val)) {
                    push(local_val);
                } else {
                    auto it = globals_by_id_.find(sid);
                    if (it != globals_by_id_.end()) {
                        push(it->second);
                    } else {
                        Value value = get_global(var_name);
                        push(value);
                    }
                }
                break;
            }

            case OpCode::OP_GET_LOCAL: {
                uint8_t idx = read_byte(ip);
                if (local_frame_bases_.empty()) {
                    runtime_error("No local frame for GET_LOCAL");
                    return VMResult::RUNTIME_ERROR;
                }
                size_t base = local_frame_bases_.back();
                size_t abs = base + static_cast<size_t>(idx);
                if (abs >= param_stack_.size()) {
                    runtime_error("Local index out of range");
                    return VMResult::RUNTIME_ERROR;
                }
                push(param_stack_[abs]);
                break;
            }
            
            case OpCode::OP_SET_GLOBAL: {
                Value variable_name = read_constant(chunk, ip);
                if (variable_name.type() != ValueType::STRING_ID) {
                    runtime_error("Expected variable name");
                    return VMResult::RUNTIME_ERROR;
                }

                uint32_t sid = variable_name.as_string_id();
                std::string var_name = strings_.get_string(sid);
                Value value = peek(); // Don't pop - assignment is an expression
                if (!local_frames_.empty()) {
                    const auto& frame = local_frames_.back();
                    auto it = frame.find(var_name);
                    if (it != frame.end()) {
                        size_t idx = it->second;
                        if (idx < param_stack_.size()) param_stack_[idx] = value;
                        break;
                    }
                }
                globals_by_id_[sid] = value;
                globals_[var_name] = value;
                break;
            }

            case OpCode::OP_SET_LOCAL: {
                uint8_t idx = read_byte(ip);
                if (local_frame_bases_.empty()) {
                    runtime_error("No local frame for SET_LOCAL");
                    return VMResult::RUNTIME_ERROR;
                }
                size_t base = local_frame_bases_.back();
                size_t abs = base + static_cast<size_t>(idx);
                Value value = peek(); // assignment is an expression
                if (abs >= param_stack_.size()) {
                    runtime_error("Local index out of range for SET_LOCAL");
                    return VMResult::RUNTIME_ERROR;
                }
                param_stack_[abs] = value;
                break;
            }
            
            case OpCode::OP_POP:
                pop();
                break;
                
            case OpCode::OP_RETURN:
                // For now just end execution
                return VMResult::OK;
                
            default:
                runtime_error("Unknown opcode: %d", instruction);
                return VMResult::RUNTIME_ERROR;
        }
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

void VM::push_local_frame(const std::vector<std::string>& locals_combined, const std::vector<Value>& args) {
    size_t base = param_stack_.size();
    local_frame_bases_.push_back(base);

    std::unordered_map<std::string, size_t> frame_map;
    for (size_t i = 0; i < locals_combined.size(); ++i) {
        Value v = (i < args.size()) ? args[i] : Value::nil();
        param_stack_.push_back(v);
        frame_map[locals_combined[i]] = base + i;
    }
    local_frames_.push_back(std::move(frame_map));
}

void VM::pop_local_frame() {
    if (local_frame_bases_.empty()) return;
    size_t base = local_frame_bases_.back();
    local_frame_bases_.pop_back();

    // remove values from param_stack_
    if (base <= param_stack_.size()) {
        param_stack_.resize(base);
    }

    if (!local_frames_.empty()) local_frames_.pop_back();
}

bool VM::local_lookup(const std::string& name, Value& out) const {
    if (local_frames_.empty()) return false;
    const auto& frame = local_frames_.back();
    auto it = frame.find(name);
    if (it == frame.end()) return false;
    size_t idx = it->second;
    if (idx >= param_stack_.size()) return false;
    out = param_stack_[idx];
    return true;
}

bool VM::binary_op(OpCode op) {
    Value b = pop();
    Value a = pop();
    
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
        }
    }

    // Mark globals
    for (const auto& pair : globals_) {
        if (pair.second.type() == ValueType::STRING_ID) {
            strings_.mark_string_reachable(pair.second.as_string_id());
        } else if (pair.second.type() == ValueType::STRING_BUFFER) {
            buffers_.mark_buffer_reachable(pair.second.as_buffer_id());
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