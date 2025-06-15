#include "sdl2_window_display.h"
#include <iostream>
#include <stdexcept>
#include <cstring>
#include <chrono>
#include <thread>

SDL2WindowDisplay::SDL2WindowDisplay(int x, int y, int width, int height) 
    : x_(x), y_(y), width_(width), height_(height),
      initialized_(false), visible_(false), should_close_(false), 
      window_(nullptr), renderer_(nullptr), current_texture_(nullptr),
      current_scaling_(ScalingMode::DEFAULT), target_fps_(0) {
}

SDL2WindowDisplay::~SDL2WindowDisplay() {
    cleanup();
}

bool SDL2WindowDisplay::initialize() {
    if (initialized_) {
        return true;
    }
    
    std::cout << "DEBUG: Initializing SDL2 window display" << std::endl;
    
    if (!init_sdl()) {
        std::cerr << "ERROR: Failed to initialize SDL2" << std::endl;
        return false;
    }
    
    if (!create_window()) {
        std::cerr << "ERROR: Failed to create SDL2 window" << std::endl;
        return false;
    }
    
    initialized_ = true;
    visible_ = true;
    
    std::cout << "DEBUG: SDL2 window display initialized successfully" << std::endl;
    return true;
}

void SDL2WindowDisplay::cleanup() {
    if (!initialized_) {
        return;
    }
    
    cleanup_sdl();
    
    initialized_ = false;
    visible_ = false;
}

bool SDL2WindowDisplay::set_background(const std::string& media_path, ScalingMode scaling) {
    current_media_path_ = media_path;
    current_scaling_ = scaling;
    return true;
}

void SDL2WindowDisplay::update() {
    if (!initialized_) {
        return;
    }
    
    handle_events();
    
    // CRITICAL FIX: Don't automatically re-render frames in update()
    // The application's video rendering logic handles frame rendering
    // This method should only handle events and window management
    // 
    // The automatic re-rendering was causing duplicate frame renders:
    // 1. Application renders new video frame
    // 2. update() re-renders the same frame again
    // This made video appear slow because each frame was displayed twice
}

std::string SDL2WindowDisplay::get_name() const {
    return "SDL2 Window";
}

bool SDL2WindowDisplay::render_image_data(const unsigned char* image_data, int img_width, int img_height, ScalingMode scaling) {
    if (!initialized_ || !image_data || !renderer_) {
        return false;
    }
    
    std::cout << "DEBUG: SDL2 rendering image: " << img_width << "x" << img_height << std::endl;
    
    // Create texture from image data
    if (!create_texture_from_data(image_data, img_width, img_height)) {
        std::cerr << "ERROR: Failed to create texture from image data" << std::endl;
        return false;
    }
    
    // Clear screen and render
    SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 255);
    SDL_RenderClear(renderer_);
    
    render_current_texture(scaling);
    
    SDL_RenderPresent(renderer_);
    
    return true;
}

