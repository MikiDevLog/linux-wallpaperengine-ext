#include "application.h"
#include "display/x11/x11_display.h"
#include "display/wayland/wayland_display.h"
#include "display/sdl2_window_display.h"
#include <iostream>
#include <chrono>
#include <thread>
#include <signal.h>
#include <algorithm>

Application::Application() : running_(false), should_exit_(false), 
                           target_fps_(30), frame_duration_(33) {}

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
    
    // Configure FPS limiting for the application
    target_fps_ = calculate_effective_fps();
    frame_duration_ = std::chrono::milliseconds(1000 / target_fps_);
    std::cout << "FPS limit set to " << target_fps_ << " (" << frame_duration_.count() << "ms per frame)" << std::endl;
    
    // FPS CONTROL ARCHITECTURE:
    // 1. For desktop background mode (X11/Wayland): MediaPlayer::should_display_frame() controls frame skipping
    // 2. For windowed mode (SDL2):
    //    a. VSync is enabled when using native video FPS
    //    b. VSync is disabled + SDL_Delay used when FPS limit is applied
    //    c. MediaPlayer::should_display_frame() is used to control which frames to display
    //    d. Both MediaPlayer and SDL2 use the same FPS target for consistency
    
    // Apply audio settings to media players
    apply_audio_settings();
    
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
        
        // Start video playbook for continuous animation
        if (!window_media_player_->play()) {
            std::cerr << "Warning: Failed to start video playback" << std::endl;
        }
        
        // Get the FPS setting from config
        int fps_setting = !config_.screen_configs.empty() ? config_.screen_configs[0].fps : config_.default_fps;
        
        // Set background with window-specific scaling
        ScalingMode scaling = parse_scaling_mode(config_.window_config.scaling);
        window_output_->set_background(config_.window_config.media_path, scaling);
        
        // Use SDL2 window display (universal cross-platform solution)
        SDL2WindowDisplay* sdl2_display = dynamic_cast<SDL2WindowDisplay*>(window_output_.get());
        
        // If we're using an SDL2 window display, configure its frame rate control
        if (sdl2_display) {
            // For SDL2 window display, use both MediaPlayer's frame skipping and SDL2's rendering control
            // 1. Configure SDL2's frame rate limiter for the rendering stage
            sdl2_display->set_target_fps(fps_setting);
            
            // 2. Enable MediaPlayer's frame skipping logic to maintain proper video speed
            window_media_player_->set_fps_limit(fps_setting);
            
            std::cout << "DEBUG: Using combined frame rate control for window mode: " 
                      << (fps_setting <= 0 ? "Native video FPS with VSync" : std::to_string(fps_setting) + " FPS")
                      << " (MediaPlayer skips frames, SDL2 renders displayed frames)"
                      << std::endl;
        } else {
            // Non-SDL2 renderer - use MediaPlayer's frame rate limiting
            window_media_player_->set_fps_limit(fps_setting);
        }
        if (sdl2_display) {
            
            // For video content, start video playback for continuous animation
            if (window_media_player_->get_media_type() == MediaType::VIDEO) {
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
        }
    }
    
    return true;
}

