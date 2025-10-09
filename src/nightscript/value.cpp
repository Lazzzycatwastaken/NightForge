#include "value.h"
#include <iostream>
#include <limits>

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

size_t Chunk::add_function(const Chunk& function_chunk, const std::vector<std::string>& param_names, const std::string& function_name) {
    functions_.push_back(function_chunk);
    function_params_.push_back(param_names);
    function_locals_.push_back(std::vector<std::string>()); // empty locals by default
    function_names_.push_back(function_name);
    return functions_.size() - 1;
}

size_t Chunk::add_function(const Chunk& function_chunk, const std::vector<std::string>& param_names, const std::vector<std::string>& local_names, const std::string& function_name) {
    functions_.push_back(function_chunk);
    function_params_.push_back(param_names);
    function_locals_.push_back(local_names);
    function_names_.push_back(function_name);
    return functions_.size() - 1;
}

const Chunk& Chunk::get_function(size_t index) const {
    return functions_[index];
}

const std::vector<std::string>& Chunk::get_function_param_names(size_t index) const {
    return function_params_[index];
}

const std::vector<std::string>& Chunk::get_function_local_names(size_t index) const {
    return function_locals_[index];
}

ssize_t Chunk::get_function_index(const std::string& name) const {
    for (size_t i = 0; i < function_names_.size(); ++i) {
        if (function_names_[i] == name) return static_cast<ssize_t>(i);
    }
    return -1;
}

size_t Chunk::function_count() const {
    return function_names_.size();
}

const std::string& Chunk::function_name(size_t index) const {
    return function_names_[index];
}

void Chunk::add_function_name(const std::string& name) {
    function_names_.push_back(name);
}

void Chunk::add_function_name_to_child(size_t child_index, const std::string& name) {
    if (child_index < functions_.size()) {
        functions_[child_index].function_names_.push_back(name);
    }
}

// Optimized String table implementation
uint32_t StringTable::intern(const std::string& str) {
    auto it = string_to_id_.find(str);
    if (it != string_to_id_.end()) {
        strings_[it->second].ref_count++;
        return it->second; // already interned
    }
    
    uint32_t id;
    if (!free_slots_.empty()) {
        id = free_slots_.back();
        free_slots_.pop_back();
        strings_[id] = {str, false, 1};
    } else {
        id = static_cast<uint32_t>(strings_.size());
        strings_.push_back({str, false, 1});
    }
    
    string_to_id_[str] = id;
    return id;
}

uint32_t StringTable::find_id(const std::string& str) const {
    auto it = string_to_id_.find(str);
    if (it == string_to_id_.end()) return 0xFFFFFFFFu;
    return it->second;
}

const std::string& StringTable::get_string(uint32_t id) const {
    static const std::string empty_string = "";
    
    if (id >= strings_.size() || strings_[id].str.empty()) {
        return empty_string;
    }
    return strings_[id].str;
}

void StringTable::mark_string_reachable(uint32_t id) {
    if (id < strings_.size()) {
        strings_[id].gc_marked = true;
    }
}

void StringTable::sweep_unreachable_strings() {
    for (uint32_t i = 0; i < strings_.size(); i++) {
        if (!strings_[i].gc_marked && !strings_[i].str.empty()) {
            // String is unreachable free it
            string_to_id_.erase(strings_[i].str);
            strings_[i].str.clear();
            strings_[i].ref_count = 0;
            free_slots_.push_back(i);
        }
    }
}

void StringTable::clear_gc_marks() {
    for (auto& entry : strings_) {
        entry.gc_marked = false;
    }
}

size_t StringTable::memory_usage() const {
    size_t total = 0;
    for (const auto& entry : strings_) {
        total += entry.str.capacity();
    }
    return total;
}

uint32_t StringTable::concat_strings(uint32_t id1, uint32_t id2) {
    const std::string& str1 = get_string(id1);
    const std::string& str2 = get_string(id2);
    return intern(str1 + str2);
}

uint32_t StringTable::concat_string_literal(uint32_t id, const std::string& literal) {
    const std::string& str = get_string(id);
    return intern(str + literal);
}

uint32_t StringTable::append_to_interned(uint32_t left_id, const std::string& suffix) {
    if (left_id >= strings_.size()) return concat_string_literal(left_id, suffix);

    auto &entry = strings_[left_id];
    if (entry.ref_count != 1) {
        return concat_string_literal(left_id, suffix);
    }

    string_to_id_.erase(entry.str);

    entry.str.reserve(entry.str.size() + suffix.size());
    entry.str.append(suffix);

    string_to_id_[entry.str] = left_id;
    return left_id;
}

uint32_t StringTable::append_id_to_interned(uint32_t left_id, uint32_t right_id) {
    if (right_id >= strings_.size()) return left_id;
    const std::string& rhs = get_string(right_id);
    return append_to_interned(left_id, rhs);
}

// BufferTable implementation
uint32_t BufferTable::create_from_two(const std::string& a, const std::string& b) {
    uint32_t id;
    if (!free_slots_.empty()) {
        id = free_slots_.back(); free_slots_.pop_back();
        buffers_[id] = {a + b, false, 1};
    } else {
        id = static_cast<uint32_t>(buffers_.size());
        buffers_.push_back({a + b, false, 1});
    }
    return id;
}

