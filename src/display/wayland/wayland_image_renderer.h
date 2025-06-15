#pragma once

#include "../display_manager.h"
#include <wayland-client.h>
#include <wayland-egl.h>
#include <memory>

// Forward declarations
struct wl_surface;
struct wl_buffer;

class WaylandImageRenderer {
public:
    WaylandImageRenderer();
    ~WaylandImageRenderer();
    
    // Initialize the renderer for SHM-based CPU rendering only
    bool initialize();
    
    // Clean up resources
    void cleanup();
    
    // Render static image using SHM (CPU-based - primary method)
    bool render_image_shm(const unsigned char* image_data, int img_width, int img_height,
                         void* shm_data, int surface_width, int surface_height,
                         ScalingMode scaling, bool windowed_mode = false);
    
    // Utility functions for image processing
    bool load_image_from_file(const std::string& image_path, unsigned char** image_data,
                             int* width, int* height);
    
    void free_image_data(unsigned char* image_data);
    
    // Check if image needs resizing for compatibility and resize if needed
    void check_and_resize_image(const unsigned char* src_data, int src_width, int src_height,
                               unsigned char** dst_data, int* dst_width, int* dst_height);

private:
    bool initialized_;
    
    // Image processing utilities
    void apply_scaling_shm(const unsigned char* src_data, int src_width, int src_height,
                          unsigned char* dst_data, int dst_width, int dst_height,
                          ScalingMode scaling, bool windowed_mode = false);
    
    // Maximum texture size for compatibility
    static constexpr int MAX_TEXTURE_SIZE = 4096;
};
