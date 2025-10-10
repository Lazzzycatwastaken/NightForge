#include "string.h"
#include <algorithm>
#include <cctype>
#include <sstream>
#include <iostream>

namespace nightforge {
namespace nightscript {
namespace stdlib {

static std::string value_to_string(VM* vm, const Value& val) {
    switch (val.type()) {
        case ValueType::STRING_ID:
            return vm->strings().get_string(val.as_string_id());
        case ValueType::STRING_BUFFER:
            return vm->buffers().get_buffer(val.as_buffer_id());
        case ValueType::INT:
            return std::to_string(val.as_integer());
        case ValueType::FLOAT:
            return std::to_string(val.as_floating());
        case ValueType::BOOL:
            return val.as_boolean() ? "true" : "false";
        case ValueType::NIL:
            return "nil";
        default:
            return "";
    }
}

Value string_split(VM* vm, const std::vector<Value>& args) {
    if (args.size() != 2) {
        std::cerr << "split: expected (string, delimiter)" << std::endl;
        return Value::nil();
    }
    
    std::string str = value_to_string(vm, args[0]);
    std::string delim = value_to_string(vm, args[1]);
    
    if (delim.empty()) {
        std::cerr << "split: delimiter cannot be empty" << std::endl;
        return Value::nil();
    }
    
    uint32_t array_id = vm->arrays().create();
    
    size_t start = 0;
    size_t end = str.find(delim);
    
    while (end != std::string::npos) {
        std::string token = str.substr(start, end - start);
        uint32_t str_id = vm->strings().intern(token);
        vm->arrays().push_back(array_id, Value::string_id(str_id));
        
        start = end + delim.length();
        end = str.find(delim, start);
    }
    
    std::string token = str.substr(start);
    uint32_t str_id = vm->strings().intern(token);
    vm->arrays().push_back(array_id, Value::string_id(str_id));
    
    return Value::array_id(array_id);
}

Value string_join(VM* vm, const std::vector<Value>& args) {
    if (args.size() != 2 || args[0].type() != ValueType::ARRAY) {
        std::cerr << "join: expected (array, separator)" << std::endl;
        return Value::nil();
    }
    
    uint32_t array_id = args[0].as_array_id();
    std::string separator = value_to_string(vm, args[1]);
    
    size_t len = vm->arrays().length(array_id);
    if (len == 0) {
        return Value::string_id(vm->strings().intern(""));
    }
    
    std::string result;
    for (size_t i = 0; i < len; ++i) {
        if (i > 0) result += separator;
        Value elem = vm->arrays().get(array_id, i);
        result += value_to_string(vm, elem);
    }
    
    return Value::string_id(vm->strings().intern(result));
}

Value string_replace(VM* vm, const std::vector<Value>& args) {
    if (args.size() != 3) {
        std::cerr << "replace: expected (string, old, new)" << std::endl;
        return Value::nil();
    }
    
    std::string str = value_to_string(vm, args[0]);
    std::string old_str = value_to_string(vm, args[1]);
    std::string new_str = value_to_string(vm, args[2]);
    
    if (old_str.empty()) {
        return Value::string_id(vm->strings().intern(str));
    }
    
    size_t pos = 0;
    while ((pos = str.find(old_str, pos)) != std::string::npos) {
        str.replace(pos, old_str.length(), new_str);
        pos += new_str.length();
    }
    
    return Value::string_id(vm->strings().intern(str));
}

Value string_substring(VM* vm, const std::vector<Value>& args) {
    if (args.size() < 2 || args.size() > 3) {
        std::cerr << "substring: expected (string, start[, end])" << std::endl;
        return Value::nil();
    }
    
    std::string str = value_to_string(vm, args[0]);
    
    int64_t start = 0;
    if (args[1].type() == ValueType::INT) {
        start = args[1].as_integer();
    } else {
        std::cerr << "substring: start must be an integer" << std::endl;
        return Value::nil();
    }
    
    if (start < 0) start = str.length() + start;
    if (start < 0) start = 0;
    if (start >= static_cast<int64_t>(str.length())) {
        return Value::string_id(vm->strings().intern(""));
    }
    
    if (args.size() == 2) {
        std::string result = str.substr(start);
        return Value::string_id(vm->strings().intern(result));
    }
    
    int64_t end = 0;
    if (args[2].type() == ValueType::INT) {
        end = args[2].as_integer();
    } else {
        std::cerr << "substring: end must be an integer" << std::endl;
        return Value::nil();
    }
    
    if (end < 0) end = str.length() + end;
    if (end > static_cast<int64_t>(str.length())) end = str.length();
    if (end <= start) {
        return Value::string_id(vm->strings().intern(""));
    }
    
    std::string result = str.substr(start, end - start);
    return Value::string_id(vm->strings().intern(result));
}

Value string_uppercase(VM* vm, const std::vector<Value>& args) {
    if (args.size() != 1) {
        std::cerr << "uppercase: expected (string)" << std::endl;
        return Value::nil();
    }
    
    std::string str = value_to_string(vm, args[0]);
    std::transform(str.begin(), str.end(), str.begin(), ::toupper);
    return Value::string_id(vm->strings().intern(str));
}

Value string_lowercase(VM* vm, const std::vector<Value>& args) {
    if (args.size() != 1) {
        std::cerr << "lowercase: expected (string)" << std::endl;
        return Value::nil();
    }
    
    std::string str = value_to_string(vm, args[0]);
    std::transform(str.begin(), str.end(), str.begin(), ::tolower);
    return Value::string_id(vm->strings().intern(str));
}

Value string_trim(VM* vm, const std::vector<Value>& args) {
    if (args.size() != 1) {
        std::cerr << "trim: expected (string)" << std::endl;
        return Value::nil();
    }
    
    std::string str = value_to_string(vm, args[0]);
    
    size_t start = 0;
    while (start < str.length() && std::isspace(str[start])) {
        ++start;
    }
    
    size_t end = str.length();
    while (end > start && std::isspace(str[end - 1])) {
        --end;
    }
    
    std::string result = str.substr(start, end - start);
    return Value::string_id(vm->strings().intern(result));
}

Value string_starts_with(VM* vm, const std::vector<Value>& args) {
    if (args.size() != 2) {
        std::cerr << "starts_with: expected (string, prefix)" << std::endl;
        return Value::nil();
    }
    
    std::string str = value_to_string(vm, args[0]);
    std::string prefix = value_to_string(vm, args[1]);
    
    bool result = str.size() >= prefix.size() && 
                  str.compare(0, prefix.size(), prefix) == 0;
    return Value::boolean(result);
}

Value string_ends_with(VM* vm, const std::vector<Value>& args) {
    if (args.size() != 2) {
        std::cerr << "ends_with: expected (string, suffix)" << std::endl;
        return Value::nil();
    }
    
    std::string str = value_to_string(vm, args[0]);
    std::string suffix = value_to_string(vm, args[1]);
    
    bool result = str.size() >= suffix.size() && 
                  str.compare(str.size() - suffix.size(), suffix.size(), suffix) == 0;
    return Value::boolean(result);
}

Value string_contains(VM* vm, const std::vector<Value>& args) {
    if (args.size() != 2) {
        std::cerr << "contains: expected (string, substring)" << std::endl;
        return Value::nil();
    }
    
    std::string str = value_to_string(vm, args[0]);
    std::string substring = value_to_string(vm, args[1]);
    
    bool result = str.find(substring) != std::string::npos;
    return Value::boolean(result);
}

Value string_find(VM* vm, const std::vector<Value>& args) {
    if (args.size() != 2) {
        std::cerr << "find: expected (string, substring)" << std::endl;
        return Value::nil();
    }
    
    std::string str = value_to_string(vm, args[0]);
    std::string substring = value_to_string(vm, args[1]);
    
    size_t pos = str.find(substring);
    if (pos == std::string::npos) {
        return Value::integer(-1);
    }
    return Value::integer(static_cast<int64_t>(pos));
}

Value string_char_at(VM* vm, const std::vector<Value>& args) {
    if (args.size() != 2) {
        std::cerr << "char_at: expected (string, index)" << std::endl;
        return Value::nil();
    }
    
    std::string str = value_to_string(vm, args[0]);
    
    int64_t index = 0;
    if (args[1].type() == ValueType::INT) {
        index = args[1].as_integer();
    } else {
        std::cerr << "char_at: index must be an integer" << std::endl;
        return Value::nil();
    }
    
    if (index < 0) index = str.length() + index;
    
    if (index < 0 || index >= static_cast<int64_t>(str.length())) {
        return Value::nil();
    }
    
    std::string result(1, str[index]);
    return Value::string_id(vm->strings().intern(result));
}

Value string_repeat(VM* vm, const std::vector<Value>& args) {
    if (args.size() != 2) {
        std::cerr << "repeat: expected (string, count)" << std::endl;
        return Value::nil();
    }
    
    std::string str = value_to_string(vm, args[0]);
    
    int64_t count = 0;
    if (args[1].type() == ValueType::INT) {
        count = args[1].as_integer();
    } else {
        std::cerr << "repeat: count must be an integer" << std::endl;
        return Value::nil();
    }
    
    if (count < 0) count = 0;
    if (count > 10000) {
        std::cerr << "repeat: count too large (max 10000)" << std::endl;
        return Value::nil();
    }
    
    std::string result;
    result.reserve(str.length() * count);
    for (int64_t i = 0; i < count; ++i) {
        result += str;
    }
    
    return Value::string_id(vm->strings().intern(result));
}

void register_string_functions(HostEnvironment* env, VM* vm) {
    env->register_function("split", [vm](const std::vector<Value>& args) {
        return string_split(vm, args);
    });
    
    env->register_function("join", [vm](const std::vector<Value>& args) {
        return string_join(vm, args);
    });
    
    env->register_function("replace", [vm](const std::vector<Value>& args) {
        return string_replace(vm, args);
    });
    
    env->register_function("substring", [vm](const std::vector<Value>& args) {
        return string_substring(vm, args);
    });
    
    env->register_function("uppercase", [vm](const std::vector<Value>& args) {
        return string_uppercase(vm, args);
    });
    
    env->register_function("lowercase", [vm](const std::vector<Value>& args) {
        return string_lowercase(vm, args);
    });
    
    env->register_function("trim", [vm](const std::vector<Value>& args) {
        return string_trim(vm, args);
    });
    
    env->register_function("starts_with", [vm](const std::vector<Value>& args) {
        return string_starts_with(vm, args);
    });
    
    env->register_function("ends_with", [vm](const std::vector<Value>& args) {
        return string_ends_with(vm, args);
    });
    
    env->register_function("contains", [vm](const std::vector<Value>& args) {
        return string_contains(vm, args);
    });
    
    env->register_function("find", [vm](const std::vector<Value>& args) {
        return string_find(vm, args);
    });
    
    env->register_function("char_at", [vm](const std::vector<Value>& args) {
        return string_char_at(vm, args);
    });
    
    env->register_function("repeat", [vm](const std::vector<Value>& args) {
        return string_repeat(vm, args);
    });
}

} // namespace stdlib
} // namespace nightscript
} // namespace nightforge
