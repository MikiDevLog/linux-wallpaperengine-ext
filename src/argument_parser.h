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
    int fps = 30;
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
    int default_fps = 30;
    std::string default_scaling = "fit";
};

class ArgumentParser {
public:
    ArgumentParser();
    Config parse(int argc, char* argv[]);
    void print_help() const;

private:
    void parse_window_geometry(const std::string& geometry, WindowConfig& config);
    std::string program_name_;
};
