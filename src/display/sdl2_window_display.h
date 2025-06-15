#pragma once

#include "display_manager.h"
#include <SDL2/SDL.h>
#include <memory>
#include <string>

/**
 * SDL2-based window implementation
 * Provides universal cross-platform windowing for all platforms (X11/Wayland/Windows/macOS)
 * Replaces Qt and native window implementations with a single, consistent solution
 */
class SDL2WindowDisplay : public DisplayOutput {
public:
    SDL2WindowDisplay(int x, int y, int width, int height);
    ~SDL2WindowDisplay() override;
    
    bool initialize() override;
    void cleanup() override;
    bool set_background(const std::string& media_path, ScalingMode scaling) override;
    void update() override;
    std::string get_name() const override;
    
    // Image rendering method
    bool render_image_data(const unsigned char* image_data, int img_width, int img_height, ScalingMode scaling);
    
    // Video frame rendering method  
    bool render_video_frame(const unsigned char* frame_data, int frame_width, int frame_height, ScalingMode scaling);
    
    // Static factory method
    static std::unique_ptr<DisplayOutput> create_window(int x, int y, int width, int height);
    
    // Window management
    void show_window();
    void hide_window();
    bool is_visible() const;
    
    // Event handling
    void handle_events();
    bool should_close() const;

private:
    // Window properties
    int x_, y_, width_, height_;
    bool initialized_;
    bool visible_;
    bool should_close_;
    
    // SDL2 objects
    SDL_Window* window_;
    SDL_Renderer* renderer_;
    SDL_Texture* current_texture_;
    
    // Rendering state
    std::string current_media_path_;
    ScalingMode current_scaling_;
    
    // Private methods
    bool init_sdl();
    bool create_window();
    void cleanup_sdl();
    
    // Rendering helpers
    bool create_texture_from_data(const unsigned char* data, int width, int height);
    void render_current_texture(ScalingMode scaling);
    void calculate_scaled_rect(int src_width, int src_height, int dst_width, int dst_height, 
                              ScalingMode scaling, SDL_Rect& dst_rect);
};