bool Application::setup_screen_instances() {
    for (size_t i = 0; i < config_.screen_configs.size(); i++) {
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
        
        // Set background
        ScalingMode scaling = parse_scaling_mode(instance.config.scaling);
        instance.display_output->set_background(instance.config.media_path, scaling);
        
        // Start playback first so MediaPlayer can detect native frame rate
        instance.media_player->play();
        
        // FIXED: Set FPS limit AFTER starting playback so native frame rate is detected
        // This allows -1 (native frame rate) to work correctly
        instance.media_player->set_fps_limit(instance.config.fps);
        
        // Render image if it's a static image
        if (instance.media_player->get_media_type() == MediaType::IMAGE) {
            WaylandDisplay* wayland_display = dynamic_cast<WaylandDisplay*>(instance.display_output.get());
            X11Display* x11_display = dynamic_cast<X11Display*>(instance.display_output.get());
            
            if (wayland_display) {
                const unsigned char* image_data = instance.media_player->get_image_data();
                if (image_data) {
                    wayland_display->render_image_data(image_data, 
                                                      instance.media_player->get_width(),
                                                      instance.media_player->get_height(),
                                                      scaling);
                } else {
                }
            } else if (x11_display) {
                const unsigned char* image_data = instance.media_player->get_image_data();
                if (image_data) {
                    x11_display->render_image_data(image_data, 
                                                  instance.media_player->get_width(),
                                                  instance.media_player->get_height(),
                                                  scaling);
                } else {
                }
            } else {
            }
        } else if (instance.media_player->get_media_type() == MediaType::VIDEO) {
            // For videos, we need to render frames continuously in the update loop
            // Initial frame rendering will be handled in the update loop
        }
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
    auto last_frame_time = std::chrono::steady_clock::now();
    
    std::cout << "Starting update loop with " << target_fps_ << " FPS target (" 
              << frame_duration_.count() << "ms per frame)" << std::endl;
    
    while (running_ && !should_exit_) {
        auto now = std::chrono::steady_clock::now();
        
        // Update media players
        if (config_.windowed_mode) {
            if (window_media_player_) {
                window_media_player_->update();
                
                // ============================================================================
                // CRITICAL SCALING MODE PARSING FIX - PREVENTS FLICKERING - DO NOT MODIFY
                // 
                // This fix resolves the SDL2 window mode scaling flickering issue.
                // BUG: Previously used config_.screen_configs[0].scaling (for background mode)
                // FIX: Now correctly uses config_.window_config.scaling (for window mode)
                // 
                // This ensures command line --scaling argument is parsed correctly in window mode.
                // Without this fix, scaling mode flickered between FILL(2) and DEFAULT(3).
                // ============================================================================
                // Render video frames continuously for windowed mode
                // FIX: Use window_config.scaling instead of screen_configs[0].scaling for window mode
                ScalingMode scaling = parse_scaling_mode(config_.window_config.scaling);
                
                // ============================================================================
                // APPLICATION UPDATE LOOP DEBUG FOR SDL2 FLICKERING INVESTIGATION
                // ============================================================================
                static int app_update_call_count = 0;
                app_update_call_count++;
                
                
                // Use SDL2 window display (universal solution)
                SDL2WindowDisplay* sdl2_display = dynamic_cast<SDL2WindowDisplay*>(window_output_.get());
                
                if (window_media_player_->get_media_type() == MediaType::VIDEO) {
                    if (sdl2_display) {
                        // Check if window should close
                        if (sdl2_display->should_close()) {
                            should_exit_ = true;
                            break;
                        }
                        
                        // Process and decode frames
                        unsigned char* frame_data;
                        int frame_width, frame_height;
                        bool frame_available = false;
                        
                        // With SDL2's frame rate control, we always display all decoded frames
                        // IMPORTANT: We rely on the target_fps_ setting in SDL2WindowDisplay
                        // to limit the frame rate and properly skip frames
                        
                        // Track frame processing metrics
                        static int processed_frame_count = 0;
                        static auto last_frame_count_time = std::chrono::steady_clock::now();
                        auto now_time = std::chrono::steady_clock::now();
                        
                        // Always decode frames to keep video running at proper speed
                        if (window_media_player_->get_video_frame_cpu(&frame_data, &frame_width, &frame_height)) {
                            frame_available = true;
                            processed_frame_count++;
                        } else if (window_media_player_->get_video_frame_ffmpeg(&frame_data, &frame_width, &frame_height)) {
                            frame_available = true;
                            processed_frame_count++;
                        }
                        
                        // Log frame processing rate every 5 seconds to verify proper speed
                        auto frame_count_elapsed = std::chrono::duration_cast<std::chrono::seconds>(now_time - last_frame_count_time);
                        if (frame_count_elapsed.count() >= 5) {
                            double fps = static_cast<double>(processed_frame_count) / frame_count_elapsed.count();
                            std::cout << "WINDOW MODE: Processed " << processed_frame_count 
                                      << " frames in " << frame_count_elapsed.count() 
                                      << "s (" << fps << " fps)" << std::endl;
                            processed_frame_count = 0;
                            last_frame_count_time = now_time;
                        }
                        
                        // Use MediaPlayer's frame skipping logic to determine if this frame should be displayed
                        // This ensures consistency between background mode and windowed mode
                        if (frame_available && window_media_player_->should_display_frame()) {
                            // Render the frame using SDL2's renderer
                            sdl2_display->render_video_frame(frame_data, frame_width, frame_height, scaling);
                            
                            // Add debug logging every 100 frames
                            static int debug_frame_count = 0;
                            if (++debug_frame_count % 100 == 0) {
                                std::cout << "WINDOW MODE: Rendered frame " << debug_frame_count << std::endl;
                            }
                        }
                        // NOTE: We always process frames to keep the video advancing at the native rate
                    }
                } else if (window_media_player_->get_media_type() == MediaType::IMAGE) {
                    // For images, render immediately
                    const unsigned char* image_data = window_media_player_->get_image_data();
                    if (image_data && sdl2_display) {
                        sdl2_display->render_image_data(image_data, 
                                                       window_media_player_->get_width(),
                                                       window_media_player_->get_height(),
                                                       scaling);
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
                            
                            // FIXED: Apply FPS control at application level instead of decode level
                            // Only render frame if enough time has passed according to target FPS
                            if (instance.media_player->should_display_frame()) {
                                WaylandDisplay* wayland_display = dynamic_cast<WaylandDisplay*>(instance.display_output.get());
                                X11Display* x11_display = dynamic_cast<X11Display*>(instance.display_output.get());
                                
                                if (wayland_display) {
                                    // PREFER CPU-based rendering for KDE Wayland stability
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
                            } else {
                                // Still need to advance video timing even if not displaying
                                // This ensures video runs at native speed regardless of display FPS
                                unsigned char* dummy_frame_data;
                                int dummy_width, dummy_height;
                                instance.media_player->get_video_frame_cpu(&dummy_frame_data, &dummy_width, &dummy_height);
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
        
        // Adaptive FPS limiting based on configuration
        auto elapsed_since_last_frame = now - last_frame_time;
        if (elapsed_since_last_frame < frame_duration_) {
            auto sleep_time = frame_duration_ - elapsed_since_last_frame;
            std::this_thread::sleep_for(sleep_time);
        }
        last_frame_time = std::chrono::steady_clock::now();
    }
    
}

void Application::update_auto_mute() {
    bool should_mute = pulse_audio_.should_mute_background_audio();
    static bool last_mute_state = false;
    
    // Only log when mute state changes to avoid spam
    if (should_mute != last_mute_state) {
        if (should_mute) {
            std::cout << "INFO: Auto-mute triggered - other applications detected playing audio" << std::endl;
        } else {
            std::cout << "INFO: Auto-mute released - no conflicting audio detected" << std::endl;
        }
        last_mute_state = should_mute;
    }
    
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

int Application::calculate_effective_fps() const {
    int effective_fps = 30; // Default FPS fallback
    
    if (config_.windowed_mode) {
        // For window mode, check if FPS was explicitly set
        if (!config_.screen_configs.empty()) {
            if (config_.screen_configs[0].fps > 0) {
                effective_fps = config_.screen_configs[0].fps;
            } else {
                // fps == -1: Use native video frame rate if available
                if (window_media_player_ && window_media_player_->is_video()) {
                    // Try to get native frame rate from the media player
                    // For now, use a reasonable default for application loop
                    effective_fps = 60; // Higher app FPS for smooth native video playback
                } else {
                    effective_fps = 30; // Default for non-video content
                }
            }
        } else {
            if (config_.default_fps > 0) {
                effective_fps = config_.default_fps;
            } else {
                // Use native video frame rate logic
                effective_fps = window_media_player_ && window_media_player_->is_video() ? 60 : 30;
            }
        }
    } else {
        // For background mode, find the highest FPS among all screen instances
        // This ensures smooth playback for the most demanding screen
        bool found_explicit_fps = false;
        for (const auto& instance : screen_instances_) {
            if (instance.initialized && instance.config.fps > 0) {
                effective_fps = std::max(effective_fps, instance.config.fps);
                found_explicit_fps = true;
            }
        }
        
        // If no explicit FPS was set and we have video content, use higher app FPS
        if (!found_explicit_fps) {
            for (const auto& instance : screen_instances_) {
                if (instance.initialized && instance.media_player && 
                    instance.media_player->is_video()) {
                    effective_fps = 60; // Higher app FPS for native video playback
                    break;
                }
            }
        }
        
        // Fallback to default FPS if no instances are initialized
        if (screen_instances_.empty()) {
            effective_fps = config_.default_fps > 0 ? config_.default_fps : 30;
        }
    }
    
    // Clamp to reasonable range (1-120 FPS)
    return std::max(1, std::min(120, effective_fps));
}

void Application::apply_audio_settings() {
    if (config_.windowed_mode) {
        if (window_media_player_) {
            // Apply audio settings from first screen config or defaults
            if (!config_.screen_configs.empty()) {
                const auto& screen_config = config_.screen_configs[0];
                window_media_player_->set_volume(screen_config.volume);
                window_media_player_->set_muted(screen_config.silent);
                
                std::cout << "Applied window audio settings: volume=" << screen_config.volume 
                          << "%, muted=" << (screen_config.silent ? "yes" : "no") << std::endl;
            } else {
                // Use global defaults
                window_media_player_->set_volume(config_.default_volume);
                window_media_player_->set_muted(config_.default_silent);
                
                std::cout << "Applied window default audio settings: volume=" << config_.default_volume 
                          << "%, muted=" << (config_.default_silent ? "yes" : "no") << std::endl;
            }
        }
    } else {
        // Apply audio settings to each screen instance
        for (auto& instance : screen_instances_) {
            if (instance.initialized && instance.media_player) {
                instance.media_player->set_volume(instance.config.volume);
                instance.media_player->set_muted(instance.config.silent);
                
                std::cout << "Applied screen '" << instance.config.screen_name 
                          << "' audio settings: volume=" << instance.config.volume 
                          << "%, muted=" << (instance.config.silent ? "yes" : "no") << std::endl;
            }
        }
    }
}
