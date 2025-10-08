#pragma once
#include "../nightscript/vm.h"
#include "../nightscript/value.h"
#include <string>
#include <memory>

namespace nightforge {

/**
 * NightForge Runtime - Pure execution environment for compiled bytecode
 */
class Runtime {
public:
    Runtime();
    explicit Runtime(nightscript::VM* vm);
    ~Runtime();
    
    bool execute_bytecode_file(const std::string& bytecode_path);
    bool execute_bytecode(const nightscript::Chunk& chunk, const nightscript::StringTable& strings);
    
    nightscript::VMResult execute_bytecode(const nightscript::Chunk& chunk);
    
    void set_host_environment(nightscript::HostEnvironment* env);
    
    void print_performance_stats() const;
    void reset_stats();
    
    bool has_error() const { return has_error_; }
    const std::string& get_error_message() const { return error_message_; }

private:
    std::unique_ptr<nightscript::VM> vm_;
    nightscript::VM* external_vm_;
    nightscript::HostEnvironment* host_env_;
    bool has_error_;
    std::string error_message_;
    
    nightscript::VM* get_vm() { return external_vm_ ? external_vm_ : vm_.get(); }
    
    void handle_runtime_result(nightscript::VMResult result);
};

} // namespace nightforge