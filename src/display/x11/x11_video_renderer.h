#pragma once

#include "../display_manager.h"
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <memory>
#include <functional>
#include <string>
#include <cstdint>

// Forward declarations for FFmpeg types
struct AVFormatContext;
struct AVCodecContext;
struct AVCodec;
struct AVFrame;
struct SwsContext;

class X11VideoRenderer {
public:
    X11VideoRenderer();
    ~X11VideoRenderer();
    
    // Initialize the CPU-based renderer with X11 display (no EGL context needed)
    bool initialize(Display* x11_display, Window window, int screen);
    
    // Note: EGL initialization has been moved to X11GpuVideoRenderer class
    
    // Clean up resources
    void cleanup();
    
    // Setup FFmpeg integration for CPU-based video decoding
    bool initialize_ffmpeg(const std::string& video_path);
    
    // Render video frame using X11 SHM (CPU-based)
    bool render_video_shm(int surface_width, int surface_height, ScalingMode scaling);
    
    // Render video frame using X11 (CPU-based)
    bool render_video_x11(int surface_width, int surface_height, ScalingMode scaling);
    
    // Handle video frame data directly (for non-MPV sources)
    bool render_rgb_frame_x11(const unsigned char* frame_data, int frame_width, int frame_height,
                              int surface_width, int surface_height, ScalingMode scaling,
                              bool windowed_mode = true);
    
    // CPU video event handling
    void handle_video_events();
    
    // Get current video dimensions from FFmpeg
    bool get_video_dimensions(int* width, int* height);
    
    // Video seeking
    void seek_to_time(double time_seconds);
    
    // Set render callback for frame updates
    using RenderCallback = std::function<void()>;
    void set_render_callback(RenderCallback callback);

private:
    bool initialized_;
    
    // X11 context
    Display* x11_display_;
    Window window_;
    int screen_;
    GC graphics_context_;
    
    // FFmpeg context for CPU-based video decoding
    struct AVFormatContext* format_context_;
    struct AVCodecContext* codec_context_;
    const struct AVCodec* codec_;
    struct AVFrame* frame_;
    struct AVFrame* rgb_frame_;
    struct SwsContext* sws_context_;
    int stream_index_;
    uint8_t* frame_buffer_;
    
    // Render callback
    RenderCallback render_callback_;
    
    // Private CPU methods
    void cleanup_ffmpeg();
    
    void apply_scaling_x11(const unsigned char* src_data, int src_width, int src_height,
                          unsigned char* dst_data, int dst_width, int dst_height,
                          ScalingMode scaling, int bytes_per_pixel, bool windowed_mode = true);
    
    // X11 specific utilities
    void convert_bgra_to_x11_format(const unsigned char* src_data, int width, int height,
                                   unsigned char** dst_data, int* bytes_per_pixel);
};
