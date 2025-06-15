#include "sdl2_window_display.h"
#include <iostream>
#include <stdexcept>
#include <cstring>

SDL2WindowDisplay::SDL2WindowDisplay(int x, int y, int width, int height)
    : x_(x), y_(y), width_(width), height_(height),
      initialized_(false), visible_(false),
      should_close_(false), window_(nullptr), renderer_(nullptr), current_texture_(nullptr),
      current_scaling_(ScalingMode::DEFAULT) {
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
    
    // Clear screen with black
    SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 255);
    SDL_RenderClear(renderer_);
    
    // Render current texture if available
    if (current_texture_) {
        render_current_texture(current_scaling_);
    }
    
    // Present the rendered frame
    SDL_RenderPresent(renderer_);
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
    
    // ============================================================================
    // ENHANCED SDL2 VIDEO FRAME RENDER DEBUG FOR FILL MODE FLICKERING INVESTIGATION
    // ============================================================================
    static int video_frame_call_count = 0;
    video_frame_call_count++;
    
    std::cout << "DEBUG: *** SDL2 VIDEO FRAME DEBUG *** ====== render_video_frame CALL " << video_frame_call_count << " ======" << std::endl;
    std::cout << "DEBUG: *** SDL2 VIDEO FRAME DEBUG *** Requested scaling: " << static_cast<int>(scaling) << " (0=STRETCH, 1=FIT, 2=FILL, 3=DEFAULT)" << std::endl;
    std::cout << "DEBUG: *** SDL2 VIDEO FRAME DEBUG *** Previous current_scaling_: " << static_cast<int>(current_scaling_) << std::endl;
    
    std::cout << "DEBUG: SDL2 rendering video frame: " << frame_width << "x" << frame_height << std::endl;
    
    // Create texture from frame data
    if (!create_texture_from_data(frame_data, frame_width, frame_height)) {
        std::cerr << "ERROR: Failed to create texture from video frame data" << std::endl;
        return false;
    }
    
    std::cout << "DEBUG: *** SDL2 VIDEO FRAME DEBUG *** Texture created successfully, about to render" << std::endl;
    
    // Clear screen and render
    SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 255);
    SDL_RenderClear(renderer_);
    
    // Store the scaling mode before rendering (THIS COULD BE THE ISSUE!)
    current_scaling_ = scaling;
    std::cout << "DEBUG: *** SDL2 VIDEO FRAME DEBUG *** Updated current_scaling_ to: " << static_cast<int>(current_scaling_) << std::endl;
    
    render_current_texture(scaling);
    
    SDL_RenderPresent(renderer_);
    
    std::cout << "DEBUG: *** SDL2 VIDEO FRAME DEBUG *** Frame rendered and presented successfully" << std::endl;
    std::cout << "DEBUG: *** SDL2 VIDEO FRAME DEBUG *** ====== render_video_frame CALL " << video_frame_call_count << " COMPLETE ======" << std::endl;
    
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
    
    // Create renderer
    renderer_ = SDL_CreateRenderer(window_, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer_) {
        std::cerr << "ERROR: Failed to create SDL2 renderer: " << SDL_GetError() << std::endl;
        return false;
    }
    
    // Set renderer blend mode for transparency support
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    
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
    
    // Destroy existing texture
    if (current_texture_) {
        SDL_DestroyTexture(current_texture_);
        current_texture_ = nullptr;
    }
    
    // Create new texture (assuming RGBA format)
    current_texture_ = SDL_CreateTexture(
        renderer_,
        SDL_PIXELFORMAT_RGBA32,
        SDL_TEXTUREACCESS_STATIC,
        width, height
    );
    
    if (!current_texture_) {
        std::cerr << "ERROR: Failed to create SDL2 texture: " << SDL_GetError() << std::endl;
        return false;
    }
    
    // Upload data to texture
    if (SDL_UpdateTexture(current_texture_, nullptr, data, width * 4) < 0) {
        std::cerr << "ERROR: Failed to update SDL2 texture: " << SDL_GetError() << std::endl;
        SDL_DestroyTexture(current_texture_);
        current_texture_ = nullptr;
        return false;
    }
    
    return true;
}

