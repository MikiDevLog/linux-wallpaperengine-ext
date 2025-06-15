#pragma once

#include "../display_manager.h"
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GL/glew.h>
#include <GL/gl.h>
#include <memory>

class X11ImageRenderer {
public:
    X11ImageRenderer();
    ~X11ImageRenderer();
    
    // Initialize the renderer with X11 display and optional EGL context
    bool initialize(Display* x11_display, Window window, int screen);
    bool initialize_egl(Display* x11_display, Window window, int screen,
                       EGLDisplay egl_display, EGLConfig egl_config, EGLContext egl_context);
    
    // Clean up resources
    void cleanup();
    
    // Render static image using X11 (CPU-based)
    bool render_image_x11(const unsigned char* image_data, int img_width, int img_height,
                         int surface_width, int surface_height, ScalingMode scaling,
                         bool windowed_mode = true);
    
    // Render static image using EGL/OpenGL (GPU-accelerated)
    bool render_image_egl(const unsigned char* image_data, int img_width, int img_height,
                         EGLSurface egl_surface, int surface_width, int surface_height,
                         ScalingMode scaling);
    
    // Utility functions for image processing
    bool load_image_from_file(const std::string& image_path, unsigned char** image_data,
                             int* width, int* height);
    
    void free_image_data(unsigned char* image_data);
    
    // Check if image needs resizing for compatibility and resize if needed
    void check_and_resize_image(const unsigned char* src_data, int src_width, int src_height,
                               unsigned char** dst_data, int* dst_width, int* dst_height,
                               bool windowed_mode = true);

private:
    bool initialized_;
    bool egl_mode_;
    
    // X11 context
    Display* x11_display_;
    Window window_;
    int screen_;
    GC graphics_context_;
    
    // EGL context (optional for GPU acceleration)
    EGLDisplay egl_display_;
    EGLConfig egl_config_;
    EGLContext egl_context_;
    EGLSurface egl_surface_;
    
    // OpenGL state
    bool glew_initialized_;
    
    // Image processing utilities
    void apply_scaling_x11(const unsigned char* src_data, int src_width, int src_height,
                          unsigned char* dst_data, int dst_width, int dst_height,
                          ScalingMode scaling, int bytes_per_pixel, bool windowed_mode = true);
    
    void process_image_for_opengl(const unsigned char* src_data, int width, int height,
                                 unsigned char** processed_data);
    
    void create_opengl_texture(const unsigned char* image_data, int width, int height,
                              unsigned int* texture_id);
    
    void render_textured_quad(int img_width, int img_height, int surface_width, int surface_height,
                             ScalingMode scaling);
    
    bool init_glew_if_needed();
    
    // X11 specific utilities
    XImage* create_ximage(const unsigned char* image_data, int width, int height);
    void convert_rgba_to_x11_format(const unsigned char* src_data, int width, int height,
                                   unsigned char** dst_data, int* bytes_per_pixel);
    
    // Maximum texture size for compatibility
    static constexpr int MAX_TEXTURE_SIZE = 4096;
};
