#pragma once
#include <cstdint>
#include <cstring>
#include <vector>
#include <string>
#include <unordered_map>
#include <functional>

#ifdef _WIN32
    #include <BaseTsd.h>
    typedef SSIZE_T ssize_t;
#elif !defined(ssize_t) && !defined(_SSIZE_T_DEFINED)
    #include <sys/types.h>
#endif

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
    // OP_CALL removed useless
    OP_CALL_HOST,    // call host function with name
    OP_TAIL_CALL,    // optimized tail call
    OP_RETURN,       // return from function
    
    // Special
    OP_POP,          // pop top of stack
    OP_PRINT,        // debug print (for now)
    OP_PRINT_SPACE,  // print with space separator

    OP_ADD_LOCAL,
    OP_ADD_FLOAT_LOCAL,
    OP_ADD_STRING_LOCAL,

    OP_CONSTANT_LOCAL,
    OP_ADD_LOCAL_CONST,
    OP_ADD_CONST_LOCAL,
    OP_ADD_LOCAL_CONST_FLOAT,
    OP_ADD_CONST_LOCAL_FLOAT,

    OP_ARRAY_CREATE,
    OP_ARRAY_GET,
    OP_ARRAY_SET,
    OP_ARRAY_LENGTH,
    OP_ARRAY_PUSH,
    OP_ARRAY_POP,
};

// Value types (classification)
enum class ValueType : uint8_t {
    NIL,
    BOOL,
    INT,
    FLOAT,
    STRING_BUFFER, // mutable string builder buffer
    STRING_ID,  // interned string ID
    TABLE_ID,   // table reference
    ARRAY,      // array reference
};

// NaN-boxed 64-bit value
// Layout:
//  - Non-qNaN bit patterns represent a 64-bit IEEE-754 double (FLOAT)
//  - qNaN (0x7FF8...) space encodes other types
//    Small immediates:
//     - NIL   = QNAN | 0x1
//     - FALSE = QNAN | 0x2
//     - TRUE  = QNAN | 0x3
//    Tagged payloads (48-bit payload in low bits):
//     - INT       = QNAN | (TAG_INT    << 48) | sign-extended 48-bit integer
//     - STRING_ID = QNAN | (TAG_STRING << 48) | uint32 payload (id)
//     - TABLE_ID  = QNAN | (TAG_TABLE  << 48) | uint32 payload (id)
struct Value {
private:
    uint64_t bits_ = 0;

    static constexpr uint64_t EXP_MASK   = 0x7FF0000000000000ULL;
    static constexpr uint64_t QNAN_MASK  = 0x7FF8000000000000ULL;
    static constexpr uint64_t QNAN       = 0x7FF8000000000000ULL; // canonical qNaN
    static constexpr uint64_t PAYLOAD_MASK_48 = 0x0000FFFFFFFFFFFFULL; // 48-bit payload
    static constexpr uint64_t SIGN_EXT_MASK_48 = 0xFFFF000000000000ULL; // for sign-ext
    static constexpr int      TAG_SHIFT  = 48;
    static constexpr uint64_t TAG_MASK   = 0x0007000000000000ULL; // 3 bits for tag

    // Small immediates
    static constexpr uint64_t TAG_NIL    = QNAN | 0x0000000000000001ULL;
    static constexpr uint64_t TAG_FALSE  = QNAN | 0x0000000000000002ULL;
    static constexpr uint64_t TAG_TRUE   = QNAN | 0x0000000000000003ULL;

    // Tagged families
    static constexpr uint64_t TAG_FAMILY_INT    = (0x1ULL << TAG_SHIFT);
    static constexpr uint64_t TAG_FAMILY_STRING = (0x2ULL << TAG_SHIFT);
    static constexpr uint64_t TAG_FAMILY_TABLE  = (0x3ULL << TAG_SHIFT);
    static constexpr uint64_t TAG_FAMILY_BUFFER = (0x4ULL << TAG_SHIFT);
    static constexpr uint64_t TAG_FAMILY_ARRAY  = (0x5ULL << TAG_SHIFT);

    static constexpr uint64_t MAKE_FAMILY(uint64_t family) { return QNAN | family; }

    static bool is_qnan(uint64_t b) { return (b & QNAN_MASK) == QNAN; }

public:
    Value() : bits_(TAG_NIL) {}

    // Factories
    static Value nil() {
        Value v; v.bits_ = TAG_NIL; return v;
    }
    static Value boolean(bool b) {
        Value v; v.bits_ = b ? TAG_TRUE : TAG_FALSE; return v;
    }
    static Value integer(int64_t i) {
        // store as 48-bit signed integer payload
        uint64_t payload = static_cast<uint64_t>(i) & PAYLOAD_MASK_48;
        Value v; v.bits_ = MAKE_FAMILY(TAG_FAMILY_INT) | payload; return v;
    }
    static Value floating(double f) {
        Value v; std::memcpy(&v.bits_, &f, sizeof(double)); return v;
    }
    static Value string_id(uint32_t id) {
        Value v; v.bits_ = MAKE_FAMILY(TAG_FAMILY_STRING) | static_cast<uint64_t>(id); return v;
    }
    static Value buffer_id(uint32_t id) {
        Value v; v.bits_ = MAKE_FAMILY(TAG_FAMILY_BUFFER) | static_cast<uint64_t>(id); return v;
    }
    static Value table_id(uint32_t id) {
        Value v; v.bits_ = MAKE_FAMILY(TAG_FAMILY_TABLE) | static_cast<uint64_t>(id); return v;
    }
    static Value array_id(uint32_t id) {
        Value v; v.bits_ = MAKE_FAMILY(TAG_FAMILY_ARRAY) | static_cast<uint64_t>(id); return v;
    }

