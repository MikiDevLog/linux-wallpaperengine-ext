#include "application.h"
#include "display/x11/x11_display.h"
#include "display/wayland/wayland_display.h"
#include "display/sdl2_window_display.h"
#include <iostream>
#include <chrono>
#include <thread>
#include <signal.h>

Application::Application() : running_(false), should_exit_(false) {}

Application::~Application() {
    shutdown();
}

bool Application::initialize(const Config& config) {
    config_ = config;
    
    // Initialize display manager
    if (!display_manager_.initialize()) {
        std::cerr << "Failed to initialize display manager" << std::endl;
        return false;
    }
    
    // Initialize PulseAudio for auto-mute functionality
    if (!pulse_audio_.initialize()) {
        std::cerr << "Warning: Failed to initialize PulseAudio. Auto-mute will be disabled." << std::endl;
    }
    
    // Setup based on mode
    if (config_.windowed_mode) {
        if (!setup_window_mode()) {
            std::cerr << "Failed to setup window mode" << std::endl;
            return false;
        }
    } else {
        if (!setup_screen_instances()) {
            std::cerr << "Failed to setup screen instances" << std::endl;
            return false;
        }
    }
    
    std::cout << "Application initialized successfully" << std::endl;
    return true;
}

bool Application::setup_window_mode() {
    // Create window
    window_output_ = display_manager_.create_window(
        config_.window_config.x, config_.window_config.y,
        config_.window_config.width, config_.window_config.height
    );
    
    if (!window_output_ || !window_output_->initialize()) {
        std::cerr << "Failed to create window" << std::endl;
        return false;
    }
    
    // Create media player for window
    window_media_player_ = std::make_unique<MediaPlayer>();
    if (!window_media_player_->initialize()) {
        std::cerr << "Failed to initialize media player for window" << std::endl;
        return false;
    }
    
    // Set up X11 integration if we're using X11
    if (display_manager_.get_protocol() == DisplayProtocol::X11) {
        // Cast to X11Display to get X11-specific methods
        X11Display* x11_display = dynamic_cast<X11Display*>(window_output_.get());
        if (x11_display && x11_display->is_windowed_mode()) {
            if (!window_media_player_->set_x11_window(x11_display->get_x11_display(), 
                                                     x11_display->get_x11_window(), 
                                                     x11_display->get_x11_screen())) {
                std::cerr << "Warning: Failed to set X11 window for media player" << std::endl;
            }
        }
    }
    
    // If there's a media path in window config, load it
    if (!config_.window_config.media_path.empty()) {
        if (!window_media_player_->load_media(config_.window_config.media_path)) {
            std::cerr << "Failed to load media: " << config_.window_config.media_path << std::endl;
            return false;
        }
        
        // Apply default audio settings
        if (config_.default_silent) {
            window_media_player_->set_muted(true);
        } else {
            window_media_player_->set_volume(config_.default_volume);
        }
        
        window_media_player_->set_fps_limit(config_.default_fps);
        
        // Set background with window-specific scaling
        ScalingMode scaling = parse_scaling_mode(config_.window_config.scaling);
        window_output_->set_background(config_.window_config.media_path, scaling);
        
        std::cout << "DEBUG: Media type detected: " << static_cast<int>(window_media_player_->get_media_type()) << std::endl;
        
        // Use SDL2 window display (universal cross-platform solution)
        SDL2WindowDisplay* sdl2_display = dynamic_cast<SDL2WindowDisplay*>(window_output_.get());
        if (sdl2_display) {
            std::cout << "DEBUG: Using SDL2 window display - setting up rendering" << std::endl;
            
            // For video content, start video playback for continuous animation
            if (window_media_player_->get_media_type() == MediaType::VIDEO) {
                std::cout << "DEBUG: Setting up video playback for SDL2 window" << std::endl;
                
                // Start video playback for continuous animation
                if (!window_media_player_->play()) {
                    std::cerr << "Warning: Failed to start video playback" << std::endl;
                }
                
                // Render initial video frame
                unsigned char* frame_data;
                int frame_width, frame_height;
                if (window_media_player_->get_video_frame(&frame_data, &frame_width, &frame_height)) {
                    sdl2_display->render_video_frame(frame_data, frame_width, frame_height, scaling);
                }
            } else if (window_media_player_->get_media_type() == MediaType::IMAGE) {
                // For images, render immediately
                const unsigned char* image_data = window_media_player_->get_image_data();
                if (image_data) {
                    sdl2_display->render_image_data(image_data, 
                                                   window_media_player_->get_width(),
                                                   window_media_player_->get_height(),
                                                   scaling);
                }
            }
        } else {
            std::cout << "DEBUG: Non-SDL2 window display detected" << std::endl;
        }
        
        // Start playback
        window_media_player_->play();
    }
    
    return true;
}

