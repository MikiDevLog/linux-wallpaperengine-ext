#include "application.h"
#include "argument_parser.h"
#include <iostream>
#include <signal.h>
#include <memory>
#include <clocale>
#include <clocale>
#include <SDL2/SDL.h>

// Global application instance for signal handling
std::unique_ptr<Application> g_app;

void signal_handler(int signal) {
    if (g_app) {
        g_app->handle_signal(signal);
    }
}

void setup_signal_handlers() {
    signal(SIGINT, signal_handler);   // Ctrl+C
    signal(SIGTERM, signal_handler);  // Termination request
    signal(SIGHUP, signal_handler);   // Hangup
}

void print_version() {
    std::cout << "Linux Wallpaper Engine Extended v1.0.0" << std::endl;
    std::cout << "A console application for outputting video, gifs and static images" << std::endl;
    std::cout << "to desktop backgrounds on X11 and Wayland protocols." << std::endl;
}

int main(int argc, char* argv[]) {
    try {
        // Set locale for numeric formatting consistency
        setlocale(LC_NUMERIC, "C");
        
        // Global VSync disabling for SDL2 before any SDL initialization
        // This ensures frame rate control works correctly in windowed mode
        SDL_SetHintWithPriority(SDL_HINT_RENDER_VSYNC, "0", SDL_HINT_OVERRIDE);
        
        print_version();
        std::cout << std::endl;
        
        // Parse command line arguments
        ArgumentParser parser;
        Config config;
        
        try {
            config = parser.parse(argc, argv);
        } catch (const std::exception& e) {
            std::cerr << "Error parsing arguments: " << e.what() << std::endl;
            return 1;
        }
        
        // Create and initialize application
        g_app = std::make_unique<Application>();
        
        // Setup signal handlers
        setup_signal_handlers();
        
        // Initialize application
        if (!g_app->initialize(config)) {
            std::cerr << "Failed to initialize application" << std::endl;
            return 1;
        }
        
        std::cout << "Press Ctrl+C to exit" << std::endl;
        std::cout << std::endl;
        
        // Run application
        g_app->run();
        
        // Cleanup
        g_app->shutdown();
        g_app.reset();
        
        std::cout << "Goodbye!" << std::endl;
        return 0;
        
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        if (g_app) {
            g_app->shutdown();
            g_app.reset();
        }
        return 1;
    } catch (...) {
        std::cerr << "Unknown fatal error occurred" << std::endl;
        if (g_app) {
            g_app->shutdown();
            g_app.reset();
        }
        return 1;
    }
}
