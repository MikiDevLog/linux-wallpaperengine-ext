#include "wayland_display.h"
#include "../../media_player.h"
#include <iostream>
#include <cstring>
#include <unistd.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <cmath>
#include <chrono>
#include <thread>

// Import protocol headers
extern "C" {
#include "xdg-shell-client-protocol.h"
}

// Use our wrapper to deal with namespace keyword issues
#include "wlr_layer_shell_wrapper.h"

// Wayland registry listener
static const struct wl_registry_listener registry_listener = {
    WaylandDisplay::registry_global,
    WaylandDisplay::registry_global_remove
};

// XDG WM Base listener
static const struct xdg_wm_base_listener xdg_wm_base_listener = {
    WaylandDisplay::xdg_wm_base_ping
};

// XDG Surface listener
static const struct xdg_surface_listener xdg_surface_listener = {
    WaylandDisplay::xdg_surface_configure
};

// XDG Toplevel listener
static const struct xdg_toplevel_listener xdg_toplevel_listener = {
    WaylandDisplay::xdg_toplevel_configure,
    WaylandDisplay::xdg_toplevel_close
};

// Layer Surface listener
static const struct zwlr_layer_surface_v1_listener layer_surface_listener = {
    WaylandDisplay::layer_surface_configure,
    WaylandDisplay::layer_surface_closed
};

// Output listener
static const struct wl_output_listener output_listener = {
    WaylandDisplay::output_geometry,
    WaylandDisplay::output_mode,
    WaylandDisplay::output_done,
    WaylandDisplay::output_scale,
    WaylandDisplay::output_name,
    WaylandDisplay::output_description
};

WaylandDisplay::WaylandDisplay(const std::string& output_name)
    : output_name_(output_name), display_(nullptr), registry_(nullptr), 
      compositor_(nullptr), surface_(nullptr), output_(nullptr), output_registry_name_(0),
      xdg_wm_base_(nullptr), xdg_surface_(nullptr), xdg_toplevel_(nullptr),
      layer_shell_(nullptr), layer_surface_(nullptr),
      egl_display_(EGL_NO_DISPLAY), egl_context_(EGL_NO_CONTEXT), 
      egl_config_(nullptr), egl_surface_(EGL_NO_SURFACE), egl_window_(nullptr),
      egl_initialized_(false), shm_(nullptr), shm_pool_(nullptr), buffer_(nullptr),
      shm_data_(nullptr), shm_fd_(-1), shm_size_(0),
      image_renderer_(std::make_unique<WaylandImageRenderer>()),
      video_renderer_(std::make_unique<WaylandVideoRenderer>()),
      frame_callback_(nullptr), frame_callback_pending_(false),
      windowed_mode_(false), use_layer_shell_(true), prefer_egl_(true),
      x_(0), y_(0), width_(800), height_(600),
      output_width_(0), output_height_(0), scale_factor_(1),
      current_scaling_(ScalingMode::DEFAULT),
      pending_image_data_(nullptr), pending_image_width_(0), pending_image_height_(0), 
      pending_scaling_(ScalingMode::DEFAULT), has_pending_render_(false) {}

WaylandDisplay::WaylandDisplay(int x, int y, int width, int height)
    : output_name_("window"), display_(nullptr), registry_(nullptr),
      compositor_(nullptr), surface_(nullptr), output_(nullptr), output_registry_name_(0),
      xdg_wm_base_(nullptr), xdg_surface_(nullptr), xdg_toplevel_(nullptr),
      layer_shell_(nullptr), layer_surface_(nullptr),
      egl_display_(EGL_NO_DISPLAY), egl_context_(EGL_NO_CONTEXT),
      egl_config_(nullptr), egl_surface_(EGL_NO_SURFACE), egl_window_(nullptr),
      egl_initialized_(false), shm_(nullptr), shm_pool_(nullptr), buffer_(nullptr),
      shm_data_(nullptr), shm_fd_(-1), shm_size_(0),
      image_renderer_(std::make_unique<WaylandImageRenderer>()),
      video_renderer_(std::make_unique<WaylandVideoRenderer>()),
      frame_callback_(nullptr), frame_callback_pending_(false),
      windowed_mode_(true), use_layer_shell_(false), prefer_egl_(true),
      x_(x), y_(y), width_(width), height_(height),
      output_width_(width), output_height_(height), scale_factor_(1),
      current_scaling_(ScalingMode::DEFAULT),
      pending_image_data_(nullptr), pending_image_width_(0), pending_image_height_(0), 
      pending_scaling_(ScalingMode::DEFAULT), has_pending_render_(false) {}

