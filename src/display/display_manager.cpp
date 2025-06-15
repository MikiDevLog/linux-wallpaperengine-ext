#include "display_manager.h"
#include "x11/x11_display.h"
#include "wayland/wayland_display.h"
#include "sdl2_window_display.h"
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <X11/Xlib.h>
#include <wayland-client.h>

DisplayManager::DisplayManager() : protocol_(DisplayProtocol::UNKNOWN), initialized_(false) {}

DisplayManager::~DisplayManager() {
    cleanup();
}

DisplayProtocol DisplayManager::detect_protocol() {
    // Return cached result if already detected
    if (protocol_ != DisplayProtocol::UNKNOWN) {
        return protocol_;
    }
    
    const char* xdg_session_type = std::getenv("XDG_SESSION_TYPE");
    const char* wayland_display = std::getenv("WAYLAND_DISPLAY");
    const char* display = std::getenv("DISPLAY");
    
    std::cout << "Protocol detection - XDG_SESSION_TYPE: " << (xdg_session_type ? xdg_session_type : "null") 
              << ", WAYLAND_DISPLAY: " << (wayland_display ? wayland_display : "null")
              << ", DISPLAY: " << (display ? display : "null") << std::endl;
    
    // Check if Wayland is available and working
    if (wayland_display && strlen(wayland_display) > 0) {
        // Try to connect to Wayland to see if it's actually working
        struct wl_display* wl_test = wl_display_connect(nullptr);
        if (wl_test) {
            wl_display_disconnect(wl_test);
            protocol_ = DisplayProtocol::WAYLAND;
            std::cout << "Detected Wayland display protocol" << std::endl;
            return protocol_;
        } else {
            std::cout << "Wayland display available but connection failed, trying X11" << std::endl;
        }
    }
    
    // Check if X11 is available
    if (display && strlen(display) > 0) {
        // Try to connect to X11 to see if it's actually working
        Display* x11_test = XOpenDisplay(nullptr);
        if (x11_test) {
            XCloseDisplay(x11_test);
            protocol_ = DisplayProtocol::X11;
            std::cout << "Detected X11 display protocol" << std::endl;
            return protocol_;
        } else {
            std::cout << "X11 display available but connection failed" << std::endl;
        }
    }
    
    // Final fallback based on session type
    if (xdg_session_type) {
        std::string session_type = xdg_session_type;
        if (session_type == "wayland") {
            protocol_ = DisplayProtocol::WAYLAND;
            std::cout << "Defaulting to Wayland based on session type" << std::endl;
            return protocol_;
        } else if (session_type == "x11") {
            protocol_ = DisplayProtocol::X11;
            std::cout << "Defaulting to X11 based on session type" << std::endl;
            return protocol_;
        }
    }
    
    std::cerr << "Warning: Could not detect display protocol. Defaulting to X11." << std::endl;
    protocol_ = DisplayProtocol::X11;
    return protocol_;
}

bool DisplayManager::initialize() {
    if (initialized_) {
        return true;
    }
    
    // Detect protocol once and cache it
    protocol_ = detect_protocol();
    
    switch (protocol_) {
        case DisplayProtocol::X11:
            std::cout << "Using X11 display protocol" << std::endl;
            break;
        case DisplayProtocol::WAYLAND:
            std::cout << "Using Wayland display protocol" << std::endl;
            break;
        default:
            std::cerr << "Unknown display protocol" << std::endl;
            return false;
    }
    
    initialized_ = true;
    return true;
}

void DisplayManager::cleanup() {
    initialized_ = false;
}

std::vector<std::unique_ptr<DisplayOutput>> DisplayManager::get_outputs() {
    std::vector<std::unique_ptr<DisplayOutput>> outputs;
    
    if (!initialized_) {
        return outputs;
    }
    
    switch (protocol_) {
        case DisplayProtocol::X11:
            return X11Display::get_outputs();
        case DisplayProtocol::WAYLAND:
            return WaylandDisplay::get_outputs();
        default:
            return outputs;
    }
}

std::unique_ptr<DisplayOutput> DisplayManager::get_output_by_name(const std::string& name) {
    if (!initialized_) {
        return nullptr;
    }
    
    switch (protocol_) {
        case DisplayProtocol::X11:
            return X11Display::get_output_by_name(name);
        case DisplayProtocol::WAYLAND:
            return WaylandDisplay::get_output_by_name(name);
        default:
            return nullptr;
    }
}

std::unique_ptr<DisplayOutput> DisplayManager::create_window(int x, int y, int width, int height) {
    if (!initialized_) {
        return nullptr;
    }
    
    // Use SDL2 for universal cross-platform windowing
    // SDL2 provides excellent performance and works on all platforms
    std::cout << "DEBUG: Creating SDL2 window (universal cross-platform)" << std::endl;
    
    auto sdl2_window = SDL2WindowDisplay::create_window(x, y, width, height);
    if (sdl2_window) {
        std::cout << "DEBUG: Successfully created SDL2 window" << std::endl;
        return sdl2_window;
    }
    
    std::cerr << "ERROR: Failed to create SDL2 window" << std::endl;
    return nullptr;
}
