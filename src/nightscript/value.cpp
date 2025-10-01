#include "value.h"
#include <iostream>

namespace nightforge {
namespace nightscript {

void Chunk::write_byte(uint8_t byte, int line) {
    code_.push_back(byte);
    lines_.push_back(line);
}

void Chunk::write_constant(const Value& value, int line) {
    size_t index = add_constant(value);
    if (index < 256) {
        write_byte(static_cast<uint8_t>(OpCode::OP_CONSTANT), line);
        write_byte(static_cast<uint8_t>(index), line);
    } else {
        // TODO: handle more than 256 constants (for now just error out)
        std::cerr << "Too many constants in chunk!" << std::endl;
    }
}

void Chunk::patch_byte(size_t index, uint8_t byte) {
    if (index < code_.size()) {
        code_[index] = byte;
    }
}

size_t Chunk::add_constant(const Value& value) {
    constants_.push_back(value);
    return constants_.size() - 1;
}

Value Chunk::get_constant(size_t index) const {
    if (index >= constants_.size()) {
        return Value::nil(); // safe fallback
    }
    return constants_[index];
}

size_t Chunk::add_function(const Chunk& function_chunk, const std::string& param_name, const std::string& function_name) {
    functions_.push_back(function_chunk);
    function_params_.push_back(param_name);
    function_names_.push_back(function_name);
    return functions_.size() - 1;
}

const Chunk& Chunk::get_function(size_t index) const {
    return functions_[index];
}

const std::string& Chunk::get_function_param_name(size_t index) const {
    return function_params_[index];
}

ssize_t Chunk::get_function_index(const std::string& name) const {
    for (size_t i = 0; i < function_names_.size(); ++i) {
        if (function_names_[i] == name) return static_cast<ssize_t>(i);
    }
    return -1;
}

// String table implementation
uint32_t StringTable::intern(const std::string& str) {
    auto it = string_to_id_.find(str);
    if (it != string_to_id_.end()) {
        return it->second; // already interned
    }
    
    uint32_t id = static_cast<uint32_t>(strings_.size());
    strings_.push_back(str);
    string_to_id_[str] = id;
    return id;
}

const std::string& StringTable::get_string(uint32_t id) const {
    static const std::string empty_string = "";
    
    if (id >= strings_.size()) {
        return empty_string; // safe fallback
    }
    return strings_[id];
}

} // namespace nightscript
} // namespace nightforge