WaylandDisplay::~WaylandDisplay() {
    cleanup();
}

bool WaylandDisplay::initialize() {
    if (!init_wayland()) {
        std::cerr << "Failed to initialize Wayland connection" << std::endl;
        return false;
    }
    
    if (windowed_mode_) {
        if (!init_window_mode()) {
            std::cerr << "Failed to initialize window mode" << std::endl;
            return false;
        }
    } else {
        if (!init_background_mode()) {
            std::cerr << "Failed to initialize background mode" << std::endl;
            return false;
        }
    }
    
    return true;
}

bool WaylandDisplay::init_wayland() {
    display_ = wl_display_connect(nullptr);
    if (!display_) {
        std::cerr << "Failed to connect to Wayland display" << std::endl;
        return false;
    }
    
    registry_ = wl_display_get_registry(display_);
    if (!registry_) {
        std::cerr << "Failed to get Wayland registry" << std::endl;
        return false;
    }
    
    wl_registry_add_listener(registry_, &registry_listener, this);
    wl_display_dispatch(display_);
    wl_display_roundtrip(display_);
    
    if (!compositor_) {
        std::cerr << "Wayland compositor not available" << std::endl;
        return false;
    }
    
    if (!windowed_mode_ && !layer_shell_) {
        std::cerr << "Layer shell not available, falling back to SHM rendering" << std::endl;
        use_layer_shell_ = false;
    }
    
    // Try to initialize EGL, fallback to SHM if it fails
    if (prefer_egl_ && !init_egl()) {
        std::cout << "EGL initialization failed, using SHM fallback" << std::endl;
        prefer_egl_ = false;
    }
    
    return true;
}

