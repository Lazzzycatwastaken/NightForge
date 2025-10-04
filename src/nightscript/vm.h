#pragma once
#include "value.h"
#include <functional>
#include <memory>
#include <unordered_map>
#include <vector>

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
    
    // Performance counters
    struct Stats {
        size_t gc_collections = 0;
        size_t bytes_allocated = 0;
        size_t bytes_freed = 0;
        double total_gc_time = 0.0;
    } stats;
    
    // Global variables
    void set_global(const std::string& name, const Value& value);
    Value get_global(const std::string& name);
    
    // Debug
    void print_stack();
    
private:
    static constexpr size_t STACK_MAX = 16384; // those who know
    static constexpr size_t GC_THRESHOLD = 1024 * 1024; // 1MB threshold for GC
    
    Value stack_[STACK_MAX];
    Value* stack_top_;
    
    std::unordered_map<std::string, Value> globals_;
    // host functions are provided via HostEnvironment (host_env_)
    StringTable strings_;
    BufferTable buffers_;
    
    // Bytecode cache
    struct BytecodeCache {
        std::unordered_map<std::string, std::shared_ptr<Chunk>> cached_chunks;
        std::unordered_map<std::string, uint64_t> file_timestamps;
    } cache_;
    
    // GC state
    std::vector<uint32_t> reachable_strings_;
    std::vector<uint32_t> reachable_buffers_;
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
    VMResult run(const Chunk& chunk);
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