bool SDL2WindowDisplay::render_video_frame(const unsigned char* frame_data, int frame_width, int frame_height, ScalingMode scaling) {
    if (!initialized_ || !frame_data || !renderer_) {
        return false;
    }
    
    // FRAME RATE CONTROL: Use high-precision steady_clock instead of SDL_GetTicks
    static auto last_render_time = std::chrono::steady_clock::now();
    auto current_time = std::chrono::steady_clock::now();
    
    // Calculate target frame interval based on target FPS (in microseconds for precision)
    std::chrono::microseconds target_interval((target_fps_ > 0) ? 
        static_cast<int64_t>(1000000.0 / target_fps_) : 0);
    
    // Skip frame if we're trying to render too soon (frame skipping)
    if (target_fps_ > 0 && target_interval.count() > 0) {
        auto time_since_last_render = std::chrono::duration_cast<std::chrono::microseconds>(
            current_time - last_render_time);
            
        if (time_since_last_render < target_interval) {
            // Not time to render yet - skip this frame
            static int frames_skipped = 0;
            frames_skipped++;
            
            // Log skipped frames every 50 frames
            if (frames_skipped % 50 == 0) {
                std::cout << "SDL2 RENDERER: Skipped " << frames_skipped << " frames to maintain "
                          << target_fps_ << " FPS" << std::endl;
                std::cout << "    Time since last: " << time_since_last_render.count() 
                          << "μs, Target: " << target_interval.count() << "μs" << std::endl;
            }
            return true; // Pretend we rendered it, but actually skip it
        }
        
        // Optional busy-wait for precision timing
        // This reduces jitter by waiting until we're exactly at the frame time
        int busy_wait_count = 0;
        while (std::chrono::steady_clock::now() - last_render_time < target_interval) {
            std::this_thread::yield(); // Reduce CPU usage while busy waiting
            if (++busy_wait_count % 1000 == 0) break; // Safety limit on busy waiting
        }
    }
    
    // Track this render time - calculate next frame time precisely
    // This prevents timing drift by anchoring to the expected frame time
    if (target_fps_ > 0 && target_interval.count() > 0) {
        last_render_time += target_interval; // Use expected time to prevent drift
        
        // If we're too far behind, reset to avoid spiral of death
        if (current_time - last_render_time > std::chrono::milliseconds(200)) {
            std::cout << "WARNING: Frame timing too far behind, resetting timer" << std::endl;
            last_render_time = current_time;
        }
    } else {
        last_render_time = current_time; // No FPS limit, just use current time
    }
    
    // Create texture from frame data
    if (!create_texture_from_data(frame_data, frame_width, frame_height)) {
        std::cerr << "ERROR: Failed to create texture from video frame data" << std::endl;
        return false;
    }
    
    // Clear screen and render
    SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 255);
    SDL_RenderClear(renderer_);
    
    // Store the scaling mode before rendering
    current_scaling_ = scaling;
    
    render_current_texture(scaling);
    
    // Present the renderer (display the frame)
    SDL_RenderPresent(renderer_);
    
    // FPS debugging output with high-precision timing
    static int frames_rendered = 0;
    static auto fps_timer_start = std::chrono::steady_clock::now();
    frames_rendered++;
    
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(current_time - fps_timer_start);
    if (elapsed.count() >= 5000) { // Log every 5 seconds
        float actual_fps = frames_rendered / (elapsed.count() / 1000.0f);
        std::cout << "SDL2 RENDER: Rendered " << frames_rendered 
                  << " frames in " << (elapsed.count()/1000) 
                  << "s (effective " << actual_fps << " FPS)" << std::endl;
                  
        // If using frame rate limiting, log the target as well
        if (target_fps_ > 0) {
            std::cout << "SDL2 RENDER: Target FPS: " << target_fps_ 
                      << " (" << (1000 / target_fps_) << "ms per frame)" << std::endl;
            
            // Check renderer info to display current VSync status
            SDL_RendererInfo renderer_info;
            if (SDL_GetRendererInfo(renderer_, &renderer_info) == 0) {
                std::cout << "SDL2 RENDER: VSync flag: " 
                          << ((renderer_info.flags & SDL_RENDERER_PRESENTVSYNC) ? "ON" : "OFF") << std::endl;
            }
        }
                  
        // Reset counters
        frames_rendered = 0;
        fps_timer_start = current_time;
    }
    
    return true;
}

void SDL2WindowDisplay::show_window() {
    if (window_) {
        SDL_ShowWindow(window_);
        visible_ = true;
    }
}

void SDL2WindowDisplay::hide_window() {
    if (window_) {
        SDL_HideWindow(window_);
        visible_ = false;
    }
}

bool SDL2WindowDisplay::is_visible() const {
    return visible_ && window_;
}

void SDL2WindowDisplay::handle_events() {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        switch (event.type) {
            case SDL_QUIT:
                should_close_ = true;
                break;
            case SDL_WINDOWEVENT:
                if (event.window.event == SDL_WINDOWEVENT_CLOSE) {
                    should_close_ = true;
                }
                break;
            case SDL_KEYDOWN:
                if (event.key.keysym.sym == SDLK_ESCAPE) {
                    should_close_ = true;
                }
                break;
        }
    }
}

bool SDL2WindowDisplay::should_close() const {
    return should_close_;
}

std::unique_ptr<DisplayOutput> SDL2WindowDisplay::create_window(int x, int y, int width, int height) {
    auto display = std::make_unique<SDL2WindowDisplay>(x, y, width, height);
    if (display->initialize()) {
        return std::move(display);
    }
    return nullptr;
}

// Private implementation methods

bool SDL2WindowDisplay::init_sdl() {
    // Initialize SDL2 with video subsystem
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        std::cerr << "ERROR: SDL2 initialization failed: " << SDL_GetError() << std::endl;
        return false;
    }
    
    return true;
}

