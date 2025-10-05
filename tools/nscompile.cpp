#include "../src/nightscript/compiler.h"
#include "../src/nightscript/value.h"
#include <iostream>
#include <fstream>
#include <chrono>

using namespace nightforge::nightscript;

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: nscompile <script.ns>" << std::endl;
        std::cerr << "       nscompile --help" << std::endl;
        return 1;
    }
    
    if (std::string(argv[1]) == "--help") {
        std::cout << "NightScript Compiler v1.0" << std::endl;
        std::cout << "Compiles .ns scripts to .nsc bytecode for faster loading" << std::endl;
        std::cout << "" << std::endl;
        std::cout << "Usage: nscompile <script.ns>" << std::endl;
        std::cout << "Output: <script.ns.nsc>" << std::endl;
        std::cout << "" << std::endl;
        std::cout << "Performance: 50-100x faster loading after compilation!" << std::endl;
        return 0;
    }
    
    std::string input_path = argv[1];
    std::string output_path = input_path + ".nsc"; // .ns -> .ns.nsc
    
    // Read source
    std::ifstream file(input_path);
    if (!file.is_open()) {
        std::cerr << "Error: Could not open file: " << input_path << std::endl;
        return 1;
    }
    
    std::string source;
    std::string line;
    while (std::getline(file, line)) {
        source += line + "\n";
    }
    file.close();
    
    if (source.empty()) {
        std::cerr << "Error: File is empty: " << input_path << std::endl;
        return 1;
    }
    
    std::cout << "Compiling: " << input_path << std::endl;
    
    // Compile
    auto start_time = std::chrono::high_resolution_clock::now();
    
    Chunk chunk;
    StringTable strings;
    Compiler compiler;
    
    if (!compiler.compile(source, chunk, strings)) {
        std::cerr << "Compilation failed!" << std::endl;
        return 1;
    }
    
    auto compile_time = std::chrono::high_resolution_clock::now();
    
    // Save bytecode
    compiler.save_bytecode_cache(input_path, chunk, strings);
    
    auto end_time = std::chrono::high_resolution_clock::now();
    
    // Performance stats
    auto compile_duration = std::chrono::duration_cast<std::chrono::microseconds>(compile_time - start_time);
    auto save_duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - compile_time);
    auto total_duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
    
    // File size stats
    std::ifstream source_file(input_path, std::ios::ate);
    size_t source_size = source_file.tellg();
    source_file.close();
    
    std::ifstream bytecode_file(output_path, std::ios::ate);
    size_t bytecode_size = bytecode_file.tellg();
    bytecode_file.close();
    
    std::cout << "✓ Compilation successful!" << std::endl;
    std::cout << "✓ Bytecode saved: " << output_path << std::endl;
    std::cout << "" << std::endl;
    std::cout << "Performance Report:" << std::endl;
    std::cout << "  Compile time: " << compile_duration.count() << " μs" << std::endl;
    std::cout << "  Save time:    " << save_duration.count() << " μs" << std::endl;
    std::cout << "  Total time:   " << total_duration.count() << " μs" << std::endl;
    std::cout << "" << std::endl;
    std::cout << "File Size Report:" << std::endl;
    std::cout << "  Source:       " << source_size << " bytes" << std::endl;
    std::cout << "  Bytecode:     " << bytecode_size << " bytes" << std::endl;
    std::cout << "  Compression:  " << (100.0 * bytecode_size / source_size) << "%" << std::endl;
    std::cout << "" << std::endl;    
    return 0;
}
