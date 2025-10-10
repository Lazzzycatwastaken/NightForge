#pragma once
#include "../host_api.h"
#include "../vm.h"
#include <string>
#include <vector>

namespace nightforge {
namespace nightscript {
namespace stdlib {

// Register all file I/O functions
void register_file_functions(HostEnvironment* env, VM* vm);

// File operations
Value file_exists(VM* vm, const std::vector<Value>& args);
Value file_read(VM* vm, const std::vector<Value>& args);
Value file_write(VM* vm, const std::vector<Value>& args);
Value file_append(VM* vm, const std::vector<Value>& args);
Value file_lines(VM* vm, const std::vector<Value>& args);
Value file_delete(VM* vm, const std::vector<Value>& args);

// Directory operations
Value dir_exists(VM* vm, const std::vector<Value>& args);
Value dir_create(VM* vm, const std::vector<Value>& args);
Value dir_list(VM* vm, const std::vector<Value>& args);

} // namespace stdlib
} // namespace nightscript
} // namespace nightforge
