#include "file.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <sys/stat.h>

#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#define stat _stat
#define mkdir(path, mode) _mkdir(path)
#else
#include <dirent.h>
#include <unistd.h>
#endif

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

static bool is_safe_path(const std::string& path) {
    if (path.empty()) return false;
    
    if (path.find("..") != std::string::npos) {
        std::cerr << "File I/O: Path traversal not allowed (..)" << std::endl;
        return false;
    }
    
    #ifndef _WIN32
    if (path[0] == '/') {
        std::cerr << "File I/O: Absolute paths not allowed" << std::endl;
        return false;
    }
    #else
    // On Windows reject paths with drive letters (C:\, etc)
    if (path.length() >= 2 && path[1] == ':') {
        std::cerr << "File I/O: Absolute paths not allowed" << std::endl;
        return false;
    }
    #endif
    
    return true;
}

Value file_exists(VM* vm, const std::vector<Value>& args) {
    if (args.size() != 1) {
        std::cerr << "file_exists: expected (path)" << std::endl;
        return Value::nil();
    }
    
    std::string path = value_to_string(vm, args[0]);
    if (!is_safe_path(path)) {
        return Value::boolean(false);
    }
    
    struct stat buffer;
    bool exists = (stat(path.c_str(), &buffer) == 0) && S_ISREG(buffer.st_mode);
    return Value::boolean(exists);
}

Value file_read(VM* vm, const std::vector<Value>& args) {
    if (args.size() != 1) {
        std::cerr << "file_read: expected (path)" << std::endl;
        return Value::nil();
    }
    
    std::string path = value_to_string(vm, args[0]);
    if (!is_safe_path(path)) {
        return Value::nil();
    }
    
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "file_read: cannot open file: " << path << std::endl;
        return Value::nil();
    }
    
    std::ostringstream ss;
    ss << file.rdbuf();
    std::string content = ss.str();
    file.close();
    
    if (content.size() > 10 * 1024 * 1024) {
        std::cerr << "file_read: file too large (max 10MB): " << path << std::endl;
        return Value::nil();
    }
    
    return Value::string_id(vm->strings().intern(content));
}

Value file_write(VM* vm, const std::vector<Value>& args) {
    if (args.size() != 2) {
        std::cerr << "file_write: expected (path, content)" << std::endl;
        return Value::boolean(false);
    }
    
    std::string path = value_to_string(vm, args[0]);
    if (!is_safe_path(path)) {
        return Value::boolean(false);
    }
    
    std::string content = value_to_string(vm, args[1]);
    
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file.is_open()) {
        std::cerr << "file_write: cannot create file: " << path << std::endl;
        return Value::boolean(false);
    }
    
    file << content;
    file.close();
    
    return Value::boolean(true);
}

Value file_append(VM* vm, const std::vector<Value>& args) {
    if (args.size() != 2) {
        std::cerr << "file_append: expected (path, content)" << std::endl;
        return Value::boolean(false);
    }
    
    std::string path = value_to_string(vm, args[0]);
    if (!is_safe_path(path)) {
        return Value::boolean(false);
    }
    
    std::string content = value_to_string(vm, args[1]);
    
    std::ofstream file(path, std::ios::binary | std::ios::app);
    if (!file.is_open()) {
        std::cerr << "file_append: cannot open file: " << path << std::endl;
        return Value::boolean(false);
    }
    
    file << content;
    file.close();
    
    return Value::boolean(true);
}

Value file_lines(VM* vm, const std::vector<Value>& args) {
    if (args.size() != 1) {
        std::cerr << "file_lines: expected (path)" << std::endl;
        return Value::nil();
    }
    
    std::string path = value_to_string(vm, args[0]);
    if (!is_safe_path(path)) {
        return Value::nil();
    }
    
    std::ifstream file(path);
    if (!file.is_open()) {
        std::cerr << "file_lines: cannot open file: " << path << std::endl;
        return Value::nil();
    }
    
    uint32_t array_id = vm->arrays().create();
    std::string line;
    size_t line_count = 0;
    
    while (std::getline(file, line)) {
        if (++line_count > 100000) {
            std::cerr << "file_lines: too many lines (max 100k)" << std::endl;
            break;
        }
        
        uint32_t str_id = vm->strings().intern(line);
        vm->arrays().push_back(array_id, Value::string_id(str_id));
    }
    
    file.close();
    return Value::array_id(array_id);
}