bool Application::setup_screen_instances() {
    std::cout << "DEBUG: Setting up " << config_.screen_configs.size() << " screen instances" << std::endl;
    for (size_t i = 0; i < config_.screen_configs.size(); i++) {
        std::cout << "DEBUG: Screen config " << i << ": " << config_.screen_configs[i].screen_name << " with media: " << config_.screen_configs[i].media_path << std::endl;
    }
    
    screen_instances_.resize(config_.screen_configs.size());
    
    for (size_t i = 0; i < config_.screen_configs.size(); i++) {
        screen_instances_[i].config = config_.screen_configs[i];
        
        if (!initialize_screen_instance(screen_instances_[i])) {
            std::cerr << "Failed to initialize screen instance for: " 
                      << screen_instances_[i].config.screen_name << std::endl;
            return false;
        }
    }
    
    return true;
}

bool Application::initialize_screen_instance(ScreenInstance& instance) {
    std::cout << "DEBUG: Initializing screen instance for: " << instance.config.screen_name << std::endl;
    
    // Get display output
    instance.display_output = display_manager_.get_output_by_name(instance.config.screen_name);
    if (!instance.display_output) {
        std::cerr << "Failed to get display output: " << instance.config.screen_name << std::endl;
        return false;
    }
    
    if (!instance.display_output->initialize()) {
        std::cerr << "Failed to initialize display output: " << instance.config.screen_name << std::endl;
        return false;
    }
    
    // Create media player
    instance.media_player = std::make_unique<MediaPlayer>();
    if (!instance.media_player->initialize()) {
        std::cerr << "Failed to initialize media player for: " << instance.config.screen_name << std::endl;
        return false;
    }
    
    // For video content, we need to establish OpenGL context before loading media
    // This ensures MPV can initialize its render context properly
    WaylandDisplay* wayland_display = dynamic_cast<WaylandDisplay*>(instance.display_output.get());
    if (wayland_display && !instance.config.media_path.empty()) {
        // Detect media type first
        MediaType media_type = instance.media_player->detect_media_type(instance.config.media_path);
        if (media_type == MediaType::VIDEO || media_type == MediaType::GIF) {
            std::cout << "DEBUG: Video/GIF detected, will establish OpenGL context after layer surface is ready" << std::endl;
            // Note: We'll make EGL context current during the rendering phase when the surface is ready
        }
    }
    
    // Load media if specified
    if (!instance.config.media_path.empty()) {
        if (!instance.media_player->load_media(instance.config.media_path)) {
            std::cerr << "Failed to load media: " << instance.config.media_path << std::endl;
            return false;
        }
        
        // Apply audio settings
        if (instance.config.silent) {
            instance.media_player->set_muted(true);
        } else {
            instance.media_player->set_volume(instance.config.volume);
        }
        
        // Auto-mute setting
        if (!instance.config.no_auto_mute) {
            pulse_audio_.set_auto_mute_enabled(true);
        }
        
        instance.media_player->set_fps_limit(instance.config.fps);
        
        // Set background
        ScalingMode scaling = parse_scaling_mode(instance.config.scaling);
        instance.display_output->set_background(instance.config.media_path, scaling);
        
        // Render image if it's a static image
        if (instance.media_player->get_media_type() == MediaType::IMAGE) {
            std::cout << "DEBUG: Rendering image for screen instance: " << instance.config.screen_name << std::endl;
            WaylandDisplay* wayland_display = dynamic_cast<WaylandDisplay*>(instance.display_output.get());
            X11Display* x11_display = dynamic_cast<X11Display*>(instance.display_output.get());
            
            if (wayland_display) {
                const unsigned char* image_data = instance.media_player->get_image_data();
                if (image_data) {
                    std::cout << "DEBUG: Image data available, rendering to Wayland background" << std::endl;
                    wayland_display->render_image_data(image_data, 
                                                      instance.media_player->get_width(),
                                                      instance.media_player->get_height(),
                                                      scaling);
                } else {
                    std::cout << "DEBUG: No image data available for Wayland background rendering" << std::endl;
                }
            } else if (x11_display) {
                const unsigned char* image_data = instance.media_player->get_image_data();
                if (image_data) {
                    std::cout << "DEBUG: Image data available, rendering to X11 background" << std::endl;
                    x11_display->render_image_data(image_data, 
                                                  instance.media_player->get_width(),
                                                  instance.media_player->get_height(),
                                                  scaling);
                } else {
                    std::cout << "DEBUG: No image data available for X11 background rendering" << std::endl;
                }
            } else {
                std::cout << "DEBUG: Unknown display type for screen instance" << std::endl;
            }
        } else if (instance.media_player->get_media_type() == MediaType::VIDEO) {
            std::cout << "DEBUG: Video detected for screen instance: " << instance.config.screen_name << std::endl;
            // For videos, we need to render frames continuously in the update loop
            // Initial frame rendering will be handled in the update loop
        }
        
        // Start playback
        instance.media_player->play();
    }
    
    instance.initialized = true;
    std::cout << "Initialized screen: " << instance.config.screen_name << std::endl;
    return true;
}