bool SDL2WindowDisplay::create_window() {
    // Create window
    window_ = SDL_CreateWindow(
        "Linux Wallpaper Engine Extended",
        x_, y_, width_, height_,
        SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE
    );
    
    if (!window_) {
        std::cerr << "ERROR: Failed to create SDL2 window: " << SDL_GetError() << std::endl;
        return false;
    }
    
    // Create renderer with hardware acceleration without VSync
    // Completely disable VSync via hint to ensure proper frame timing control
    SDL_SetHintWithPriority(SDL_HINT_RENDER_VSYNC, "0", SDL_HINT_OVERRIDE);
    
    // Create renderer without the VSync flag for better FPS control
    renderer_ = SDL_CreateRenderer(window_, -1, SDL_RENDERER_ACCELERATED);
    if (!renderer_) {
        std::cerr << "ERROR: Failed to create SDL2 renderer: " << SDL_GetError() << std::endl;
        return false;
    }
    
    std::cout << "DEBUG: Created SDL2 renderer with VSync explicitly disabled" << std::endl;
    
    // Set renderer blend mode for transparency support
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    
    // Check renderer info to confirm VSync status
    SDL_RendererInfo renderer_info;
    if (SDL_GetRendererInfo(renderer_, &renderer_info) == 0) {
        std::cout << "DEBUG: SDL2 renderer created: " << renderer_info.name << std::endl;
        std::cout << "DEBUG: Initial VSync status: " << ((renderer_info.flags & SDL_RENDERER_PRESENTVSYNC) ? "ENABLED" : "DISABLED") << std::endl;
    }
    
    return true;
}

void SDL2WindowDisplay::cleanup_sdl() {
    if (current_texture_) {
        SDL_DestroyTexture(current_texture_);
        current_texture_ = nullptr;
    }
    
    if (renderer_) {
        SDL_DestroyRenderer(renderer_);
        renderer_ = nullptr;
    }
    
    if (window_) {
        SDL_DestroyWindow(window_);
        window_ = nullptr;
    }
    
    SDL_Quit();
}

bool SDL2WindowDisplay::create_texture_from_data(const unsigned char* data, int width, int height) {
    if (!renderer_) {
        return false;
    }
    
    // OPTIMIZATION: Only recreate texture if dimensions changed
    // This prevents unnecessary texture creation/destruction every frame
    static int last_width = 0;
    static int last_height = 0;
    
    if (!current_texture_ || width != last_width || height != last_height) {
        // Destroy existing texture only if dimensions changed
        if (current_texture_) {
            SDL_DestroyTexture(current_texture_);
            current_texture_ = nullptr;
        }
        
        // Create new texture (assuming RGBA format)
        current_texture_ = SDL_CreateTexture(
            renderer_,
            SDL_PIXELFORMAT_RGBA32,
            SDL_TEXTUREACCESS_STREAMING, // Use STREAMING for better performance
            width, height
        );
        
        if (!current_texture_) {
            std::cerr << "ERROR: Failed to create SDL2 texture: " << SDL_GetError() << std::endl;
            return false;
        }
        
        last_width = width;
        last_height = height;
    }
    
    // Upload data to texture - use more efficient method for streaming textures
    void* pixels;
    int pitch;
    if (SDL_LockTexture(current_texture_, nullptr, &pixels, &pitch) == 0) {
        // Copy data line by line to handle different pitch values
        const unsigned char* src = data;
        unsigned char* dst = static_cast<unsigned char*>(pixels);
        int bytes_per_row = width * 4; // RGBA = 4 bytes per pixel
        
        for (int y = 0; y < height; y++) {
            memcpy(dst, src, bytes_per_row);
            src += bytes_per_row;
            dst += pitch;
        }
        
        SDL_UnlockTexture(current_texture_);
    } else {
        std::cerr << "ERROR: Failed to lock SDL2 texture: " << SDL_GetError() << std::endl;
        current_texture_ = nullptr;
        return false;
    }
    
    return true;
}

void SDL2WindowDisplay::render_current_texture(ScalingMode scaling) {
    if (!current_texture_ || !renderer_) {
        return;
    }
    
    // Get texture dimensions
    int tex_width, tex_height;
    SDL_QueryTexture(current_texture_, nullptr, nullptr, &tex_width, &tex_height);
    
    // Get window dimensions
    int win_width, win_height;
    SDL_GetWindowSize(window_, &win_width, &win_height);
    
    // Calculate destination rectangle based on scaling mode
    SDL_Rect dst_rect;
    calculate_scaled_rect(tex_width, tex_height, win_width, win_height, scaling, dst_rect);
    
    // ============================================================================
    // CRITICAL Y-AXIS ORIENTATION FIX - DO NOT MODIFY OR REMOVE!
    // Render texture without vertical flip - FFmpeg output is correctly oriented
    // ============================================================================
    SDL_RenderCopy(renderer_, current_texture_, nullptr, &dst_rect);
}

