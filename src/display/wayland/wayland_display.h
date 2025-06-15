#pragma once

#include "../display_manager.h"
#include "wayland_image_renderer.h"
#include "wayland_video_renderer.h"
#include <wayland-client.h>
#include <wayland-egl.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GL/glew.h>

// Forward declarations
class MediaPlayer;
#include <GL/gl.h>
#include <vector>
#include <memory>

// Forward declarations for protocols
struct xdg_wm_base;
struct xdg_surface;
struct xdg_toplevel;
struct wl_shm;
struct wl_shm_pool;
struct wl_buffer;
struct zwlr_layer_shell_v1;
struct zwlr_layer_surface_v1;
struct wl_output;

class WaylandDisplay : public DisplayOutput {
public:
    WaylandDisplay(const std::string& output_name);
    WaylandDisplay(int x, int y, int width, int height); // For windowed mode
    ~WaylandDisplay() override;
    
    bool initialize() override;
    void cleanup() override;
    bool set_background(const std::string& media_path, ScalingMode scaling) override;
    void update() override;
    std::string get_name() const override;
    
    // Image rendering method
    bool render_image_data(const unsigned char* image_data, int img_width, int img_height, ScalingMode scaling);
    
    // Video frame rendering method
    bool render_video_frame(const unsigned char* frame_data, int frame_width, int frame_height, ScalingMode scaling);
    
    // Enhanced video rendering with native FFmpeg support
    bool render_video_enhanced(MediaPlayer* media_player, ScalingMode scaling);
    
    // OpenGL context management
    bool make_egl_current();
    
    // Static factory methods
    static std::vector<std::unique_ptr<DisplayOutput>> get_outputs();
    static std::unique_ptr<DisplayOutput> get_output_by_name(const std::string& name);
    static std::unique_ptr<DisplayOutput> create_window(int x, int y, int width, int height);

private:
    std::string output_name_;
    struct wl_display* display_;
    struct wl_registry* registry_;
    struct wl_compositor* compositor_;
    struct wl_surface* surface_;
    struct wl_output* output_;
    uint32_t output_registry_name_;
    
    // XDG Shell (for windows)
    struct xdg_wm_base* xdg_wm_base_;
    struct xdg_surface* xdg_surface_;
    struct xdg_toplevel* xdg_toplevel_;
    
    // Layer Shell (for backgrounds)
    struct zwlr_layer_shell_v1* layer_shell_;
    struct zwlr_layer_surface_v1* layer_surface_;
    
    // EGL context for OpenGL rendering
    EGLDisplay egl_display_;
    EGLContext egl_context_;
    EGLConfig egl_config_;
    EGLSurface egl_surface_;
    struct wl_egl_window* egl_window_;
    bool egl_initialized_;
    
    // SHM buffer (fallback for simple rendering)
    struct wl_shm* shm_;
    struct wl_shm_pool* shm_pool_;
    struct wl_buffer* buffer_;
    void* shm_data_;
    int shm_fd_;
    size_t shm_size_;
    
    // Specialized renderers
    std::unique_ptr<WaylandImageRenderer> image_renderer_;
    std::unique_ptr<WaylandVideoRenderer> video_renderer_;          // CPU-based video rendering
    
    // Frame callback for continuous rendering
    struct wl_callback* frame_callback_;
    bool frame_callback_pending_;
    
    // Configuration
    bool windowed_mode_;
    bool use_layer_shell_;
    bool prefer_egl_;
    int x_, y_, width_, height_;
    int output_width_, output_height_;
    int scale_factor_;
    ScalingMode current_scaling_;
    
    // Pending render data (for frame callback rendering)
    const unsigned char* pending_image_data_;
    int pending_image_width_;
    int pending_image_height_;
    ScalingMode pending_scaling_;
    bool has_pending_render_;
    
    // Initialization methods
    bool init_wayland();
    bool init_egl();
    bool init_window_mode();
    bool init_background_mode();
    bool create_shm_buffer();
    void setup_layer_surface();
    bool init_renderers();
    
    // Rendering methods (now delegate to specialized renderers)
    bool render_with_egl(const unsigned char* data, int width, int height, ScalingMode scaling, bool is_video = false);
    bool render_with_shm(const unsigned char* data, int width, int height, ScalingMode scaling, bool is_video = false);
    
    // Frame callback methods
    void request_frame_callback();
    void handle_frame_callback();
    
    // Utility methods
    void cleanup_egl();
    void cleanup_shm();
    bool find_output_by_name();
    bool find_and_configure_output();
    bool create_layer_surface();
    bool init_glew();
    
public:
    // Wayland event handlers
    static void registry_global(void* data, struct wl_registry* registry,
                               uint32_t name, const char* interface, uint32_t version);
    static void registry_global_remove(void* data, struct wl_registry* registry, uint32_t name);
    
    // XDG Shell event handlers (for window mode)
    static void xdg_wm_base_ping(void* data, struct xdg_wm_base* xdg_wm_base, uint32_t serial);
    static void xdg_surface_configure(void* data, struct xdg_surface* xdg_surface, uint32_t serial);
    static void xdg_toplevel_configure(void* data, struct xdg_toplevel* xdg_toplevel,
                                      int32_t width, int32_t height, struct wl_array* states);
    static void xdg_toplevel_close(void* data, struct xdg_toplevel* xdg_toplevel);
    
    // Layer Shell event handlers (for background mode)
    static void layer_surface_configure(void* data, struct zwlr_layer_surface_v1* layer_surface,
                                       uint32_t serial, uint32_t width, uint32_t height);
    static void layer_surface_closed(void* data, struct zwlr_layer_surface_v1* layer_surface);
    
    // Frame callback handler
    static void frame_callback_done(void* data, struct wl_callback* callback, uint32_t time);
    
    // Output event handlers
    static void output_geometry(void* data, struct wl_output* output,
                               int32_t x, int32_t y, int32_t physical_width, int32_t physical_height,
                               int32_t subpixel, const char* make, const char* model, int32_t transform);
    static void output_mode(void* data, struct wl_output* output, uint32_t flags,
                           int32_t width, int32_t height, int32_t refresh);
    static void output_done(void* data, struct wl_output* output);
    static void output_scale(void* data, struct wl_output* output, int32_t factor);
    static void output_name(void* data, struct wl_output* output, const char* name);
    static void output_description(void* data, struct wl_output* output, const char* description);
};
