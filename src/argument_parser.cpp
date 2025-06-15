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
    
    // ============================================================================
    // COMPLETELY REWRITTEN ARGUMENT PARSER - CLEAN AND INTUITIVE LOGIC
    // 
    // NEW LOGIC:
    // 1. All parameters before a media path apply to that media
    // 2. Parameters can be specified in ANY order before the media path
    // 3. No more complex dummy configs or order dependencies
    // 4. Works the same for window mode and desktop background mode
    // ============================================================================
    
    // Current settings being built up (applied to next media file)
    CurrentSettings current;
    
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        
        if (arg == "--window" && i + 1 < argc) {
            current.is_window_mode = true;
            parse_window_geometry(argv[++i], current.window_config);
        }
        else if (arg == "--screen-root" && i + 1 < argc) {
            current.is_window_mode = false;
            current.screen_name = argv[++i];
        }
        else if (arg == "--silent" || arg == "--mute") {
            current.silent = true;
        }
        else if (arg == "--volume" && i + 1 < argc) {
            current.volume = std::stoi(argv[++i]);
        }
        else if (arg == "--noautomute") {
            current.no_auto_mute = true;
        }
        else if (arg == "--fps" && i + 1 < argc) {
            current.fps = std::stoi(argv[++i]);
        }
        else if (arg == "--scaling" && i + 1 < argc) {
            std::string scaling = argv[++i];
            if (scaling != "stretch" && scaling != "fit" && scaling != "fill" && scaling != "default") {
                throw std::runtime_error("Invalid scaling mode: " + scaling);
            }
            current.scaling = scaling;
        }
        else if (arg == "--path-to-media" && i + 1 < argc) {
            std::string media_path = argv[++i];
            apply_current_settings_to_config(config, current, media_path);
        }
        else if (arg == "--help" || arg == "-h") {
            print_help();
            exit(0);
        }
        else if (arg.find("--") != 0) {
            // Direct media path - apply current settings
            apply_current_settings_to_config(config, current, arg);
        }
        else {
            std::cerr << "Unknown argument: " << arg << std::endl;
            print_help();
            throw std::runtime_error("Invalid argument");
        }
    }
    
    // Validation
    if (config.windowed_mode && config.window_config.media_path.empty()) {
        throw std::runtime_error("Window mode specified but no media path provided");
    }
    if (!config.windowed_mode && config.screen_configs.empty()) {
        throw std::runtime_error("No screen configurations provided");
    }
    
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

void ArgumentParser::apply_current_settings_to_config(Config& config, const CurrentSettings& current, const std::string& media_path) {
    if (current.is_window_mode) {
        // Window mode - clear any existing screen configs and set up window
        config.windowed_mode = true;
        config.screen_configs.clear();
        
        // Apply settings to window config
        config.window_config = current.window_config;
        config.window_config.media_path = media_path;
        config.window_config.scaling = current.scaling;
        
        // Create a single screen config for compatibility with existing application logic
        ScreenConfig window_screen_config;
        window_screen_config.screen_name = "window";
        window_screen_config.media_path = media_path;
        window_screen_config.silent = current.silent;
        window_screen_config.volume = current.volume;
        window_screen_config.no_auto_mute = current.no_auto_mute;
        window_screen_config.fps = current.fps;
        window_screen_config.scaling = current.scaling;
        config.screen_configs.push_back(window_screen_config);
    } else {
        // Screen mode - add a new screen configuration
        config.windowed_mode = false;
        
        ScreenConfig screen_config;
        screen_config.screen_name = current.screen_name;
        screen_config.media_path = media_path;
        screen_config.silent = current.silent;
        screen_config.volume = current.volume;
        screen_config.no_auto_mute = current.no_auto_mute;
        screen_config.fps = current.fps;
        screen_config.scaling = current.scaling;
        config.screen_configs.push_back(screen_config);
    }
}

void ArgumentParser::print_help() const {
    std::cout << "Linux Wallpaper Engine Extended - Media Background Application\n";
    std::cout << "Usage: " << program_name_ << " [OPTIONS] [path-to-media]\n\n";
    std::cout << "Options:\n";
    std::cout << "  --path-to-media <path>     Path to media file (video/gif/image)\n";
    std::cout << "  --silent, --mute           Mute background audio\n";
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