uint32_t BufferTable::create_from_ids(uint32_t left_id, uint32_t right_id, const StringTable& strings) {
    std::string a = (left_id < strings.string_count()) ? strings.get_string(left_id) : std::string();
    std::string b = (right_id < strings.string_count()) ? strings.get_string(right_id) : std::string();
    return create_from_two(a, b);
}

const std::string& BufferTable::get_buffer(uint32_t id) const {
    static const std::string empty = "";
    if (id >= buffers_.size()) return empty;
    return buffers_[id].str;
}

uint32_t BufferTable::append_literal(uint32_t id, const std::string& suffix) {
    if (id >= buffers_.size()) return id;
    auto &entry = buffers_[id];
    entry.str.append(suffix);
    return id;
}

uint32_t BufferTable::append_id(uint32_t left_id, uint32_t right_id, const StringTable& strings) {
    if (right_id >= strings.string_count()) return left_id;
    const std::string& rhs = strings.get_string(right_id);
    return append_literal(left_id, rhs);
}

void BufferTable::reserve(uint32_t id, size_t capacity) {
    if (id >= buffers_.size()) return;
    buffers_[id].str.reserve(capacity);
}

void BufferTable::mark_buffer_reachable(uint32_t id) {
    if (id < buffers_.size()) buffers_[id].gc_marked = true;
}

void BufferTable::sweep_unreachable_buffers() {
    for (uint32_t i = 0; i < buffers_.size(); ++i) {
        if (!buffers_[i].gc_marked && !buffers_[i].str.empty()) {
            buffers_[i].str.clear();
            buffers_[i].ref_count = 0;
            free_slots_.push_back(i);
        }
    }
}

void BufferTable::clear_gc_marks() {
    for (auto &b : buffers_) b.gc_marked = false;
}

size_t BufferTable::memory_usage() const {
    size_t total = 0;
    for (const auto &b : buffers_) total += b.str.capacity();
    return total;
}

// ArrayTable implementation
uint32_t ArrayTable::create(size_t reserve) {
    uint32_t id;
    if (!free_slots_.empty()) {
        id = free_slots_.back();
        free_slots_.pop_back();
        arrays_[id] = ArrayEntry{};
        if (reserve) arrays_[id].items.reserve(reserve);
    } else {
        id = static_cast<uint32_t>(arrays_.size());
        arrays_.push_back(ArrayEntry{});
        if (reserve) arrays_.back().items.reserve(reserve);
    }
    return id;
}

size_t ArrayTable::length(uint32_t id) const {
    if (id >= arrays_.size()) return 0;
    return arrays_[id].items.size();
}

void ArrayTable::push_back(uint32_t id, const Value& v) {
    if (id >= arrays_.size()) return;
    arrays_[id].items.push_back(v);
}

Value ArrayTable::pop_back(uint32_t id) {
    if (id >= arrays_.size()) return Value::nil();
    auto &vec = arrays_[id].items;
    if (vec.empty()) return Value::nil();
    Value v = vec.back();
    vec.pop_back();
    return v;
}

static inline bool normalize_index(ssize_t& idx, size_t size) {
    if (idx < 0) {
        // Negative index from end (Python)
        ssize_t from_end = static_cast<ssize_t>(size) + idx; // idx is negative
        if (from_end < 0) return false;
        idx = from_end;
    }
    return static_cast<size_t>(idx) < size;
}

Value ArrayTable::get(uint32_t id, ssize_t index) const {
    if (id >= arrays_.size()) return Value::nil();
    const auto &vec = arrays_[id].items;
    if (!normalize_index(index, vec.size())) return Value::nil();
    return vec[static_cast<size_t>(index)];
}

void ArrayTable::set(uint32_t id, ssize_t index, const Value& v) {
    if (id >= arrays_.size()) return;
    auto &vec = arrays_[id].items;
    ssize_t idx = index;
    if (idx < 0) {
        if (!normalize_index(idx, vec.size())) return;
    }
    size_t uidx = static_cast<size_t>(idx);
    if (uidx == vec.size()) {
        vec.push_back(v);
        return;
    }
    if (uidx < vec.size()) {
        vec[uidx] = v;
    }
}

Value ArrayTable::remove_at(uint32_t id, ssize_t index) {
    if (id >= arrays_.size()) return Value::nil();
    auto &vec = arrays_[id].items;
    if (!normalize_index(index, vec.size())) return Value::nil();
    size_t uidx = static_cast<size_t>(index);
    Value v = vec[uidx];
    vec.erase(vec.begin() + static_cast<ssize_t>(uidx));
    return v;
}

void ArrayTable::clear(uint32_t id) {
    if (id >= arrays_.size()) return;
    arrays_[id].items.clear();
}

void ArrayTable::mark_array_reachable(uint32_t id) {
    if (id < arrays_.size()) arrays_[id].gc_marked = true;
}

void ArrayTable::clear_gc_marks() {
    for (auto &a : arrays_) a.gc_marked = false;
}

void ArrayTable::for_each(uint32_t id, const std::function<void(const Value&)>& fn) const {
    if (id >= arrays_.size()) return;
    const auto &vec = arrays_[id].items;
    for (const auto &val : vec) fn(val);
}


} // namespace nightscript
} // namespace nightforge