bool WaylandDisplay::init_egl() {
    std::cout << "DEBUG: Initializing EGL for Wayland" << std::endl;
    
    // Get EGL platform display with explicit Wayland platform
    PFNEGLGETPLATFORMDISPLAYEXTPROC eglGetPlatformDisplayEXT = 
        (PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress("eglGetPlatformDisplayEXT");
    
    if (eglGetPlatformDisplayEXT) {
        egl_display_ = eglGetPlatformDisplayEXT(EGL_PLATFORM_WAYLAND_EXT, display_, nullptr);
    } else {
        egl_display_ = eglGetDisplay((EGLNativeDisplayType)display_);
    }
    
    if (egl_display_ == EGL_NO_DISPLAY) {
        std::cerr << "ERROR: Failed to get EGL display" << std::endl;
        return false;
    }
    
    EGLint major, minor;
    if (!eglInitialize(egl_display_, &major, &minor)) {
        std::cerr << "ERROR: Failed to initialize EGL: " << eglGetError() << std::endl;
        return false;
    }
    
    std::cout << "DEBUG: EGL initialized. Version: " << major << "." << minor << std::endl;
    
    // Bind OpenGL API
    if (!eglBindAPI(EGL_OPENGL_API)) {
        std::cerr << "ERROR: Failed to bind OpenGL API: " << eglGetError() << std::endl;
        return false;
    }
    
    // Configure EGL
    EGLint config_attribs[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_DEPTH_SIZE, 0,
        EGL_STENCIL_SIZE, 0,
        EGL_NONE
    };
    
    EGLint num_configs;
    EGLConfig configs[32];
    
    if (!eglChooseConfig(egl_display_, config_attribs, configs, 32, &num_configs)) {
        std::cerr << "ERROR: Failed to choose EGL config: " << eglGetError() << std::endl;
        return false;
    }
    
    if (num_configs == 0) {
        std::cerr << "ERROR: No suitable EGL configs found" << std::endl;
        return false;
    }
    
    egl_config_ = configs[0];
    
    // Create EGL context
    EGLint context_attribs[] = {
        EGL_CONTEXT_MAJOR_VERSION, 2,
        EGL_CONTEXT_MINOR_VERSION, 1,
        EGL_NONE
    };
    
    egl_context_ = eglCreateContext(egl_display_, egl_config_, EGL_NO_CONTEXT, context_attribs);
    if (egl_context_ == EGL_NO_CONTEXT) {
        std::cerr << "ERROR: Failed to create EGL context: " << eglGetError() << std::endl;
        return false;
    }
    
    egl_initialized_ = true;
    std::cout << "DEBUG: EGL context created successfully" << std::endl;
    return true;
}

bool WaylandDisplay::init_window_mode() {
    if (!xdg_wm_base_) {
        std::cerr << "XDG WM Base not available for window mode" << std::endl;
        return false;
    }
    
    surface_ = wl_compositor_create_surface(compositor_);
    if (!surface_) {
        std::cerr << "Failed to create Wayland surface" << std::endl;
        return false;
    }
    
    xdg_surface_ = xdg_wm_base_get_xdg_surface(xdg_wm_base_, surface_);
    if (!xdg_surface_) {
        std::cerr << "Failed to create XDG surface" << std::endl;
        return false;
    }
    
    xdg_surface_add_listener(xdg_surface_, &xdg_surface_listener, this);
    
    xdg_toplevel_ = xdg_surface_get_toplevel(xdg_surface_);
    if (!xdg_toplevel_) {
        std::cerr << "Failed to create XDG toplevel" << std::endl;
        return false;
    }
    
    xdg_toplevel_add_listener(xdg_toplevel_, &xdg_toplevel_listener, this);
    xdg_toplevel_set_title(xdg_toplevel_, "Linux Wallpaper Engine Extended");
    
    // Set initial window geometry
    xdg_surface_set_window_geometry(xdg_surface_, 0, 0, width_, height_);
    
    // Set up rendering buffers BEFORE committing
    if (prefer_egl_ && egl_initialized_) {
        egl_window_ = wl_egl_window_create(surface_, width_, height_);
        if (!egl_window_) {
            std::cerr << "ERROR: Failed to create wl_egl_window" << std::endl;
            prefer_egl_ = false;
        } else {
            egl_surface_ = eglCreateWindowSurface(egl_display_, egl_config_, 
                                                 (EGLNativeWindowType)egl_window_, nullptr);
            if (egl_surface_ == EGL_NO_SURFACE) {
                std::cerr << "ERROR: Failed to create EGL window surface: " << eglGetError() << std::endl;
                wl_egl_window_destroy(egl_window_);
                egl_window_ = nullptr;
                prefer_egl_ = false;
            }
        }
    }
    
    if (!prefer_egl_) {
        if (!create_shm_buffer()) {
            std::cerr << "Failed to create SHM buffer for window" << std::endl;
            return false;
        }
        
        // Clear the buffer to a solid color so we have visible content
        if (shm_data_) {
            uint32_t* pixel_data = static_cast<uint32_t*>(shm_data_);
            uint32_t color = 0xFF202020; // Dark gray background
            for (int i = 0; i < width_ * height_; i++) {
                pixel_data[i] = color;
            }
        }
    } else {
        // Even when EGL is preferred, create SHM buffer as fallback for CPU rendering
        if (!create_shm_buffer()) {
            std::cerr << "WARNING: Failed to create SHM buffer fallback" << std::endl;
            // Don't fail here, continue with EGL-only mode
        }
    }
    
    // Commit the initial surface configuration (no buffer yet)
    wl_surface_commit(surface_);
    
    // Wait for initial configure events
    wl_display_roundtrip(display_);
    wl_display_roundtrip(display_);  // Second roundtrip to ensure all events are processed
    
    std::cout << "DEBUG: Window initialized with size: " << width_ << "x" << height_ << std::endl;
    
    return true;
}

bool WaylandDisplay::init_background_mode() {
    if (!find_output_by_name()) {
        std::cerr << "Output '" << output_name_ << "' not found" << std::endl;
        return false;
    }
    
    surface_ = wl_compositor_create_surface(compositor_);
    if (!surface_) {
        std::cerr << "Failed to create Wayland surface" << std::endl;
        return false;
    }
    
    if (use_layer_shell_ && layer_shell_) {
        setup_layer_surface();
        wl_display_roundtrip(display_);
        
        if (output_width_ == 0 || output_height_ == 0) {
            std::cerr << "Failed to get proper output dimensions" << std::endl;
            return false;
        }
        
        width_ = output_width_;
        height_ = output_height_;
    } else {
        std::cerr << "Layer shell not available, background mode not supported" << std::endl;
        return false;
    }

    if (!create_shm_buffer()) {
        std::cerr << "Failed to create SHM buffer for background" << std::endl;
        return false;
    }
    
    return true;
}

void WaylandDisplay::setup_layer_surface() {
    const char* app_id = "linux-wallpaperengine-ext";
    layer_surface_ = zwlr_layer_shell_get_layer_surface_wrapper(layer_shell_, surface_, output_,
                                                              ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND,
                                                              app_id);
    if (!layer_surface_) {
        std::cerr << "Failed to create layer surface" << std::endl;
        return;
    }
    
    zwlr_layer_surface_v1_add_listener(layer_surface_, &layer_surface_listener, this);
    
    // Configure the layer surface
    zwlr_layer_surface_v1_set_size(layer_surface_, 0, 0);
    zwlr_layer_surface_v1_set_anchor(layer_surface_,
                                     ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP |
                                     ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT |
                                     ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM |
                                     ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT);
    zwlr_layer_surface_v1_set_exclusive_zone(layer_surface_, -1);
    zwlr_layer_surface_v1_set_keyboard_interactivity(layer_surface_, 
                                                     ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE);
    
    // Disable input
    wl_region* region = wl_compositor_create_region(compositor_);
    wl_surface_set_input_region(surface_, region);
    wl_region_destroy(region);
    
    wl_surface_commit(surface_);
}

bool WaylandDisplay::create_shm_buffer() {
    if (!shm_) {
        std::cerr << "Wayland SHM not available" << std::endl;
        return false;
    }
    
    int stride = width_ * 4;
    shm_size_ = stride * height_;
    
    shm_fd_ = memfd_create("wayland-shm", MFD_CLOEXEC);
    if (shm_fd_ < 0) {
        std::cerr << "Failed to create shared memory file" << std::endl;
        return false;
    }
    
    if (ftruncate(shm_fd_, shm_size_) < 0) {
        std::cerr << "Failed to truncate shared memory file" << std::endl;
        close(shm_fd_);
        shm_fd_ = -1;
        return false;
    }
    
    shm_data_ = mmap(nullptr, shm_size_, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd_, 0);
    if (shm_data_ == MAP_FAILED) {
        std::cerr << "Failed to map shared memory" << std::endl;
        close(shm_fd_);
        shm_fd_ = -1;
        return false;
    }
    
    shm_pool_ = wl_shm_create_pool(shm_, shm_fd_, shm_size_);
    if (!shm_pool_) {
        std::cerr << "Failed to create SHM pool" << std::endl;
        munmap(shm_data_, shm_size_);
        close(shm_fd_);
        shm_fd_ = -1;
        return false;
    }
    
    buffer_ = wl_shm_pool_create_buffer(shm_pool_, 0, width_, height_, stride, WL_SHM_FORMAT_ARGB8888);
    if (!buffer_) {
        std::cerr << "Failed to create SHM buffer" << std::endl;
        return false;
    }
    
    return true;
}

bool WaylandDisplay::find_output_by_name() {
    return output_ != nullptr;
}

void WaylandDisplay::cleanup() {
    cleanup_egl();
    cleanup_shm();
    
    if (frame_callback_) {
        wl_callback_destroy(frame_callback_);
        frame_callback_ = nullptr;
    }
    
    if (layer_surface_) {
        zwlr_layer_surface_v1_destroy(layer_surface_);
        layer_surface_ = nullptr;
    }
    
    if (xdg_toplevel_) {
        xdg_toplevel_destroy(xdg_toplevel_);
        xdg_toplevel_ = nullptr;
    }
    
    if (xdg_surface_) {
        xdg_surface_destroy(xdg_surface_);
        xdg_surface_ = nullptr;
    }
    
    if (surface_) {
        wl_surface_destroy(surface_);
        surface_ = nullptr;
    }
    
    if (registry_) {
        wl_registry_destroy(registry_);
        registry_ = nullptr;
    }
    
    if (display_) {
        wl_display_disconnect(display_);
        display_ = nullptr;
    }
}

void WaylandDisplay::cleanup_egl() {
    if (egl_surface_ != EGL_NO_SURFACE) {
        eglDestroySurface(egl_display_, egl_surface_);
        egl_surface_ = EGL_NO_SURFACE;
    }
    
    if (egl_window_) {
        wl_egl_window_destroy(egl_window_);
        egl_window_ = nullptr;
    }
    
    if (egl_context_ != EGL_NO_CONTEXT) {
        eglDestroyContext(egl_display_, egl_context_);
        egl_context_ = EGL_NO_CONTEXT;
    }
    
    if (egl_display_ != EGL_NO_DISPLAY) {
        eglTerminate(egl_display_);
        egl_display_ = EGL_NO_DISPLAY;
    }
    
    egl_initialized_ = false;
}

void WaylandDisplay::cleanup_shm() {
    if (buffer_) {
        wl_buffer_destroy(buffer_);
        buffer_ = nullptr;
    }
    
    if (shm_pool_) {
        wl_shm_pool_destroy(shm_pool_);
        shm_pool_ = nullptr;
    }
    
    if (shm_data_) {
        munmap(shm_data_, shm_size_);
        shm_data_ = nullptr;
    }
    
    if (shm_fd_ >= 0) {
        close(shm_fd_);
        shm_fd_ = -1;
    }
    
    shm_size_ = 0;
}

bool WaylandDisplay::make_egl_current() {
    if (!egl_initialized_ || egl_display_ == EGL_NO_DISPLAY || 
        egl_context_ == EGL_NO_CONTEXT || egl_surface_ == EGL_NO_SURFACE) {
        return false;
    }
    
    return eglMakeCurrent(egl_display_, egl_surface_, egl_surface_, egl_context_);
}

bool WaylandDisplay::set_background(const std::string& media_path, ScalingMode scaling) {
    current_scaling_ = scaling;
    return true;
}

void WaylandDisplay::update() {
    if (display_) {
        wl_display_dispatch_pending(display_);
        wl_display_flush(display_);
    }
}

std::string WaylandDisplay::get_name() const {
    return output_name_;
}

// Clean rendering functions that delegate to specialized renderers
bool WaylandDisplay::render_image_data(const unsigned char* image_data, int img_width, int img_height, ScalingMode scaling) {
    if (!image_data) {
        std::cerr << "ERROR: No image data provided" << std::endl;
        return false;
    }
    
    
    current_scaling_ = scaling;
    bool result = false;
    
    // Initialize image renderer if not already done
    if (!image_renderer_->initialize()) {
        std::cerr << "ERROR: Failed to initialize image renderer" << std::endl;
        return false;
    }

    // For images, prefer CPU-based SHM rendering (reliable and fast for static images)
    if (shm_data_) {
        result = image_renderer_->render_image_shm(image_data, img_width, img_height,
                                                  shm_data_, width_, height_, scaling, windowed_mode_);
        
        if (result && surface_) {
            wl_surface_attach(surface_, buffer_, 0, 0);
            wl_surface_damage(surface_, 0, 0, width_, height_);
            wl_surface_commit(surface_);
        }
    }
    
    if (!result) {
        std::cerr << "ERROR: All image rendering methods failed" << std::endl;
    }
    
    return result;
}

bool WaylandDisplay::render_video_frame(const unsigned char* frame_data, int frame_width, int frame_height, ScalingMode scaling) {
    if (!frame_data) {
        std::cerr << "ERROR: No video frame data provided" << std::endl;
        return false;
    }
    
    // Check for scaling mode changes (only log when actually changing)
    if (current_scaling_ != scaling) {
        std::cout << "INFO: Scaling mode changed from " << static_cast<int>(current_scaling_) << " to " << static_cast<int>(scaling) << " (0=STRETCH, 1=FIT, 2=FILL, 3=DEFAULT)" << std::endl;
    }
    
    // Initialize video renderer if not already done
    if (!video_renderer_->initialize(display_, shm_)) {
        std::cerr << "ERROR: Failed to initialize video renderer" << std::endl;
        return false;
    }
    
    current_scaling_ = scaling;
    bool result = false;
    
    // Use CPU-based SHM rendering (reliable and always works)
    if (shm_data_) {
        result = video_renderer_->render_frame_data_shm(frame_data, frame_width, frame_height,
                                                       shm_data_, width_, height_, scaling, windowed_mode_);
        
        if (result && surface_) {
            wl_surface_attach(surface_, buffer_, 0, 0);
            wl_surface_damage(surface_, 0, 0, width_, height_);
            wl_surface_commit(surface_);
        }
    }
    
    if (!result) {
        std::cerr << "ERROR: All video rendering methods failed" << std::endl;
    }
    
    return result;
}

bool WaylandDisplay::render_video_enhanced(MediaPlayer* media_player, ScalingMode scaling) {
    if (!surface_ || !media_player) {
        return false;
    }
    
    current_scaling_ = scaling;
    bool result = false;
    
    // Use CPU-based SHM rendering (reliable and always works)
    if (shm_data_) {
        unsigned char* frame_data = nullptr;
        int frame_width, frame_height;
        if (media_player->get_video_frame(&frame_data, &frame_width, &frame_height)) {
            result = video_renderer_->render_frame_data_shm(frame_data, frame_width, frame_height,
                                                           shm_data_, width_, height_, scaling, windowed_mode_);
            
            if (result && surface_) {
                wl_surface_attach(surface_, buffer_, 0, 0);
                wl_surface_damage(surface_, 0, 0, width_, height_);
                wl_surface_commit(surface_);
            }
        }
    }
    
    return result;
}

// Static factory methods implementation
std::vector<std::unique_ptr<DisplayOutput>> WaylandDisplay::get_outputs() {
    std::vector<std::unique_ptr<DisplayOutput>> outputs;
    
    auto temp_display = std::make_unique<WaylandDisplay>("temp");
    if (temp_display->init_wayland()) {
        outputs.push_back(std::make_unique<WaylandDisplay>("default"));
    }
    
    return outputs;
}

std::unique_ptr<DisplayOutput> WaylandDisplay::get_output_by_name(const std::string& name) {
    auto display = std::make_unique<WaylandDisplay>(name);
    if (display->initialize()) {
        return std::move(display);
    }
    return nullptr;
}

std::unique_ptr<DisplayOutput> WaylandDisplay::create_window(int x, int y, int width, int height) {
    auto display = std::make_unique<WaylandDisplay>(x, y, width, height);
    if (display->initialize()) {
        return std::move(display);
    }
    return nullptr;
}

// Protocol event handlers
void WaylandDisplay::registry_global(void* data, struct wl_registry* registry, uint32_t name,
                                    const char* interface, uint32_t version) {
    WaylandDisplay* display = static_cast<WaylandDisplay*>(data);
    
    if (strcmp(interface, wl_compositor_interface.name) == 0) {
        display->compositor_ = static_cast<wl_compositor*>(
            wl_registry_bind(registry, name, &wl_compositor_interface, 4));
    } else if (strcmp(interface, wl_shm_interface.name) == 0) {
        display->shm_ = static_cast<wl_shm*>(
            wl_registry_bind(registry, name, &wl_shm_interface, 1));
    } else if (strcmp(interface, xdg_wm_base_interface.name) == 0) {
        display->xdg_wm_base_ = static_cast<xdg_wm_base*>(
            wl_registry_bind(registry, name, &xdg_wm_base_interface, 1));
        xdg_wm_base_add_listener(display->xdg_wm_base_, &xdg_wm_base_listener, display);
    } else if (strcmp(interface, zwlr_layer_shell_v1_interface.name) == 0) {
        display->layer_shell_ = static_cast<zwlr_layer_shell_v1*>(
            wl_registry_bind(registry, name, &zwlr_layer_shell_v1_interface, 1));
    } else if (strcmp(interface, wl_output_interface.name) == 0) {
        display->output_ = static_cast<wl_output*>(
            wl_registry_bind(registry, name, &wl_output_interface, 4));
        display->output_registry_name_ = name;
        wl_output_add_listener(display->output_, &output_listener, display);
    }
}

void WaylandDisplay::registry_global_remove(void* data, struct wl_registry* registry, uint32_t name) {
    WaylandDisplay* display = static_cast<WaylandDisplay*>(data);
    
    if (name == display->output_registry_name_) {
        display->output_ = nullptr;
        display->output_registry_name_ = 0;
    }
}

void WaylandDisplay::xdg_wm_base_ping(void* data, struct xdg_wm_base* xdg_wm_base, uint32_t serial) {
    xdg_wm_base_pong(xdg_wm_base, serial);
}

void WaylandDisplay::xdg_surface_configure(void* data, struct xdg_surface* xdg_surface, uint32_t serial) {
    WaylandDisplay* display = static_cast<WaylandDisplay*>(data);
    
    xdg_surface_ack_configure(xdg_surface, serial);
    
    // After acknowledging configure, make sure we have content and commit the surface
    if (display->surface_ && display->windowed_mode_) {
        // Ensure we have a buffer attached for windowed mode
        if (!display->prefer_egl_ && display->buffer_) {
            wl_surface_attach(display->surface_, display->buffer_, 0, 0);
        }
        
        // Damage the entire surface
        wl_surface_damage(display->surface_, 0, 0, display->width_, display->height_);
        
        // Commit to make the window visible
        wl_surface_commit(display->surface_);
        wl_display_flush(display->display_);
        
        std::cout << "DEBUG: Surface committed after configure" << std::endl;
    }
}

void WaylandDisplay::xdg_toplevel_configure(void* data, struct xdg_toplevel* xdg_toplevel,
                                           int32_t width, int32_t height, struct wl_array* states) {
    WaylandDisplay* display = static_cast<WaylandDisplay*>(data);
    
    std::cout << "DEBUG: XDG toplevel configure - width: " << width << ", height: " << height << std::endl;
    
    if (width > 0 && height > 0) {
        display->width_ = width;
        display->height_ = height;
        
        if (display->egl_window_) {
            wl_egl_window_resize(display->egl_window_, width, height, 0, 0);
        }
        
        // Recreate SHM buffer if needed for fallback rendering
        if (!display->prefer_egl_ && display->windowed_mode_) {
            display->cleanup_shm();
            display->create_shm_buffer();
        }
    } else {
        // Use the default dimensions if none provided
        if (display->width_ == 0 || display->height_ == 0) {
            display->width_ = 800;
            display->height_ = 600;
        }
        std::cout << "DEBUG: Using default window size: " << display->width_ << "x" << display->height_ << std::endl;
    }
}

void WaylandDisplay::xdg_toplevel_close(void* data, struct xdg_toplevel* xdg_toplevel) {
    // Handle window close request - signal application to exit
    WaylandDisplay* display = static_cast<WaylandDisplay*>(data);
    std::cout << "DEBUG: Wayland window close event - terminating application" << std::endl;
    
    // Signal application exit by setting a global flag
    // Since we can't directly access the Application object here, we'll use exit()
    std::exit(0);
}

void WaylandDisplay::layer_surface_configure(void* data, struct zwlr_layer_surface_v1* layer_surface,
                                            uint32_t serial, uint32_t width, uint32_t height) {
    WaylandDisplay* display = static_cast<WaylandDisplay*>(data);
    
    display->output_width_ = width;
    display->output_height_ = height;
    display->width_ = width;
    display->height_ = height;
    
    zwlr_layer_surface_v1_ack_configure(layer_surface, serial);
}

void WaylandDisplay::layer_surface_closed(void* data, struct zwlr_layer_surface_v1* layer_surface) {
    // Handle layer surface close
}

void WaylandDisplay::output_geometry(void* data, struct wl_output* output, int32_t x, int32_t y,
                                    int32_t physical_width, int32_t physical_height, int32_t subpixel,
                                    const char* make, const char* model, int32_t transform) {
    // Handle output geometry information
}

void WaylandDisplay::output_mode(void* data, struct wl_output* output, uint32_t flags,
                                int32_t width, int32_t height, int32_t refresh) {
    WaylandDisplay* display = static_cast<WaylandDisplay*>(data);
    
    if (flags & WL_OUTPUT_MODE_CURRENT) {
        display->output_width_ = width;
        display->output_height_ = height;
    }
}

void WaylandDisplay::output_done(void* data, struct wl_output* output) {
    // Output information is complete
}

void WaylandDisplay::output_scale(void* data, struct wl_output* output, int32_t factor) {
    WaylandDisplay* display = static_cast<WaylandDisplay*>(data);
    display->scale_factor_ = factor;
}

void WaylandDisplay::output_name(void* data, struct wl_output* output, const char* name) {
    // Handle output name
}

void WaylandDisplay::output_description(void* data, struct wl_output* output, const char* description) {
    // Handle output description
}
