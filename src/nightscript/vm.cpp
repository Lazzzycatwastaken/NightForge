#include "vm.h"
#include <iostream>
#include <cstdarg>
#include <cstring>

namespace nightforge {
namespace nightscript {

VM::VM() {
    reset_stack();
}

VM::~VM() {
    // cleanup if needed
}

VMResult VM::execute(const Chunk& chunk) {
    return run(chunk);
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
        return;
    }
    *stack_top_ = value;
    stack_top_++;
}

Value VM::pop() {
    if (stack_top_ <= stack_) {
        runtime_error("Stack underflow");
        return Value::nil();
    }
    stack_top_--;
    return *stack_top_;
}

Value VM::peek(int distance) {
    if (stack_top_ - 1 - distance < stack_) {
        return Value::nil();
    }
    return stack_top_[-1 - distance];
}

void VM::reset_stack() {
    stack_top_ = stack_;
}

VMResult VM::run(const Chunk& chunk) {
    const uint8_t* ip = chunk.code().data();
    const uint8_t* end = ip + chunk.code().size();
    
#ifdef DEBUG_TRACE_EXECUTION
    std::cout << "== execution begin ==" << std::endl;
#endif
    
    while (ip < end) {
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
                if (!binary_op(static_cast<OpCode>(instruction))) {
                    return VMResult::RUNTIME_ERROR;
                }
                break;
                
            case OpCode::OP_NOT: {
                Value value = pop();
                bool is_falsy = (value.type == ValueType::NIL) || 
                               (value.type == ValueType::BOOL && !value.as.boolean);
                push(Value::boolean(is_falsy));
                break;
            }
            
            case OpCode::OP_EQUAL: {
                Value b = pop();
                Value a = pop();
                // Simple equality (could be more sophisticated)
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
                auto it = host_functions_.find(func_name);
                if (it == host_functions_.end()) {
                    runtime_error("Unknown function: %s", func_name.c_str());
                    return VMResult::RUNTIME_ERROR;
                }
                
                // Collect arguments from stack
                std::vector<Value> args;
                for (int i = 0; i < arg_count; ++i) {
                    args.insert(args.begin(), pop()); // reverse order
                }
                
                // Call host function
                Value result = it->second(args);
                push(result);
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

} // namespace nightscript
} // namespace nightforge