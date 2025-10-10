#pragma once
#include "../host_api.h"
#include "../vm.h"
#include <string>
#include <vector>

namespace nightforge {
namespace nightscript {
namespace stdlib {

// Register all string manipulation functions
void register_string_functions(HostEnvironment* env, VM* vm);

// String functions
Value string_split(VM* vm, const std::vector<Value>& args);
Value string_join(VM* vm, const std::vector<Value>& args);
Value string_replace(VM* vm, const std::vector<Value>& args);
Value string_substring(VM* vm, const std::vector<Value>& args);
Value string_uppercase(VM* vm, const std::vector<Value>& args);
Value string_lowercase(VM* vm, const std::vector<Value>& args);
Value string_trim(VM* vm, const std::vector<Value>& args);
Value string_starts_with(VM* vm, const std::vector<Value>& args);
Value string_ends_with(VM* vm, const std::vector<Value>& args);
Value string_contains(VM* vm, const std::vector<Value>& args);
Value string_find(VM* vm, const std::vector<Value>& args);
Value string_char_at(VM* vm, const std::vector<Value>& args);
Value string_repeat(VM* vm, const std::vector<Value>& args);

} // namespace stdlib
} // namespace nightscript
} // namespace nightforge
