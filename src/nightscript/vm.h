#pragma once
#include "value.h"
#include <functional>
#include <memory>
#include <unordered_map>
#include <vector>

// Enable computed goto optimization for GCC/Clang
#if defined(__GNUC__) || defined(__clang__)
    #define USE_COMPUTED_GOTO 1
#else
    #define USE_COMPUTED_GOTO 0
#endif

namespace nightforge {
namespace nightscript {

class HostEnvironment; // forward declare

// Host function signature
using HostFunction = std::function<Value(const std::vector<Value>&)>;

// VM execution result
enum class VMResult {
    OK,
    COMPILE_ERROR,
    RUNTIME_ERROR
};

// stolen from lua 5.4 lol
struct CallFrame {
    Value* base;
    Value* top;
    const uint8_t* return_ip;
    const Chunk* chunk;
};

class VM {
public:
    VM(HostEnvironment* host_env = nullptr);
    ~VM();
    
    // Execute bytecode chunk
    VMResult execute(const Chunk& chunk);
    VMResult execute(const Chunk& chunk, const Chunk* parent_chunk);
    
    // Assign a host environment
    void set_host_environment(HostEnvironment* env) { host_env_ = env; }
    
    // String and buffer management
    StringTable& strings() { return strings_; }
    BufferTable& buffers() { return buffers_; }
    ArrayTable& arrays() { return arrays_; }
    
    // Garbage collection
    void collect_garbage(const Chunk* active_chunk = nullptr);
    void mark_reachable_strings(const Chunk* active_chunk = nullptr);
    
    // Global variables
    void set_global(const std::string& name, const Value& value);
    Value get_global(const std::string& name);
    
    // Debug
    void print_stack();
    
    // Performance monitoring
    void reset_stats() { 
        stats = Stats{}; // Reset to default values
    }
    
    // Performance counters
    struct Stats {
        size_t gc_collections = 0;
        size_t bytes_allocated = 0;
        size_t bytes_freed = 0;
        double total_gc_time = 0.0;
        std::array<uint64_t, 256> op_counts = {};
    } stats;
    
private:
    // Helper for fast string conversion
    std::string value_to_string(const Value& val);
    
#if USE_COMPUTED_GOTO

#endif

    std::vector<CallFrame> call_frames_;
    CallFrame* current_frame_;

    // Call frame helpers (unified stack)
    void push_call_frame(const Chunk* chunk, uint8_t arg_count);
    void pop_call_frame();
    Value* get_local(uint8_t slot);  // Direct shot
    
private:
    static constexpr size_t STACK_MAX = 262144; // Increased to 256K for deep recursion support
    static constexpr size_t GC_THRESHOLD = 1024 * 1024; // 1MB threshold for GC
    
    Value stack_[STACK_MAX];
    Value* stack_top_;
    
    std::unordered_map<std::string, Value> globals_;
    std::unordered_map<uint32_t, Value> globals_by_id_; // fast path for interned globals
    // host functions are provided via HostEnvironment (host_env_)
    StringTable strings_;
    BufferTable buffers_;
    ArrayTable array_table_;
    // Back-compat alias for naming consistency
    ArrayTable& arrays_ = array_table_;
    
    size_t bytes_allocated_since_gc_ = 0;
    
    std::vector<Value> tmp_args_;
    HostEnvironment* host_env_ = nullptr;
    
    // Stack operations
    void push(const Value& value);
    Value pop();
    Value peek(int distance = 0);
    void reset_stack();

    // Error state
    bool has_runtime_error_ = false;
    
    // Execution
    VMResult run(const Chunk& chunk, const Chunk* parent_chunk);
    uint8_t read_byte(const uint8_t*& ip);
    Value read_constant(const Chunk& chunk, const uint8_t*& ip);
    
    // Binary operations
    bool binary_op(OpCode op);
    
    // Debug
    void runtime_error(const char* format, ...);
};

} // namespace nightscript
} // namespace nightforge