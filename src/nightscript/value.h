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
    
    // Arithmetic - Generic
    OP_ADD,
    OP_SUBTRACT,
    OP_MULTIPLY,
    OP_DIVIDE,
    OP_MODULO,
    
    // Arithmetic - Performance
    OP_ADD_INT,      // integer addition
    OP_ADD_FLOAT,    // float addition
    OP_ADD_STRING,   // string concatenation
    OP_SUB_INT,
    OP_SUB_FLOAT,
    OP_MUL_INT,
    OP_MUL_FLOAT,
    OP_DIV_INT,
    OP_DIV_FLOAT,
    OP_MOD_INT,
    
    // Comparison
    OP_EQUAL,
    OP_GREATER,
    OP_GREATER_EQUAL,
    OP_LESS_EQUAL,
    OP_LESS,
    OP_NOT,
    
    // Control flow
    OP_JUMP,         // unconditional jump
    OP_JUMP_IF_FALSE, // conditional jump
    OP_JUMP_BACK,    // jump backwards (for loops)
    OP_CALL,         // call host function
    OP_CALL_HOST,    // call host function with name
    OP_TAIL_CALL,    // optimized tail call
    OP_RETURN,       // return from function
    
    // Special
    OP_POP,          // pop top of stack
    OP_PRINT,        // debug print (for now)
    OP_PRINT_SPACE,  // print with space separator
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
    
    static Value table_id(uint32_t id) {
        Value v;
        v.type = ValueType::TABLE_ID;
        v.as.table_id = id;
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
    // User-defined functions stored with the chunk
    size_t add_function(const Chunk& function_chunk, const std::vector<std::string>& param_names, const std::string& function_name);
    const Chunk& get_function(size_t index) const;
    const std::vector<std::string>& get_function_param_names(size_t index) const;
    ssize_t get_function_index(const std::string& name) const;
    size_t function_count() const;
    const std::string& function_name(size_t index) const;
    void add_function_name(const std::string& name);
    void add_function_name_to_child(size_t child_index, const std::string& name);
    size_t code_size() const { return code_.size(); }
    void patch_byte(size_t index, uint8_t byte);
    
private:
    std::vector<uint8_t> code_;      // bytecode instructions
    std::vector<Value> constants_;   // constant pool
    std::vector<int> lines_;         // line numbers for debugging
    // user functions
    std::vector<Chunk> functions_;
    std::vector<std::vector<std::string>> function_params_; // changed to vector of vectors
    std::vector<std::string> function_names_;
};

// String intern table (for performance + GC)
class StringTable {
public:
    uint32_t intern(const std::string& str);
    const std::string& get_string(uint32_t id) const;
    
    // Garbage collection support
    void mark_string_reachable(uint32_t id);
    void sweep_unreachable_strings();
    void clear_gc_marks();
    
    // Performance monitoring
    size_t memory_usage() const;
    size_t string_count() const { return strings_.size(); }
    
    // String concatenation optimization
    uint32_t concat_strings(uint32_t id1, uint32_t id2);
    uint32_t concat_string_literal(uint32_t id, const std::string& literal);
    
private:
    struct StringEntry {
        std::string str;
        bool gc_marked = false;
        size_t ref_count = 0;
    };
    
    std::vector<StringEntry> strings_;
    std::unordered_map<std::string, uint32_t> string_to_id_;
    std::vector<uint32_t> free_slots_; // for reusing deleted string slots
};

} // namespace nightscript
} // namespace nightforge