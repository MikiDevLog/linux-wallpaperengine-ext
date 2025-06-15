#pragma once

#include "../display_manager.h"
#include <wayland-client.h>
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

// Forward declarations
struct wl_surface;
struct wl_buffer;

class WaylandVideoRenderer {
public:
    WaylandVideoRenderer();
    ~WaylandVideoRenderer();
    
    // Initialize the CPU-based renderer with Wayland display and SHM
    bool initialize(struct wl_display* wayland_display, struct wl_shm* shm);
    
    // Clean up resources
    void cleanup();
    
    // Setup FFmpeg integration for CPU-based video decoding
    bool initialize_ffmpeg(const std::string& video_path);
    
    // Render video frame using SHM (CPU) - uses FFmpeg decoder
    bool render_video_shm(void* shm_data, int surface_width, int surface_height,
                         ScalingMode scaling);
    
    // Handle video frame data directly (for non-FFmpeg sources)
    bool render_rgb_frame_shm(const unsigned char* frame_data, int frame_width, int frame_height,
                             void* shm_data, int surface_width, int surface_height,
                             ScalingMode scaling, bool windowed_mode = false);
    
    bool render_frame_data_shm(const unsigned char* frame_data, int frame_width, int frame_height,
                              void* shm_data, int surface_width, int surface_height,
                              ScalingMode scaling, bool windowed_mode = false);
    
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
    
    // Wayland context
    struct wl_display* wayland_display_;
    struct wl_shm* shm_;
    
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
    
    void apply_scaling_shm(const unsigned char* src_data, int src_width, int src_height,
                          unsigned char* dst_data, int dst_width, int dst_height,
                          ScalingMode scaling, bool windowed_mode = false);
};
