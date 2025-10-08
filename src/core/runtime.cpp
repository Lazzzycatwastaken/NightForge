#include "runtime.h"
#include "../nightscript/compiler.h"
#include <iostream>
#include <fstream>
#include <algorithm>

namespace nightforge {
Runtime::Runtime()
    : vm_(std::make_unique<nightscript::VM>()),
      external_vm_(nullptr),
      host_env_(nullptr),
      has_error_(false) {
}

Runtime::Runtime(nightscript::VM* vm)
    : vm_(nullptr),
      external_vm_(vm),
      host_env_(nullptr),
      has_error_(false) {
    // Don't create internal VM when using external one
}

Runtime::~Runtime() = default;

bool Runtime::execute_bytecode_file(const std::string& bytecode_path) {
    has_error_ = false;
    error_message_.clear();
    
    // Load bytecode directly so no compilation phase
    nightscript::Chunk chunk;
    nightscript::StringTable strings;
    nightscript::Compiler compiler;
    
    if (!compiler.load_cached_bytecode(bytecode_path, chunk, strings)) {
        has_error_ = true;
        error_message_ = "Failed to load bytecode file: " + bytecode_path;
        return false;
    }
    
    return execute_bytecode(chunk, strings);
}

bool Runtime::execute_bytecode(const nightscript::Chunk& chunk, const nightscript::StringTable& strings) {
    has_error_ = false;
    error_message_.clear();
    
    nightscript::VM* vm = get_vm();
    if (!vm) {
        has_error_ = true;
        error_message_ = "No VM available";
        return false;
    }
    
    vm->strings() = strings;
    
    nightscript::VMResult result = vm->execute(chunk);
    handle_runtime_result(result);
    
    return !has_error_;
}

nightscript::VMResult Runtime::execute_bytecode(const nightscript::Chunk& chunk) {
    has_error_ = false;
    error_message_.clear();
    
    nightscript::VM* vm = get_vm();
    if (!vm) {
        has_error_ = true;
        error_message_ = "No VM available";
        return nightscript::VMResult::RUNTIME_ERROR;
    }
    
    return vm->execute(chunk);
}

void Runtime::set_host_environment(nightscript::HostEnvironment* env) {
    host_env_ = env;
    nightscript::VM* vm = get_vm();
    if (vm) {
        vm->set_host_environment(env);
    }
}

// void Runtime::print_performance_stats() const {
//     std::cout << "--- Opcode hotspots ---" << std::endl;
    
//     nightscript::VM* vm = external_vm_ ? external_vm_ : vm_.get();
//     if (!vm) {
//         std::cout << "No VM available for stats" << std::endl;
//         return;
//     }
    
//     std::vector<std::pair<int, uint64_t>> ops;
//     for (int i = 0; i < 256; ++i) {
//         uint64_t count = vm->stats.op_counts[i];
//         if (count > 0) {
//             ops.emplace_back(i, count);
//         }
//     }
    
//     std::sort(ops.begin(), ops.end(), 
//               [](const auto& a, const auto& b) { return a.second > b.second; });
    
    // int shown = 0;
    // for (const auto& op : ops) {
    //     if (shown++ >= 10) break;
    //     std::cout << "op=" << op.first << " count=" << op.second << std::endl;
    // }
// }

void Runtime::reset_stats() {
    nightscript::VM* vm = get_vm();
    if (vm) {
        vm->reset_stats();
    }
}

void Runtime::handle_runtime_result(nightscript::VMResult result) {
    switch (result) {
        case nightscript::VMResult::OK:
            // Success - no error
            break;
        case nightscript::VMResult::COMPILE_ERROR:
            has_error_ = true;
            error_message_ = "Compilation error during execution";
            break;
        case nightscript::VMResult::RUNTIME_ERROR:
            has_error_ = true;
            error_message_ = "Runtime error during execution";
            break;
    }
}

} // namespace nightforge