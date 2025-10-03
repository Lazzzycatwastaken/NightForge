#include "vm.h"
#include <iostream>
#include <cstdarg>
#include <cstring>
#include <algorithm>
#include <cctype>
#include <chrono>

#ifdef DEBUG_TRACE_EXECUTION
#  undef DEBUG_TRACE_EXECUTION
#endif

namespace nightforge {
namespace nightscript {

VM::VM() {
    reset_stack();
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

void VM::register_host_function(const std::string& name, HostFunction func) {
    host_functions_[name] = func;
}

void VM::set_global(const std::string& name, const Value& value) {
    globals_[name] = value;
}

Value VM::get_global(const std::string& name) {
    auto it = globals_.find(name);
    if (it != globals_.end()) {
        return it->second;
    }
    return Value::nil(); // undefined global
}

void VM::print_stack() {
    std::cout << "Stack: ";
    for (Value* slot = stack_; slot < stack_top_; ++slot) {
        std::cout << "[";
        switch (slot->type) {
            case ValueType::NIL: std::cout << "nil"; break;
            case ValueType::BOOL: std::cout << (slot->as.boolean ? "true" : "false"); break;
            case ValueType::INT: std::cout << slot->as.integer; break;
            case ValueType::FLOAT: std::cout << slot->as.floating; break;
            case ValueType::STRING_ID: 
                std::cout << "\"" << strings_.get_string(slot->as.string_id) << "\""; 
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
        
        uint8_t instruction = read_byte(ip);
        
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
                push(Value::integer(a.as.integer + b.as.integer));
                break;
            }
            
            case OpCode::OP_ADD_FLOAT: {
                Value b = pop();
                Value a = pop();
                push(Value::floating(a.as.floating + b.as.floating));
                break;
            }
            
            case OpCode::OP_ADD_STRING: {
                Value b = pop();
                Value a = pop();
                
                // Convert both operands to strings (like binary_op does)
                std::string str_a, str_b;
                
                // Convert a to string
                if (a.type == ValueType::STRING_ID) {
                    str_a = strings_.get_string(a.as.string_id);
                } else if (a.type == ValueType::INT) {
                    str_a = std::to_string(a.as.integer);
                } else if (a.type == ValueType::FLOAT) {
                    str_a = std::to_string(a.as.floating);
                } else if (a.type == ValueType::BOOL) {
                    str_a = a.as.boolean ? "true" : "false";
                } else if (a.type == ValueType::NIL) {
                    str_a = "nil";
                } else {
                    str_a = "unknown";
                }
                
                // Convert b to string
                if (b.type == ValueType::STRING_ID) {
                    str_b = strings_.get_string(b.as.string_id);
                } else if (b.type == ValueType::INT) {
                    str_b = std::to_string(b.as.integer);
                } else if (b.type == ValueType::FLOAT) {
                    str_b = std::to_string(b.as.floating);
                } else if (b.type == ValueType::BOOL) {
                    str_b = b.as.boolean ? "true" : "false";
                } else if (b.type == ValueType::NIL) {
                    str_b = "nil";
                } else {
                    str_b = "unknown";
                }
                
                // Concatenate and intern the result
                std::string result = str_a + str_b;
                uint32_t result_id = strings_.intern(result);
                push(Value::string_id(result_id));
                
                // Trigger GC if we're using too much memory
                bytes_allocated_since_gc_ += result.length();
                if (bytes_allocated_since_gc_ > GC_THRESHOLD) {
                    collect_garbage(&chunk);
                }
                break;
            }
            
            case OpCode::OP_SUB_INT: {
                Value b = pop();
                Value a = pop();
                push(Value::integer(a.as.integer - b.as.integer));
                break;
            }
            
            case OpCode::OP_SUB_FLOAT: {
                Value b = pop();
                Value a = pop();
                push(Value::floating(a.as.floating - b.as.floating));
                break;
            }
            
            case OpCode::OP_MUL_INT: {
                Value b = pop();
                Value a = pop();
                push(Value::integer(a.as.integer * b.as.integer));
                break;
            }
            
            case OpCode::OP_MUL_FLOAT: {
                Value b = pop();
                Value a = pop();
                push(Value::floating(a.as.floating * b.as.floating));
                break;
            }
            
            case OpCode::OP_DIV_INT: {
                Value b = pop();
                Value a = pop();
                if (b.as.integer == 0) {
                    runtime_error("Division by zero");
                    return VMResult::RUNTIME_ERROR;
                }
                push(Value::integer(a.as.integer / b.as.integer));
                break;
            }
            
            case OpCode::OP_DIV_FLOAT: {
                Value b = pop();
                Value a = pop();
                if (b.as.floating == 0.0) {
                    runtime_error("Division by zero");
                    return VMResult::RUNTIME_ERROR;
                }
                push(Value::floating(a.as.floating / b.as.floating));
                break;
            }
            
            case OpCode::OP_MOD_INT: {
                Value b = pop();
                Value a = pop();
                if (b.as.integer == 0) {
                    runtime_error("Modulo by zero");
                    return VMResult::RUNTIME_ERROR;
                }
                push(Value::integer(a.as.integer % b.as.integer));
                break;
            }
                
            case OpCode::OP_NOT: {
                Value value = pop();
                bool is_falsy = (value.type == ValueType::NIL) || 
                               (value.type == ValueType::BOOL && !value.as.boolean);
                push(Value::boolean(is_falsy));
                break;
            }

            case OpCode::OP_JUMP_IF_FALSE: {
                Value cond = pop();
                bool is_false = (cond.type == ValueType::NIL) || (cond.type == ValueType::BOOL && !cond.as.boolean);
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
                if (a.type == b.type) {
                    switch (a.type) {
                        case ValueType::NIL: equal = true; break;
                        case ValueType::BOOL: equal = a.as.boolean == b.as.boolean; break;
                        case ValueType::INT: equal = a.as.integer == b.as.integer; break;
                        case ValueType::FLOAT: equal = a.as.floating == b.as.floating; break;
                        case ValueType::STRING_ID: equal = a.as.string_id == b.as.string_id; break;
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
                if (a.type == b.type) {
                    switch (a.type) {
                        case ValueType::INT: result = a.as.integer > b.as.integer; break;
                        case ValueType::FLOAT: result = a.as.floating > b.as.floating; break;
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
                if (a.type == b.type) {
                    switch (a.type) {
                        case ValueType::INT: result = a.as.integer >= b.as.integer; break;
                        case ValueType::FLOAT: result = a.as.floating >= b.as.floating; break;
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
                if (a.type == b.type) {
                    switch (a.type) {
                        case ValueType::INT: result = a.as.integer <= b.as.integer; break;
                        case ValueType::FLOAT: result = a.as.floating <= b.as.floating; break;
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
                if (a.type == b.type) {
                    switch (a.type) {
                        case ValueType::INT: result = a.as.integer < b.as.integer; break;
                        case ValueType::FLOAT: result = a.as.floating < b.as.floating; break;
                        default: result = false; break;
                    }
                }
                push(Value::boolean(result));
                break;
            }
            
            case OpCode::OP_PRINT: {
                Value value = pop();
                switch (value.type) {
                    case ValueType::NIL: std::cout << "nil"; break;
                    case ValueType::BOOL: std::cout << (value.as.boolean ? "true" : "false"); break;
                    case ValueType::INT: std::cout << value.as.integer; break;
                    case ValueType::FLOAT: std::cout << value.as.floating; break;
                    case ValueType::STRING_ID: 
                        std::cout << strings_.get_string(value.as.string_id); 
                        break;
                    default: std::cout << "unknown"; break;
                }
                std::cout << std::endl;
                break;
            }
            
            case OpCode::OP_PRINT_SPACE: {
                Value value = pop();
                switch (value.type) {
                    case ValueType::NIL: std::cout << "nil"; break;
                    case ValueType::BOOL: std::cout << (value.as.boolean ? "true" : "false"); break;
                    case ValueType::INT: std::cout << value.as.integer; break;
                    case ValueType::FLOAT: std::cout << value.as.floating; break;
                    case ValueType::STRING_ID: 
                        std::cout << strings_.get_string(value.as.string_id); 
                        break;
                    default: std::cout << "unknown"; break;
                }
                std::cout << " ";  // space instead of newline
                break;
            }
            
            case OpCode::OP_CALL_HOST: {
                Value function_name = read_constant(chunk, ip);
                uint8_t arg_count = read_byte(ip);
                
                if (function_name.type != ValueType::STRING_ID) {
                    runtime_error("Expected function name");
                    return VMResult::RUNTIME_ERROR;
                }
                
                std::string func_name = strings_.get_string(function_name.as.string_id);
                // Convert to lowercase for case-insensitive lookup
                std::string func_name_lc = func_name;
                std::transform(func_name_lc.begin(), func_name_lc.end(), func_name_lc.begin(), [](unsigned char c){ return std::tolower(c); });
                auto it = host_functions_.find(func_name_lc);

                // Collect arguments from stack
                std::vector<Value> args;
                for (int i = 0; i < arg_count; ++i) {
                    args.insert(args.begin(), pop()); // reverse order
                }

                if (it != host_functions_.end()) {
                    // Call host function
                    Value result = it->second(args);
                    push(result);
                    break;
                }

                // If not a host function, check for user-defined function in the chunk
                ssize_t func_index = chunk.get_function_index(func_name_lc);
                if (func_index >= 0) {
                    const Chunk& fchunk = chunk.get_function(static_cast<size_t>(func_index));

                    // Set function parameters as globals
                    const std::vector<std::string>& param_names = chunk.get_function_param_names(static_cast<size_t>(func_index));
                    
                    // Save current parameter values
                    std::vector<Value> saved_param_values;
                    std::vector<bool> had_saved_params;
                    
                    for (size_t i = 0; i < param_names.size(); ++i) {
                        const std::string& param_name = param_names[i];
                        
                        // Save current value if it exists
                        auto global_it = globals_.find(param_name);
                        if (global_it != globals_.end()) {
                            saved_param_values.push_back(global_it->second);
                            had_saved_params.push_back(true);
                        } else {
                            saved_param_values.push_back(Value::nil());
                            had_saved_params.push_back(false);
                        }
                        
                        // Set parameter value
                        if (i < args.size()) {
                            set_global(param_name, args[i]);
                        } else {
                            set_global(param_name, Value::nil());
                        }
                    }

                    // Save current stack state
                    Value* saved_stack_top = stack_top_;
                    
                    VMResult r = execute(fchunk, &chunk);
                    
                    // Restore parameter values
                    for (size_t i = 0; i < param_names.size(); ++i) {
                        const std::string& param_name = param_names[i];
                        if (had_saved_params[i]) {
                            set_global(param_name, saved_param_values[i]);
                        } else {
                            globals_.erase(param_name);
                        }
                    }
                    
                    if (r != VMResult::OK) return r;

                    if (stack_top_ <= stack_) {
                        push(Value::nil());
                    }
                    
                    Value return_value = pop();
                    stack_top_ = saved_stack_top;
                    push(return_value);
                    break;
                }

                if (parent_chunk) {
                    ssize_t parent_func_index = parent_chunk->get_function_index(func_name_lc);
                    if (parent_func_index >= 0) {
                        const Chunk& fchunk = parent_chunk->get_function(static_cast<size_t>(parent_func_index));

                        // Set function parameters as globals  
                        const std::vector<std::string>& param_names = parent_chunk->get_function_param_names(static_cast<size_t>(parent_func_index));

                        std::vector<Value> saved_param_values;
                        std::vector<bool> had_saved_params;
                        
                        for (size_t i = 0; i < param_names.size(); ++i) {
                            const std::string& param_name = param_names[i];
                            
                            // Save current value if it exists
                            auto global_it = globals_.find(param_name);
                            if (global_it != globals_.end()) {
                                saved_param_values.push_back(global_it->second);
                                had_saved_params.push_back(true);
                            } else {
                                saved_param_values.push_back(Value::nil());
                                had_saved_params.push_back(false);
                            }
                            
                            if (i < args.size()) {
                                set_global(param_name, args[i]);
                            } else {
                                set_global(param_name, Value::nil());
                            }
                        }

                        // Save current stack state
                        Value* saved_stack_top = stack_top_;
                        
                        // Execute function chunk pass parent chunk as parent for recursive lookups
                        VMResult r = execute(fchunk, parent_chunk);
                        
                        // Restore parameter values
                        for (size_t i = 0; i < param_names.size(); ++i) {
                            const std::string& param_name = param_names[i];
                            if (had_saved_params[i]) {
                                set_global(param_name, saved_param_values[i]);
                            } else {
                                globals_.erase(param_name);
                            }
                        }
                        
                        if (r != VMResult::OK) return r;

                        if (stack_top_ <= stack_) {
                            push(Value::nil());
                        }

                        Value return_value = pop();
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
                
                if (function_name.type != ValueType::STRING_ID) {
                    runtime_error("Expected function name");
                    return VMResult::RUNTIME_ERROR;
                }
                
                std::string func_name = strings_.get_string(function_name.as.string_id);
                std::string func_name_lc = func_name;
                std::transform(func_name_lc.begin(), func_name_lc.end(), func_name_lc.begin(), [](unsigned char c){ return std::tolower(c); });
                
                std::vector<Value> args;
                for (int i = 0; i < arg_count; ++i) {
                    args.insert(args.begin(), pop());
                }
                
                ssize_t func_index = chunk.get_function_index(func_name_lc);
                if (func_index >= 0) {
                    const std::vector<std::string>& param_names = chunk.get_function_param_names(static_cast<size_t>(func_index));
                    
                    for (size_t i = 0; i < param_names.size(); ++i) {
                        const std::string& param_name = param_names[i];
                        if (i < args.size()) {
                            set_global(param_name, args[i]);
                        } else {
                            set_global(param_name, Value::nil());
                        }
                    }
                    
                    // Reset instruction pointer to start of current function
                    // This creates a loop instead of recursion eliminating stack growth
                    ip = chunk.code().data();
                    continue; // Restart execution from the beginning
                }
                
                auto it = host_functions_.find(func_name_lc);
                if (it != host_functions_.end()) {
                    Value result = it->second(args);
                    push(result);
                } else {
                    runtime_error("Unknown function in tail call: %s", func_name.c_str());
                    return VMResult::RUNTIME_ERROR;
                }
                break;
            }
            
            case OpCode::OP_GET_GLOBAL: {
                Value variable_name = read_constant(chunk, ip);
                if (variable_name.type != ValueType::STRING_ID) {
                    runtime_error("Expected variable name");
                    return VMResult::RUNTIME_ERROR;
                }
                
                std::string var_name = strings_.get_string(variable_name.as.string_id);
                Value value = get_global(var_name);
                push(value);
                break;
            }
            
            case OpCode::OP_SET_GLOBAL: {
                Value variable_name = read_constant(chunk, ip);
                if (variable_name.type != ValueType::STRING_ID) {
                    runtime_error("Expected variable name");
                    return VMResult::RUNTIME_ERROR;
                }
                
                std::string var_name = strings_.get_string(variable_name.as.string_id);
                Value value = peek(); // Don't pop - assignment is an expression
                set_global(var_name, value);
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

bool VM::binary_op(OpCode op) {
    Value b = pop();
    Value a = pop();
    
    // Handle string concatenation for addition
    if (op == OpCode::OP_ADD && (a.type == ValueType::STRING_ID || b.type == ValueType::STRING_ID)) {
        std::string str_a, str_b;
        
        // Convert a to string
        if (a.type == ValueType::STRING_ID) {
            str_a = strings_.get_string(a.as.string_id);
        } else if (a.type == ValueType::INT) {
            str_a = std::to_string(a.as.integer);
        } else if (a.type == ValueType::FLOAT) {
            str_a = std::to_string(a.as.floating);
        } else if (a.type == ValueType::BOOL) {
            str_a = a.as.boolean ? "true" : "false";
        } else if (a.type == ValueType::NIL) {
            str_a = "nil";
        } else {
            str_a = "unknown";
        }
        
        // Convert b to string
        if (b.type == ValueType::STRING_ID) {
            str_b = strings_.get_string(b.as.string_id);
        } else if (b.type == ValueType::INT) {
            str_b = std::to_string(b.as.integer);
        } else if (b.type == ValueType::FLOAT) {
            str_b = std::to_string(b.as.floating);
        } else if (b.type == ValueType::BOOL) {
            str_b = b.as.boolean ? "true" : "false";
        } else if (b.type == ValueType::NIL) {
            str_b = "nil";
        } else {
            str_b = "unknown";
        }
        
        std::string result = str_a + str_b;
        uint32_t result_id = strings_.intern(result);
        push(Value::string_id(result_id));
        return true;
    }
    
    // Type checking (keep it simple for now)
    if (a.type == ValueType::INT && b.type == ValueType::INT) {
        int64_t result;
        switch (op) {
            case OpCode::OP_ADD: result = a.as.integer + b.as.integer; break;
            case OpCode::OP_SUBTRACT: result = a.as.integer - b.as.integer; break;
            case OpCode::OP_MULTIPLY: result = a.as.integer * b.as.integer; break;
            case OpCode::OP_DIVIDE: 
                if (b.as.integer == 0) {
                    runtime_error("Don't divide by zero.");
                    return false;
                }
                result = a.as.integer / b.as.integer; 
                break;
            case OpCode::OP_MODULO:
                if (b.as.integer == 0) {
                    runtime_error("Don't modulo by zero.");
                    return false;
                }
                result = a.as.integer % b.as.integer; 
                break;
            default:
                runtime_error("Unknown binary operator");
                return false;
        }
        push(Value::integer(result));
        return true;
    }
    
    if (a.type == ValueType::FLOAT || b.type == ValueType::FLOAT) {
        double da = (a.type == ValueType::FLOAT) ? a.as.floating : static_cast<double>(a.as.integer);
        double db = (b.type == ValueType::FLOAT) ? b.as.floating : static_cast<double>(b.as.integer);
        
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
    
    // Mark all reachable strings
    mark_reachable_strings(active_chunk);
    
    // Sweep unreachable strings
    size_t old_memory = strings_.memory_usage();
    strings_.sweep_unreachable_strings();
    size_t new_memory = strings_.memory_usage();
    
    // Update stats
    stats.gc_collections++;
    stats.bytes_freed += (old_memory - new_memory);
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
        if (slot->type == ValueType::STRING_ID) {
            strings_.mark_string_reachable(slot->as.string_id);
        }
    }

    // Mark globals
    for (const auto& pair : globals_) {
        if (pair.second.type == ValueType::STRING_ID) {
            strings_.mark_string_reachable(pair.second.as.string_id);
        }
    }

    // Also mark any strings stored in the active chunk's constants (function names, string literals)
    if (active_chunk) {
        for (const auto& constant : active_chunk->constants()) {
            if (constant.type == ValueType::STRING_ID) {
                strings_.mark_string_reachable(constant.as.string_id);
            }
        }

        // Also mark nested function names/constants
        for (size_t i = 0; i < active_chunk->function_count(); ++i) {
            const Chunk& f = active_chunk->get_function(i);
            for (const auto& c : f.constants()) {
                if (c.type == ValueType::STRING_ID) strings_.mark_string_reachable(c.as.string_id);
            }
        }
    }
}

} // namespace nightscript
} // namespace nightforge