    // Classification
    ValueType type() const {
        if (!is_qnan(bits_)) return ValueType::FLOAT; // normal numbers, +/-inf, sNaN treated as float
        if (bits_ == TAG_NIL) return ValueType::NIL;
        if (bits_ == TAG_TRUE || bits_ == TAG_FALSE) return ValueType::BOOL;
        uint64_t tag = bits_ & TAG_MASK;
        if (tag == TAG_FAMILY_INT) return ValueType::INT;
        if (tag == TAG_FAMILY_BUFFER) return ValueType::STRING_BUFFER;
        if (tag == TAG_FAMILY_STRING) return ValueType::STRING_ID;
        if (tag == TAG_FAMILY_TABLE) return ValueType::TABLE_ID;
        if (tag == TAG_FAMILY_ARRAY) return ValueType::ARRAY;
        // Fallback treat as float (covers numeric qNaN payloads)
        return ValueType::FLOAT;
    }

    // Predicates
    bool is_nil() const { return bits_ == TAG_NIL; }
    bool is_bool() const { return bits_ == TAG_TRUE || bits_ == TAG_FALSE; }
    bool is_true() const { return bits_ == TAG_TRUE; }
    bool is_false() const { return bits_ == TAG_FALSE; }
    bool is_int() const { return is_qnan(bits_) && ((bits_ & TAG_MASK) == TAG_FAMILY_INT); }
    bool is_float() const { return !is_qnan(bits_); }
    bool is_string_id() const { return is_qnan(bits_) && ((bits_ & TAG_MASK) == TAG_FAMILY_STRING); }
    bool is_buffer_id() const { return is_qnan(bits_) && ((bits_ & TAG_MASK) == TAG_FAMILY_BUFFER); }
    bool is_table_id() const { return is_qnan(bits_) && ((bits_ & TAG_MASK) == TAG_FAMILY_TABLE); }
    bool is_array_id() const { return is_qnan(bits_) && ((bits_ & TAG_MASK) == TAG_FAMILY_ARRAY); }

    // Accessors (caller must ensure the type matches)
    bool as_boolean() const { return bits_ == TAG_TRUE; }
    int64_t as_integer() const {
        uint64_t payload = bits_ & PAYLOAD_MASK_48;
        // sign-extend 48-bit value to 64-bit
        if (payload & (1ULL << 47)) {
            payload |= SIGN_EXT_MASK_48;
        }
        return static_cast<int64_t>(payload);
    }
    double as_floating() const {
        double d; std::memcpy(&d, &bits_, sizeof(double)); return d;
    }
    uint32_t as_string_id() const { return static_cast<uint32_t>(bits_ & 0xFFFFFFFFULL); }
    uint32_t as_buffer_id() const { return static_cast<uint32_t>(bits_ & 0xFFFFFFFFULL); }
    uint32_t as_table_id() const { return static_cast<uint32_t>(bits_ & 0xFFFFFFFFULL); }
    uint32_t as_array_id() const { return static_cast<uint32_t>(bits_ & 0xFFFFFFFFULL); }
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
    size_t add_function(const Chunk& function_chunk, const std::vector<std::string>& param_names, const std::vector<std::string>& local_names, const std::string& function_name);
    const Chunk& get_function(size_t index) const;
    const std::vector<std::string>& get_function_param_names(size_t index) const;
    const std::vector<std::string>& get_function_local_names(size_t index) const;
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
    std::vector<std::vector<std::string>> function_locals_; // local variable names per function
    std::vector<std::string> function_names_;
};

// String intern table (for performance + GC)
class StringTable {
public:
    uint32_t intern(const std::string& str);
    const std::string& get_string(uint32_t id) const;
    // Return 0xFFFFFFFF if not found
    uint32_t find_id(const std::string& str) const;
    
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
    uint32_t append_to_interned(uint32_t left_id, const std::string& suffix);
    // Append from another string id
    uint32_t append_id_to_interned(uint32_t left_id, uint32_t right_id);
    
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

// Mutable string buffer table (string builders)
class BufferTable {
public:
    uint32_t create_from_two(const std::string& a, const std::string& b);
    uint32_t create_from_ids(uint32_t left_id, uint32_t right_id, const StringTable& strings);
    const std::string& get_buffer(uint32_t id) const;
    uint32_t append_literal(uint32_t id, const std::string& suffix);
    uint32_t append_id(uint32_t left_id, uint32_t right_id, const StringTable& strings);

    // Reserve capacity for a buffer
    void reserve(uint32_t id, size_t capacity);

    // GC support
    void mark_buffer_reachable(uint32_t id);
    void sweep_unreachable_buffers();
    void clear_gc_marks();

    size_t memory_usage() const;

private:
    struct BufferEntry {
        std::string str;
        bool gc_marked = false;
        size_t ref_count = 0;
    };

    std::vector<BufferEntry> buffers_;
    std::vector<uint32_t> free_slots_;
};

class ArrayTable {
public:
    uint32_t create(size_t reserve = 0);
    size_t length(uint32_t id) const;
    void push_back(uint32_t id, const Value& v);
    Value pop_back(uint32_t id);
    Value get(uint32_t id, ssize_t index) const;
    void set(uint32_t id, ssize_t index, const Value& v);
    Value remove_at(uint32_t id, ssize_t index);
    void clear(uint32_t id);

    // GC support
    void mark_array_reachable(uint32_t id);
    void clear_gc_marks();
    // Traverse to mark contained references (strings/buffers/arrays)
    void for_each(uint32_t id, const std::function<void(const Value&)>& fn) const;

private:
    struct ArrayEntry {
        std::vector<Value> items;
        bool gc_marked = false;
    };

    std::vector<ArrayEntry> arrays_;
    std::vector<uint32_t> free_slots_;
};

} // namespace nightscript
} // namespace nightforge