#pragma once

#include "../display_manager.h"
#include <X11/Xlib.h>
#include <X11/extensions/Xrandr.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <vector>
#include <memory>

// Forward declarations for specialized renderers
class X11ImageRenderer;
class X11VideoRenderer;

class X11Display : public DisplayOutput {
public:
    X11Display(const std::string& output_name);
    X11Display(int x, int y, int width, int height); // For windowed mode
    ~X11Display() override;
    
    bool initialize() override;
    void cleanup() override;
    bool set_background(const std::string& media_path, ScalingMode scaling) override;
    void update() override;
    std::string get_name() const override;
    
    // X11 specific methods for MPV integration
    Display* get_x11_display() const { return display_; }
    Window get_x11_window() const { return windowed_mode_ ? window_ : root_window_; }
    int get_x11_screen() const { return screen_; }
    bool is_windowed_mode() const { return windowed_mode_; }
    
    // Image rendering using specialized renderer
    bool render_image_data(const unsigned char* image_data, int img_width, int img_height, ScalingMode scaling);
    
    // Video frame rendering using specialized renderer
    bool render_video_frame(const unsigned char* frame_data, int frame_width, int frame_height, ScalingMode scaling);
    
    // EGL context management for GPU acceleration
    bool initialize_egl();
    bool make_egl_current();
    void cleanup_egl();
    
    // Static factory methods
    static std::vector<std::unique_ptr<DisplayOutput>> get_outputs();
    static std::unique_ptr<DisplayOutput> get_output_by_name(const std::string& name);
    static std::unique_ptr<DisplayOutput> create_window(int x, int y, int width, int height);

private:
    std::string output_name_;
    Display* display_;
    Window root_window_;
    Window window_; // For windowed mode
    int screen_;
    
    // Window mode specific
    bool windowed_mode_;
    int x_, y_, width_, height_;
    
    // Background mode image buffer (like reference implementation)
    char* image_data_;
    uint32_t image_size_;
    XImage* ximage_;
    Pixmap pixmap_;
    GC gc_;
    
    // EGL context for GPU acceleration (optional)
    bool egl_initialized_;
    bool prefer_egl_;
    
    // Image buffer utilities (for background mode)
    bool init_image_buffer();
    void cleanup_image_buffer();
    void update_background_from_buffer();
    bool render_to_image_buffer(const unsigned char* image_data, int img_width, int img_height, ScalingMode scaling);
    EGLDisplay egl_display_;
    EGLConfig egl_config_;
    EGLContext egl_context_;
    EGLSurface egl_surface_;
    
    // Specialized renderers
    std::unique_ptr<X11ImageRenderer> image_renderer_;
    std::unique_ptr<X11VideoRenderer> video_renderer_;        // CPU-based video rendering
    
    // Current state
    ScalingMode current_scaling_;
    
    bool init_x11();
    bool init_window_mode();
    bool init_background_mode();
    void set_window_background(const std::string& media_path);
    
    // EGL helpers
    bool create_egl_context();
    bool choose_egl_config();
    bool create_egl_surface();
};