void SDL2WindowDisplay::set_target_fps(int fps) {
    target_fps_ = fps;
    
    // Always disable VSync for any explicit FPS control
    // Our high-precision timing will handle frame pacing correctly
    if (renderer_) {
        // First use the hint to make sure drivers respect our setting
        SDL_SetHintWithPriority(SDL_HINT_RENDER_VSYNC, "0", SDL_HINT_OVERRIDE);
        
#if SDL_VERSION_ATLEAST(2, 0, 18)
        // SDL 2.0.18+ supports direct VSync control
        if (SDL_RenderSetVSync(renderer_, 0) != 0) {
            std::cout << "DEBUG: Failed to disable VSync via SDL_RenderSetVSync: " << SDL_GetError() << std::endl;
        } else {
            std::cout << "DEBUG: Successfully disabled VSync via SDL_RenderSetVSync" << std::endl;
        }
#else
        // For older SDL versions, we already did our best with the hint
        std::cout << "DEBUG: Using SDL_HINT_RENDER_VSYNC='0' for frame rate control" << std::endl;
#endif

        // Check renderer info to confirm VSync status after change
        SDL_RendererInfo renderer_info;
        if (SDL_GetRendererInfo(renderer_, &renderer_info) == 0) {
            bool vsync_enabled = (renderer_info.flags & SDL_RENDERER_PRESENTVSYNC) != 0;
            std::cout << "DEBUG: VSync status after change: " 
                      << (vsync_enabled ? "ENABLED" : "DISABLED") << std::endl;
            
            // If VSync is still enabled despite our efforts, warn about it
            if (vsync_enabled && fps > 0) {
                std::cout << "WARNING: VSync is still ENABLED despite attempts to disable it." << std::endl;
                std::cout << "         This may affect frame rate limiting accuracy." << std::endl;
                std::cout << "         Consider forcing driver-level VSync off if available." << std::endl;
            }
        }
    }
    
    if (fps <= 0) {
        std::cout << "DEBUG: SDL2 frame rate control disabled (using maximum possible FPS)" << std::endl;
    } else {
        std::cout << "DEBUG: SDL2 high-precision frame rate control set to " << fps << " FPS" << std::endl;
        std::cout << "DEBUG: Frame interval: " << (1000000.0 / fps) << " microseconds" << std::endl;
    }
}

void SDL2WindowDisplay::calculate_scaled_rect(int src_width, int src_height, int dst_width, int dst_height, 
                                             ScalingMode scaling, SDL_Rect& dst_rect) {
    // ============================================================================
    // CRITICAL SCALING MODE IMPLEMENTATION - VERIFIED WORKING - DO NOT MODIFY
    // 
    // This section implements the three required scaling modes for SDL2 window:
    // - STRETCH (0): Fill entire surface, may distort aspect ratio
    // - FIT (1): Letterbox/pillarbox, preserve aspect ratio, DEFAULT fallback  
    // - FILL (2): Crop to fill surface, preserve aspect ratio
    // 
    // FILL mode uses negative positions for cropping - this is intentional!
    // This is the correct window-mode scaling implementation.
    // ============================================================================
    
    // Calculate aspect ratios
    float src_aspect = static_cast<float>(src_width) / src_height;
    float dst_aspect = static_cast<float>(dst_width) / dst_height;
    
    switch (scaling) {
        case ScalingMode::STRETCH:
            dst_rect.x = 0;
            dst_rect.y = 0;
            dst_rect.w = dst_width;
            dst_rect.h = dst_height;
            break;
            
        case ScalingMode::FIT:
        case ScalingMode::DEFAULT: {
            if (src_aspect > dst_aspect) {
                // Source is wider, fit to width
                dst_rect.w = dst_width;
                dst_rect.h = static_cast<int>(dst_width / src_aspect);
                dst_rect.x = 0;
                dst_rect.y = (dst_height - dst_rect.h) / 2;
            } else {
                // Source is taller, fit to height
                dst_rect.h = dst_height;
                dst_rect.w = static_cast<int>(dst_height * src_aspect);
                dst_rect.x = (dst_width - dst_rect.w) / 2;
                dst_rect.y = 0;
            }
            break;
        }
            
        case ScalingMode::FILL: {
            if (src_aspect > dst_aspect) {
                // Source is wider, crop horizontally
                dst_rect.h = dst_height;
                dst_rect.w = static_cast<int>(dst_height * src_aspect);
                dst_rect.x = -(dst_rect.w - dst_width) / 2;
                dst_rect.y = 0;
            } else {
                // Source is taller, crop vertically
                dst_rect.w = dst_width;
                dst_rect.h = static_cast<int>(dst_width / src_aspect);
                dst_rect.x = 0;
                dst_rect.y = -(dst_rect.h - dst_height) / 2;
            }
            break;
        }
    }
}
