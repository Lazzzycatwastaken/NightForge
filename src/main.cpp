#include <iostream>
#include <string>
#include <cstdlib>
#include "core/engine.h"
#include "core/config.h"

void print_usage(const char* program_name) {
    std::cout << "Usage: " << program_name << " [options] [script_file]\n";
    std::cout << "Options:\n";
    std::cout << "  --min-width WIDTH     Minimum terminal width (default: 80)\n";
    std::cout << "  --min-height HEIGHT   Minimum terminal height (default: 24)\n";
    std::cout << "  --dev-hot-reload      Enable hot reload for development\n";
    std::cout << "  --bench               Run microbenchmarks\n";
    std::cout << "  --help, -h            Show this help message\n";
    std::cout << "\n";
    std::cout << "Examples:\n";
    std::cout << "  " << program_name << " assets/scripts/demo.ns\n";
}

int main(int argc, char* argv[]) {
    nightforge::Config config;
    
    // Parse command line arguments
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        
        if (arg == "--help" || arg == "-h") {
            print_usage(argv[0]);
            return 0;
        } else if (arg == "--min-width" && i + 1 < argc) {
            config.min_width = std::atoi(argv[++i]);
        } else if (arg == "--min-height" && i + 1 < argc) {
            config.min_height = std::atoi(argv[++i]);
        } else if (arg == "--dev-hot-reload") {
            config.hot_reload = true;
        } else if (arg == "--bench") {
            config.run_benchmarks = true;
        } else if (arg.substr(0, 2) == "--") {
            std::cerr << "Unknown option: " << arg << std::endl;
            print_usage(argv[0]);
            return 1;
        } else {
            // Treat as script file
            config.script_file = arg;
        }
    }
    
    try {
        nightforge::Engine engine(config);
        return engine.run();
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}