Value file_delete(VM* vm, const std::vector<Value>& args) {
    if (args.size() != 1) {
        std::cerr << "file_delete: expected (path)" << std::endl;
        return Value::boolean(false);
    }
    
    std::string path = value_to_string(vm, args[0]);
    if (!is_safe_path(path)) {
        return Value::boolean(false);
    }
    
    bool success = (std::remove(path.c_str()) == 0);
    if (!success) {
        std::cerr << "file_delete: failed to delete: " << path << std::endl;
    }
    
    return Value::boolean(success);
}

Value dir_exists(VM* vm, const std::vector<Value>& args) {
    if (args.size() != 1) {
        std::cerr << "dir_exists: expected (path)" << std::endl;
        return Value::nil();
    }
    
    std::string path = value_to_string(vm, args[0]);
    if (!is_safe_path(path)) {
        return Value::boolean(false);
    }
    
    struct stat buffer;
    bool exists = (stat(path.c_str(), &buffer) == 0) && S_ISDIR(buffer.st_mode);
    return Value::boolean(exists);
}

Value dir_create(VM* vm, const std::vector<Value>& args) {
    if (args.size() != 1) {
        std::cerr << "dir_create: expected (path)" << std::endl;
        return Value::boolean(false);
    }
    
    std::string path = value_to_string(vm, args[0]);
    if (!is_safe_path(path)) {
        return Value::boolean(false);
    }
    
    #ifdef _WIN32
    bool success = (_mkdir(path.c_str()) == 0);
    #else
    bool success = (mkdir(path.c_str(), 0755) == 0);
    #endif
    
    if (!success) {
        std::cerr << "dir_create: failed to create directory: " << path << std::endl;
    }
    
    return Value::boolean(success);
}

Value dir_list(VM* vm, const std::vector<Value>& args) {
    if (args.size() != 1) {
        std::cerr << "dir_list: expected (path)" << std::endl;
        return Value::nil();
    }
    
    std::string path = value_to_string(vm, args[0]);
    if (!is_safe_path(path)) {
        return Value::nil();
    }
    
    uint32_t array_id = vm->arrays().create();
    
    #ifdef _WIN32
    WIN32_FIND_DATAA find_data;
    std::string search_path = path + "\\*";
    HANDLE hFind = FindFirstFileA(search_path.c_str(), &find_data);
    
    if (hFind == INVALID_HANDLE_VALUE) {
        std::cerr << "dir_list: cannot open directory: " << path << std::endl;
        return Value::array_id(array_id);
    }
    
    do {
        std::string filename = find_data.cFileName;
        // Skip . and ..
        if (filename != "." && filename != "..") {
            uint32_t str_id = vm->strings().intern(filename);
            vm->arrays().push_back(array_id, Value::string_id(str_id));
        }
    } while (FindNextFileA(hFind, &find_data) != 0);
    
    FindClose(hFind);
    
    #else
    DIR* dir = opendir(path.c_str());
    if (!dir) {
        std::cerr << "dir_list: cannot open directory: " << path << std::endl;
        return Value::array_id(array_id);
    }
    
    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        std::string filename = entry->d_name;
        // Skip . and ..
        if (filename != "." && filename != "..") {
            uint32_t str_id = vm->strings().intern(filename);
            vm->arrays().push_back(array_id, Value::string_id(str_id));
        }
    }
    
    closedir(dir);
    #endif
    
    return Value::array_id(array_id);
}

void register_file_functions(HostEnvironment* env, VM* vm) {
    env->register_function("file_exists", [vm](const std::vector<Value>& args) {
        return file_exists(vm, args);
    });
    
    env->register_function("file_read", [vm](const std::vector<Value>& args) {
        return file_read(vm, args);
    });
    
    env->register_function("file_write", [vm](const std::vector<Value>& args) {
        return file_write(vm, args);
    });
    
    env->register_function("file_append", [vm](const std::vector<Value>& args) {
        return file_append(vm, args);
    });
    
    env->register_function("file_lines", [vm](const std::vector<Value>& args) {
        return file_lines(vm, args);
    });
    
    env->register_function("file_delete", [vm](const std::vector<Value>& args) {
        return file_delete(vm, args);
    });
    
    env->register_function("dir_exists", [vm](const std::vector<Value>& args) {
        return dir_exists(vm, args);
    });
    
    env->register_function("dir_create", [vm](const std::vector<Value>& args) {
        return dir_create(vm, args);
    });
    
    env->register_function("dir_list", [vm](const std::vector<Value>& args) {
        return dir_list(vm, args);
    });
}

} // namespace stdlib
} // namespace nightscript
} // namespace nightforge
