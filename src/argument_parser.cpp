#include "argument_parser.h"
#include <iostream>
#include <cstring>
#include <stdexcept>

ArgumentParser::ArgumentParser() = default;

Config ArgumentParser::parse(int argc, char* argv[]) {
    Config config;
    program_name_ = argv[0];
    
    if (argc < 2) {
        print_help();
        throw std::runtime_error("No arguments provided");
    }
    
    ScreenConfig current_screen;
    bool has_current_screen = false;
    
    // Default values that can be set globally before screen-specific configs
    bool global_silent = false;
    int global_volume = 100;
    bool global_no_auto_mute = false;
    int global_fps = 30;
    std::string global_scaling = "fit";
    
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        
        if (arg == "--path-to-media" && i + 1 < argc) {
            if (config.windowed_mode) {
                // In windowed mode, assign to window config
                config.window_config.media_path = argv[++i];
            } else {
                // In screen mode, assign to current screen
                if (!has_current_screen) {
                    // This is a global media path, create a default screen config
                    current_screen.screen_name = "default";
                    has_current_screen = true;
                }
                current_screen.media_path = argv[++i];
            }
        }
        else if (arg == "--silent") {
            if (has_current_screen) {
                current_screen.silent = true;
            } else {
                global_silent = true;
            }
        }
        else if (arg == "--volume" && i + 1 < argc) {
            int volume = std::stoi(argv[++i]);
            if (has_current_screen) {
                current_screen.volume = volume;
            } else {
                global_volume = volume;
            }
        }
        else if (arg == "--noautomute") {
            if (has_current_screen) {
                current_screen.no_auto_mute = true;
            } else {
                global_no_auto_mute = true;
            }
        }
        else if (arg == "--fps" && i + 1 < argc) {
            int fps = std::stoi(argv[++i]);
            if (has_current_screen) {
                current_screen.fps = fps;
            } else {
                global_fps = fps;
            }
        }
        else if (arg == "--window" && i + 1 < argc) {
            config.windowed_mode = true;
            parse_window_geometry(argv[++i], config.window_config);
            
            // Apply current global scaling to window config
            config.window_config.scaling = global_scaling;
            
            // If window mode is specified, clear any screen configurations
            // as window mode takes precedence
            config.screen_configs.clear();
            has_current_screen = false;
        }
        else if (arg == "--screen-root" && i + 1 < argc) {
            // Save the previous screen config if it exists
            if (has_current_screen) {
                config.screen_configs.push_back(current_screen);
            }
            
            // Start a new screen config
            current_screen = ScreenConfig();
            current_screen.screen_name = argv[++i];
            
            // Apply global defaults
            current_screen.silent = global_silent;
            current_screen.volume = global_volume;
            current_screen.no_auto_mute = global_no_auto_mute;
            current_screen.fps = global_fps;
            current_screen.scaling = global_scaling;
            
            has_current_screen = true;
        }
        else if (arg == "--scaling" && i + 1 < argc) {
            std::string scaling = argv[++i];
            if (scaling != "stretch" && scaling != "fit" && scaling != "fill" && scaling != "default") {
                throw std::runtime_error("Invalid scaling mode: " + scaling);
            }
            if (config.windowed_mode) {
                // In windowed mode, apply to window config
                config.window_config.scaling = scaling;
            } else if (has_current_screen) {
                // In screen mode with current screen, apply to current screen
                current_screen.scaling = scaling;
            } else {
                // Global default
                global_scaling = scaling;
            }
        }
        else if (arg == "--help" || arg == "-h") {
            print_help();
            exit(0);
        }
        else if (arg.find("--") != 0) {
            // Assume it's a direct path to media
            if (config.windowed_mode) {
                // In windowed mode, assign directly to window config
                config.window_config.media_path = arg;
            } else {
                // In screen mode, assign to current screen
                if (!has_current_screen) {
                    // This is a global media path, create a default screen config
                    current_screen.screen_name = "default";
                    has_current_screen = true;
                }
                current_screen.media_path = arg;
            }
        }
        else {
            std::cerr << "Unknown argument: " << arg << std::endl;
            print_help();
            throw std::runtime_error("Invalid argument");
        }
    }
    
    // Add the last screen config if it exists
    if (has_current_screen) {
        config.screen_configs.push_back(current_screen);
    }
    
    if (config.screen_configs.empty() && !config.windowed_mode) {
        throw std::runtime_error("No screen configurations provided");
    }
    
    // Set global defaults
    config.default_silent = global_silent;
    config.default_volume = global_volume;
    config.default_no_auto_mute = global_no_auto_mute;
    config.default_fps = global_fps;
    config.default_scaling = global_scaling;
    
    return config;
}

void ArgumentParser::parse_window_geometry(const std::string& geometry, WindowConfig& config) {
    // Parse format: XxYxWxH
    size_t x_pos = geometry.find('x');
    if (x_pos == std::string::npos) {
        throw std::runtime_error("Invalid window geometry format. Expected: XxYxWxH");
    }
    
    size_t y_pos = geometry.find('x', x_pos + 1);
    if (y_pos == std::string::npos) {
        throw std::runtime_error("Invalid window geometry format. Expected: XxYxWxH");
    }
    
    size_t w_pos = geometry.find('x', y_pos + 1);
    if (w_pos == std::string::npos) {
        throw std::runtime_error("Invalid window geometry format. Expected: XxYxWxH");
    }
    
    try {
        config.x = std::stoi(geometry.substr(0, x_pos));
        config.y = std::stoi(geometry.substr(x_pos + 1, y_pos - x_pos - 1));
        config.width = std::stoi(geometry.substr(y_pos + 1, w_pos - y_pos - 1));
        config.height = std::stoi(geometry.substr(w_pos + 1));
    } catch (const std::exception& e) {
        throw std::runtime_error("Invalid window geometry values");
    }
}

void ArgumentParser::print_help() const {
    std::cout << "Linux Wallpaper Engine Extended - Media Background Application\n";
    std::cout << "Usage: " << program_name_ << " [OPTIONS] [path-to-media]\n\n";
    std::cout << "Options:\n";
    std::cout << "  --path-to-media <path>     Path to media file (video/gif/image)\n";
    std::cout << "  --silent                   Mute background audio\n";
    std::cout << "  --volume <val>             Set audio volume (0-100)\n";
    std::cout << "  --noautomute              Don't mute when other apps play audio\n";
    std::cout << "  --fps <val>               Limit frame rate\n";
    std::cout << "  --window <XxYxWxH>        Run in windowed mode with custom size/position\n";
    std::cout << "  --screen-root <screen>    Set as background for specific screen\n";
    std::cout << "  --scaling <mode>          Wallpaper scaling: stretch, fit, fill, or default\n";
    std::cout << "  --help, -h                Show this help message\n\n";
    std::cout << "Examples:\n";
    std::cout << "  " << program_name_ << " --path-to-media /path/to/video.mp4\n";
    std::cout << "  " << program_name_ << " /path/to/video.mp4  # Direct path usage\n";
    std::cout << "  " << program_name_ << " --screen-root HDMI-1 --volume 50 --fps 60 --scaling fill /path/to/video.mp4 --screen-root HDMI-2 --silent --fps 30 --scaling fill /path/to/video2.mov\n";
    std::cout << "  " << program_name_ << " --window 0x0x800x600 /path/to/image.jpg\n";
}
