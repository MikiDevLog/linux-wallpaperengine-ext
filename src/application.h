#pragma once

#include "argument_parser.h"
#include "media_player.h"
#include "display/display_manager.h"
#include "audio/pulse_audio.h"
#include <vector>
#include <memory>
#include <thread>
#include <atomic>

struct ScreenInstance {
    std::unique_ptr<DisplayOutput> display_output;
    std::unique_ptr<MediaPlayer> media_player;
    ScreenConfig config;
    bool initialized = false;
};

class Application {
public:
    Application();
    ~Application();
    
    bool initialize(const Config& config);
    void run();
    void shutdown();
    
    // Signal handlers
    void handle_signal(int signal);

private:
    Config config_;
    DisplayManager display_manager_;
    PulseAudio pulse_audio_;
    
    std::vector<ScreenInstance> screen_instances_;
    std::unique_ptr<DisplayOutput> window_output_;
    std::unique_ptr<MediaPlayer> window_media_player_;
    
    std::atomic<bool> running_;
    std::atomic<bool> should_exit_;
    
    // FPS limiting
    int target_fps_;
    std::chrono::milliseconds frame_duration_;
    
    bool setup_screen_instances();
    bool setup_window_mode();
    bool initialize_screen_instance(ScreenInstance& instance);
    
    void update_loop();
    void update_auto_mute();
    void apply_audio_settings();
    
    ScalingMode parse_scaling_mode(const std::string& scaling);
    int calculate_effective_fps() const;
};
