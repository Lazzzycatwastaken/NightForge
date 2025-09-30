#pragma once
#include <cstdint>
#include <vector>
#include <string>
#include <unordered_map>

namespace nightforge {
namespace nightscript {

// Bytecode instructions (keep it simple)
enum class OpCode : uint8_t {
    // Constants
    OP_CONSTANT,     // load constant
    OP_NIL,          // push nil
    OP_TRUE,         // push true  
    OP_FALSE,        // push false
    
    // Variables
    OP_GET_GLOBAL,   // get global variable
    OP_SET_GLOBAL,   // set global variable
    OP_GET_LOCAL,    // get local variable
    OP_SET_LOCAL,    // set local variable
    
    // Arithmetic
    OP_ADD,
    OP_SUBTRACT,
    OP_MULTIPLY,
    OP_DIVIDE,
    
    // Comparison
    OP_EQUAL,
    OP_GREATER,
    OP_LESS,
    OP_NOT,
    
    // Control flow
    OP_JUMP,         // unconditional jump
    OP_JUMP_IF_FALSE, // conditional jump
    OP_CALL,         // call host function
    OP_CALL_HOST,    // call host function with name
    OP_RETURN,       // return from function
    
    // Special
    OP_POP,          // pop top of stack
    OP_PRINT,        // debug print (for now)
};

// Value types (POD union as specified)
enum class ValueType : uint8_t {
    NIL,
    BOOL,
    INT,
    FLOAT,
    STRING_ID,  // interned string ID
    TABLE_ID,   // table reference
};

struct Value {
    ValueType type;
    uint8_t padding[7]; // align to 8 bytes
    union {
        bool boolean;
        int64_t integer;
        double floating;
        uint32_t string_id;
        uint32_t table_id;
    } as;
    
    Value() : type(ValueType::NIL) { as.integer = 0; }
    
    static Value nil() {
        Value v;
        v.type = ValueType::NIL;
        return v;
    }
    
    static Value boolean(bool b) {
        Value v;
        v.type = ValueType::BOOL;
        v.as.boolean = b;
        return v;
    }
    
    static Value integer(int64_t i) {
        Value v;
        v.type = ValueType::INT;
        v.as.integer = i;
        return v;
    }
    
    static Value floating(double f) {
        Value v;
        v.type = ValueType::FLOAT;
        v.as.floating = f;
        return v;
    }
    
    static Value string_id(uint32_t id) {
        Value v;
        v.type = ValueType::STRING_ID;
        v.as.string_id = id;
        return v;
    }
};

// Bytecode chunk (contains instructions + constants)
class Chunk {
public:
    void write_byte(uint8_t byte, int line);
    void write_constant(const Value& value, int line);
    
    size_t add_constant(const Value& value);
    Value get_constant(size_t index) const;
    
    const std::vector<uint8_t>& code() const { return code_; }
    const std::vector<Value>& constants() const { return constants_; }
    const std::vector<int>& lines() const { return lines_; }
    
private:
    std::vector<uint8_t> code_;      // bytecode instructions
    std::vector<Value> constants_;   // constant pool
    std::vector<int> lines_;         // line numbers for debugging
};

// String intern table (for performance)
class StringTable {
public:
    uint32_t intern(const std::string& str);
    const std::string& get_string(uint32_t id) const;
    
private:
    std::vector<std::string> strings_;
    std::unordered_map<std::string, uint32_t> string_to_id_;
};

} // namespace nightscript
} // namespace nightforge