ScalingMode Application::parse_scaling_mode(const std::string& scaling) {
    if (scaling == "stretch") return ScalingMode::STRETCH;
    if (scaling == "fit") return ScalingMode::FIT;
    if (scaling == "fill") return ScalingMode::FILL;
    return ScalingMode::DEFAULT;
}

void Application::run() {
    running_ = true;
    
    std::cout << "Starting application main loop..." << std::endl;
    
    // Main update loop
    update_loop();
    
    std::cout << "Application main loop ended" << std::endl;
}

void Application::update_loop() {
    auto last_auto_mute_check = std::chrono::steady_clock::now();
    const auto auto_mute_check_interval = std::chrono::milliseconds(1000); // Check every second
    
    std::cout << "DEBUG: Starting update loop for KDE Wayland compatibility" << std::endl;
    
    while (running_ && !should_exit_) {
        auto now = std::chrono::steady_clock::now();
        
        // Update media players
        if (config_.windowed_mode) {
            if (window_media_player_) {
                window_media_player_->update();
                
                // Render video frames continuously for windowed mode
                if (window_media_player_->get_media_type() == MediaType::VIDEO) {
                    ScalingMode scaling = config_.screen_configs.empty() ? ScalingMode::DEFAULT : 
                                         parse_scaling_mode(config_.screen_configs[0].scaling);
                    
                    // Use SDL2 window display (universal solution)
                    SDL2WindowDisplay* sdl2_display = dynamic_cast<SDL2WindowDisplay*>(window_output_.get());
                    if (sdl2_display) {
                        // Check if window should close
                        if (sdl2_display->should_close()) {
                            should_exit_ = true;
                            break;
                        }
                        
                        unsigned char* frame_data;
                        int frame_width, frame_height;
                        if (window_media_player_->get_video_frame(&frame_data, &frame_width, &frame_height)) {
                            sdl2_display->render_video_frame(frame_data, frame_width, frame_height, scaling);
                        }
                    }
                } else if (window_media_player_->get_media_type() == MediaType::IMAGE) {
                    // Handle image rendering for SDL2 window display
                    SDL2WindowDisplay* sdl2_display = dynamic_cast<SDL2WindowDisplay*>(window_output_.get());
                    if (sdl2_display) {
                        // Check if window should close
                        if (sdl2_display->should_close()) {
                            should_exit_ = true;
                            break;
                        }
                        
                        const unsigned char* image_data = window_media_player_->get_image_data();
                        if (image_data) {
                            ScalingMode scaling = config_.screen_configs.empty() ? ScalingMode::DEFAULT : 
                                                 parse_scaling_mode(config_.screen_configs[0].scaling);
                            sdl2_display->render_image_data(image_data, 
                                                           window_media_player_->get_width(),
                                                           window_media_player_->get_height(),
                                                           scaling);
                        }
                    }
                }
            }
            if (window_output_) {
                window_output_->update();
            }
        } else {
            for (auto& instance : screen_instances_) {
                if (instance.initialized) {
                    if (instance.media_player) {
                        instance.media_player->update();
                        
                        // Render video frames continuously for background mode
                        if (instance.media_player->get_media_type() == MediaType::VIDEO) {
                            ScalingMode scaling = parse_scaling_mode(instance.config.scaling);
                            
                            WaylandDisplay* wayland_display = dynamic_cast<WaylandDisplay*>(instance.display_output.get());
                            X11Display* x11_display = dynamic_cast<X11Display*>(instance.display_output.get());
                            
                            if (wayland_display) {
                                // PREFER CPU-based rendering for KDE Wayland stability
                                std::cout << "DEBUG: Using CPU-based video rendering for KDE Wayland background" << std::endl;
                                unsigned char* frame_data;
                                int frame_width, frame_height;
                                if (instance.media_player->get_video_frame_cpu(&frame_data, &frame_width, &frame_height)) {
                                    wayland_display->render_video_frame(frame_data, frame_width, frame_height, scaling);
                                } else {
                                    // Final fallback to FFmpeg if CPU extraction fails
                                    if (instance.media_player->get_video_frame_ffmpeg(&frame_data, &frame_width, &frame_height)) {
                                        wayland_display->render_video_frame(frame_data, frame_width, frame_height, scaling);
                                    }
                                }
                            } else if (x11_display) {
                                // Make context current (no-op for X11 but for API consistency)
                                if (x11_display->make_egl_current()) {
                                    unsigned char* frame_data;
                                    int frame_width, frame_height;
                                    if (instance.media_player->get_video_frame(&frame_data, &frame_width, &frame_height)) {
                                        x11_display->render_video_frame(frame_data, frame_width, frame_height, scaling);
                                    }
                                }
                            }
                        }
                    }
                    if (instance.display_output) {
                        instance.display_output->update();
                    }
                }
            }
        }
        
        // Check for auto-mute periodically
        if (now - last_auto_mute_check >= auto_mute_check_interval) {
            update_auto_mute();
            last_auto_mute_check = now;
        }
        
        // Sleep to prevent excessive CPU usage - use longer intervals for KDE Wayland stability
        std::this_thread::sleep_for(std::chrono::milliseconds(33)); // ~30 FPS update rate for better stability
    }
    
    std::cout << "DEBUG: Update loop ended" << std::endl;
}

