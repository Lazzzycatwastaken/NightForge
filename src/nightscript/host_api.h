#pragma once
#include "value.h"
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace nightforge {
namespace nightscript {

using HostFunction = std::function<Value(const std::vector<Value>&)>;

// Abstract host environment interface (engine should implement this to expose host functions)
class HostEnvironment {
public:
    virtual ~HostEnvironment() = default;
    // Register a host function into the host environment
    virtual void register_function(const std::string& name, HostFunction func) = 0;
    // Call a host function by (lowercased) name Return std::nullopt if not found
    virtual std::optional<Value> call_host(const std::string& name, const std::vector<Value>& args) = 0;
};

} // namespace nightscript
} // namespace nightforge
