#pragma once

#include <string>
#include <vector>
#include <map>

struct ScreenConfig {
    std::string screen_name;
    std::string media_path;
    bool silent = false;
    int volume = 100;
    bool no_auto_mute = false;
    int fps = -1; // -1 means use native video frame rate
    std::string scaling = "fit"; // stretch, fit, fill, default
};

struct WindowConfig {
    int x = 0;
    int y = 0;
    int width = 800;
    int height = 600;
    std::string media_path;
    std::string scaling = "fit"; // stretch, fit, fill, default
};

struct Config {
    std::vector<ScreenConfig> screen_configs;
    bool windowed_mode = false;
    WindowConfig window_config;
    
    // Global defaults (can be overridden per screen)
    bool default_silent = false;
    int default_volume = 100;
    bool default_no_auto_mute = false;
    int default_fps = -1; // -1 means use native video frame rate
    std::string default_scaling = "fit";
};

class ArgumentParser {
public:
    ArgumentParser();
    Config parse(int argc, char* argv[]);
    void print_help() const;

private:
    struct CurrentSettings {
        bool is_window_mode = false;
        WindowConfig window_config;
        std::string screen_name = "default";
        bool silent = false;
        int volume = 100;
        bool no_auto_mute = false;
        int fps = -1; // -1 means use native video frame rate
        std::string scaling = "fit";
    };
    
    void parse_window_geometry(const std::string& geometry, WindowConfig& config);
    void apply_current_settings_to_config(Config& config, const CurrentSettings& current, const std::string& media_path);
    std::string program_name_;
};
