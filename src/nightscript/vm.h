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
    
    // Garbage collection
    void collect_garbage(const Chunk* active_chunk = nullptr);
    void mark_reachable_strings(const Chunk* active_chunk = nullptr);
    
    // Global variables
    void set_global(const std::string& name, const Value& value);
    Value get_global(const std::string& name);
    
    // Debug
    void print_stack();
    
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

    std::vector<Value> param_stack_;
    std::vector<std::unordered_map<std::string, size_t>> local_frames_;
    std::vector<size_t> local_frame_bases_; // base index per frame

    // Local frame helpers
    void push_local_frame(const std::vector<std::string>& locals_combined, const std::vector<Value>& args);
    void pop_local_frame();
    bool local_lookup(const std::string& name, Value& out) const;
    
private:
    static constexpr size_t STACK_MAX = 16384; // those who know
    static constexpr size_t GC_THRESHOLD = 1024 * 1024; // 1MB threshold for GC
    
    Value stack_[STACK_MAX];
    Value* stack_top_;
    
    std::unordered_map<std::string, Value> globals_;
    std::unordered_map<uint32_t, Value> globals_by_id_; // fast path for interned globals
    // host functions are provided via HostEnvironment (host_env_)
    StringTable strings_;
    BufferTable buffers_;
    
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