void SDL2WindowDisplay::render_current_texture(ScalingMode scaling) {
    if (!current_texture_ || !renderer_) {
        return;
    }
    
    // ============================================================================
    // ENHANCED SDL2 RENDER DEBUG FOR FILL MODE FLICKERING INVESTIGATION
    // ============================================================================
    static int render_call_count = 0;
    render_call_count++;
    
    std::cout << "DEBUG: *** SDL2 RENDER DEBUG *** ====== render_current_texture CALL " << render_call_count << " ======" << std::endl;
    std::cout << "DEBUG: *** SDL2 RENDER DEBUG *** Requested scaling mode: " << static_cast<int>(scaling) << " (0=STRETCH, 1=FIT, 2=FILL, 3=DEFAULT)" << std::endl;
    std::cout << "DEBUG: *** SDL2 RENDER DEBUG *** Current stored scaling (current_scaling_): " << static_cast<int>(current_scaling_) << std::endl;
    
    // Check for scaling mode inconsistency
    if (current_scaling_ != scaling) {
        std::cout << "DEBUG: *** SDL2 RENDER DEBUG *** *** SCALING MODE MISMATCH DETECTED! ***" << std::endl;
        std::cout << "DEBUG: *** SDL2 RENDER DEBUG *** *** current_scaling_=" << static_cast<int>(current_scaling_) << " != requested scaling=" << static_cast<int>(scaling) << " ***" << std::endl;
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
    
    std::cout << "DEBUG: *** SDL2 RENDER DEBUG *** About to SDL_RenderCopy with rect: x=" << dst_rect.x << ", y=" << dst_rect.y << ", w=" << dst_rect.w << ", h=" << dst_rect.h << std::endl;
    
    // ============================================================================
    // CRITICAL Y-AXIS ORIENTATION FIX - DO NOT MODIFY OR REMOVE!
    // Render texture without vertical flip - FFmpeg output is correctly oriented
    // ============================================================================
    SDL_RenderCopy(renderer_, current_texture_, nullptr, &dst_rect);
    
    std::cout << "DEBUG: *** SDL2 RENDER DEBUG *** SDL_RenderCopy completed successfully" << std::endl;
    std::cout << "DEBUG: *** SDL2 RENDER DEBUG *** ====== render_current_texture CALL " << render_call_count << " COMPLETE ======" << std::endl;
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
    
    // ============================================================================
    // ENHANCED SDL2 SCALING DEBUG FOR FILL MODE FLICKERING INVESTIGATION
    // ============================================================================
    static int debug_call_count = 0;
    debug_call_count++;
    
    std::cout << "DEBUG: *** SDL2 SCALING DEBUG *** ====== calculate_scaled_rect CALL " << debug_call_count << " ======" << std::endl;
    std::cout << "DEBUG: *** SDL2 SCALING DEBUG *** Scaling mode: " << static_cast<int>(scaling) << " (0=STRETCH, 1=FIT, 2=FILL, 3=DEFAULT)" << std::endl;
    
    // Calculate aspect ratios
    float src_aspect = static_cast<float>(src_width) / src_height;
    float dst_aspect = static_cast<float>(dst_width) / dst_height;
    std::cout << "DEBUG: *** SDL2 SCALING DEBUG *** Source aspect: " << src_aspect << ", Destination aspect: " << dst_aspect << std::endl;
    
    switch (scaling) {
        case ScalingMode::STRETCH:
            std::cout << "DEBUG: *** SDL2 SCALING DEBUG *** Using STRETCH mode" << std::endl;
            dst_rect.x = 0;
            dst_rect.y = 0;
            dst_rect.w = dst_width;
            dst_rect.h = dst_height;
            break;
            
        case ScalingMode::FIT:
        case ScalingMode::DEFAULT: {
            std::cout << "DEBUG: *** SDL2 SCALING DEBUG *** Using FIT/DEFAULT mode" << std::endl;
            
            if (src_aspect > dst_aspect) {
                // Source is wider, fit to width
                dst_rect.w = dst_width;
                dst_rect.h = static_cast<int>(dst_width / src_aspect);
                dst_rect.x = 0;
                dst_rect.y = (dst_height - dst_rect.h) / 2;
                std::cout << "DEBUG: *** SDL2 SCALING DEBUG *** FIT: Source wider - fit to width" << std::endl;
            } else {
                // Source is taller, fit to height
                dst_rect.h = dst_height;
                dst_rect.w = static_cast<int>(dst_height * src_aspect);
                dst_rect.x = (dst_width - dst_rect.w) / 2;
                dst_rect.y = 0;
                std::cout << "DEBUG: *** SDL2 SCALING DEBUG *** FIT: Source taller - fit to height" << std::endl;
            }
            break;
        }
            
        case ScalingMode::FILL: {
            std::cout << "DEBUG: *** SDL2 SCALING DEBUG *** Using FILL mode - crop to fill destination" << std::endl;
            
            if (src_aspect > dst_aspect) {
                // Source is wider, crop horizontally
                dst_rect.h = dst_height;
                dst_rect.w = static_cast<int>(dst_height * src_aspect);
                dst_rect.x = -(dst_rect.w - dst_width) / 2;
                dst_rect.y = 0;
                std::cout << "DEBUG: *** SDL2 SCALING DEBUG *** FILL: Source wider - crop horizontally" << std::endl;
                std::cout << "DEBUG: *** SDL2 SCALING DEBUG *** FILL: render_rect=" << dst_rect.w << "x" << dst_rect.h << ", offset_x=" << dst_rect.x << " (NEGATIVE - cropping)" << std::endl;
            } else {
                // Source is taller, crop vertically
                dst_rect.w = dst_width;
                dst_rect.h = static_cast<int>(dst_width / src_aspect);
                dst_rect.x = 0;
                dst_rect.y = -(dst_rect.h - dst_height) / 2;
                std::cout << "DEBUG: *** SDL2 SCALING DEBUG *** FILL: Source taller - crop vertically" << std::endl;
                std::cout << "DEBUG: *** SDL2 SCALING DEBUG *** FILL: render_rect=" << dst_rect.w << "x" << dst_rect.h << ", offset_y=" << dst_rect.y << " (NEGATIVE - cropping)" << std::endl;
            }
            break;
        }
    }
    
    std::cout << "DEBUG: *** SDL2 SCALING DEBUG *** Final destination rect: x=" << dst_rect.x << ", y=" << dst_rect.y << ", w=" << dst_rect.w << ", h=" << dst_rect.h << std::endl;
    std::cout << "DEBUG: *** SDL2 SCALING DEBUG *** ====== calculate_scaled_rect CALL " << debug_call_count << " COMPLETE ======" << std::endl;
}