void Application::update_auto_mute() {
    bool should_mute = pulse_audio_.should_mute_background_audio();
    
    if (config_.windowed_mode) {
        if (window_media_player_ && !config_.screen_configs.empty() && 
            !config_.screen_configs[0].no_auto_mute && !config_.screen_configs[0].silent) {
            window_media_player_->set_muted(should_mute);
        }
    } else {
        for (auto& instance : screen_instances_) {
            if (instance.initialized && instance.media_player && 
                !instance.config.no_auto_mute && !instance.config.silent) {
                instance.media_player->set_muted(should_mute);
            }
        }
    }
}

void Application::shutdown() {
    should_exit_ = true;
    running_ = false;
    
    std::cout << "Shutting down application..." << std::endl;
    
    // Cleanup screen instances
    for (auto& instance : screen_instances_) {
        if (instance.media_player) {
            instance.media_player->stop();
            instance.media_player->cleanup();
        }
        if (instance.display_output) {
            instance.display_output->cleanup();
        }
    }
    screen_instances_.clear();
    
    // Cleanup window mode
    if (window_media_player_) {
        window_media_player_->stop();
        window_media_player_->cleanup();
        window_media_player_.reset();
    }
    if (window_output_) {
        window_output_->cleanup();
        window_output_.reset();
    }
    
    // Cleanup subsystems
    pulse_audio_.cleanup();
    display_manager_.cleanup();
    
    std::cout << "Application shutdown complete" << std::endl;
}

void Application::handle_signal(int signal) {
    std::cout << "Received signal " << signal << ", shutting down..." << std::endl;
    should_exit_ = true;
}
