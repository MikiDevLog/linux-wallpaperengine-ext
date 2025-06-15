#include "x11_display.h"
#include "x11_image_renderer.h"
#include "x11_video_renderer.h"
#include <iostream>
#include <cstring>
#include <X11/Xatom.h>

X11Display::X11Display(const std::string& output_name) 
    : output_name_(output_name), display_(nullptr), root_window_(0), window_(0), 
      screen_(0), windowed_mode_(false), x_(0), y_(0), width_(800), height_(600),
      image_data_(nullptr), image_size_(0), ximage_(nullptr), pixmap_(0), gc_(0),
      egl_initialized_(false), prefer_egl_(true), egl_display_(EGL_NO_DISPLAY),
      egl_config_(nullptr), egl_context_(EGL_NO_CONTEXT), egl_surface_(EGL_NO_SURFACE),
      current_scaling_(ScalingMode::DEFAULT) {
    image_renderer_ = std::make_unique<X11ImageRenderer>();
    video_renderer_ = std::make_unique<X11VideoRenderer>();
}

X11Display::X11Display(int x, int y, int width, int height)
    : output_name_("window"), display_(nullptr), root_window_(0), window_(0),
      screen_(0), windowed_mode_(true), x_(x), y_(y), width_(width), height_(height),
      image_data_(nullptr), image_size_(0), ximage_(nullptr), pixmap_(0), gc_(0),
      egl_initialized_(false), prefer_egl_(true), egl_display_(EGL_NO_DISPLAY),
      egl_config_(nullptr), egl_context_(EGL_NO_CONTEXT), egl_surface_(EGL_NO_SURFACE),
      current_scaling_(ScalingMode::DEFAULT) {
    image_renderer_ = std::make_unique<X11ImageRenderer>();
    video_renderer_ = std::make_unique<X11VideoRenderer>();
}

X11Display::~X11Display() {
    cleanup();
}

bool X11Display::initialize() {
    if (!init_x11()) {
        return false;
    }
    
    if (windowed_mode_) {
        if (!init_window_mode()) {
            return false;
        }
    } else {
        if (!init_background_mode()) {
            return false;
        }
    }
    
    // Try to initialize EGL for GPU acceleration
    if (prefer_egl_) {
        if (initialize_egl()) {
            std::cout << "DEBUG: X11Display initialized with EGL support" << std::endl;
        } else {
            std::cout << "DEBUG: X11Display falling back to CPU rendering" << std::endl;
            prefer_egl_ = false;
        }
    }
    
    // Initialize renderers
    Window target_window = windowed_mode_ ? window_ : root_window_;
    
    if (egl_initialized_) {
        // Initialize with EGL support
        if (!image_renderer_->initialize_egl(display_, target_window, screen_, 
                                           egl_display_, egl_config_, egl_context_)) {
            std::cerr << "ERROR: Failed to initialize X11 image renderer with EGL" << std::endl;
            return false;
        }
        if (!video_renderer_->initialize(display_, target_window, screen_)) {
            std::cerr << "ERROR: Failed to initialize X11 video renderer" << std::endl;
            return false;
        }
    } else {
        // Initialize with X11 only
        if (!image_renderer_->initialize(display_, target_window, screen_)) {
            std::cerr << "ERROR: Failed to initialize X11 image renderer" << std::endl;
            return false;
        }
        if (!video_renderer_->initialize(display_, target_window, screen_)) {
            std::cerr << "ERROR: Failed to initialize X11 video renderer" << std::endl;
            return false;
        }
    }
    
    return true;
}

bool X11Display::init_x11() {
    display_ = XOpenDisplay(nullptr);
    if (!display_) {
        std::cerr << "Failed to open X11 display" << std::endl;
        return false;
    }
    
    screen_ = DefaultScreen(display_);
    root_window_ = RootWindow(display_, screen_);
    
    return true;
}

bool X11Display::init_window_mode() {
    // Create a window for windowed mode
    window_ = XCreateSimpleWindow(display_, root_window_, x_, y_, width_, height_, 
                                  1, BlackPixel(display_, screen_), WhitePixel(display_, screen_));
    
    if (!window_) {
        std::cerr << "Failed to create X11 window" << std::endl;
        return false;
    }
    
    // Set window properties
    XStoreName(display_, window_, "Linux Wallpaper Engine Ext");
    XSelectInput(display_, window_, ExposureMask | KeyPressMask);
    XMapWindow(display_, window_);
    XFlush(display_);
    
    return true;
}

bool X11Display::init_background_mode() {
    // For background mode, we'll work with the root window
    // We need to find the specific monitor if output_name_ is specified
    
    int num_monitors;
    XRRMonitorInfo* monitors = XRRGetMonitors(display_, root_window_, True, &num_monitors);
    
    if (!monitors) {
        std::cerr << "Failed to get monitor information" << std::endl;
        return false;
    }
    
    bool found_monitor = false;
    if (output_name_ != "default") {
        for (int i = 0; i < num_monitors; i++) {
            char* name = XGetAtomName(display_, monitors[i].name);
            if (name && std::strcmp(name, output_name_.c_str()) == 0) {
                found_monitor = true;
                // Store monitor geometry for later use
                x_ = monitors[i].x;
                y_ = monitors[i].y;
                width_ = monitors[i].width;
                height_ = monitors[i].height;
                XFree(name);
                break;
            }
            if (name) XFree(name);
        }
        
        if (!found_monitor) {
            std::cerr << "Monitor " << output_name_ << " not found" << std::endl;
            XRRFreeMonitors(monitors);
            return false;
        }
    } else {
        // Use the primary monitor
        x_ = monitors[0].x;
        y_ = monitors[0].y;
        width_ = monitors[0].width;
        height_ = monitors[0].height;
    }
    
    XRRFreeMonitors(monitors);
    
    // Initialize image buffer for background mode
    if (!init_image_buffer()) {
        std::cerr << "Failed to initialize image buffer for background mode" << std::endl;
        return false;
    }
    
    return true;
}

void X11Display::cleanup() {
    // Cleanup renderers
    if (image_renderer_) {
        image_renderer_->cleanup();
    }
    if (video_renderer_) {
        video_renderer_->cleanup();
    }
    
    // Cleanup image buffer
    cleanup_image_buffer();
    
    // Cleanup EGL
    cleanup_egl();
    
    // Cleanup X11
    if (window_) {
        XDestroyWindow(display_, window_);
        window_ = 0;
    }
    
    if (display_) {
        XCloseDisplay(display_);
        display_ = nullptr;
    }
}

bool X11Display::set_background(const std::string& media_path, ScalingMode scaling) {
    if (windowed_mode_) {
        set_window_background(media_path);
        std::cout << "Set X11 window background with media: " << media_path << std::endl;
    } else {
        // For background mode, we'll create a pixmap and set it as the root window background
        // This is a simplified implementation - in a real implementation, 
        // you'd use MPV to render to a pixmap or use XVideo extension
        std::cout << "Setting background for X11 output " << output_name_ 
                  << " with media: " << media_path << std::endl;
    }
    return true;
}

void X11Display::set_window_background(const std::string& media_path) {
    // This is where you'd integrate with MPV to render in the window
    // For now, just change the window background color as a placeholder
    XSetWindowBackground(display_, window_, BlackPixel(display_, screen_));
    XClearWindow(display_, window_);
    XFlush(display_);
    
    std::cout << "Set window background with media: " << media_path << std::endl;
}

void X11Display::update() {
    if (windowed_mode_ && display_) {
        // Handle X11 events for windowed mode
        XEvent event;
        while (XPending(display_)) {
            XNextEvent(display_, &event);
            // Handle events as needed
        }
    }
}

std::string X11Display::get_name() const {
    return output_name_;
}

std::vector<std::unique_ptr<DisplayOutput>> X11Display::get_outputs() {
    std::vector<std::unique_ptr<DisplayOutput>> outputs;
    
    Display* display = XOpenDisplay(nullptr);
    if (!display) {
        return outputs;
    }
    
    Window root = RootWindow(display, DefaultScreen(display));
    int num_monitors;
    XRRMonitorInfo* monitors = XRRGetMonitors(display, root, True, &num_monitors);
    
    if (monitors) {
        for (int i = 0; i < num_monitors; i++) {
            char* name = XGetAtomName(display, monitors[i].name);
            if (name) {
                outputs.push_back(std::make_unique<X11Display>(std::string(name)));
                XFree(name);
            }
        }
        XRRFreeMonitors(monitors);
    }
    
    XCloseDisplay(display);
    return outputs;
}

std::unique_ptr<DisplayOutput> X11Display::get_output_by_name(const std::string& name) {
    Display* display = XOpenDisplay(nullptr);
    if (!display) {
        return nullptr;
    }
    
    Window root = RootWindow(display, DefaultScreen(display));
    int num_monitors;
    XRRMonitorInfo* monitors = XRRGetMonitors(display, root, True, &num_monitors);
    
    std::unique_ptr<DisplayOutput> result = nullptr;
    
    if (monitors) {
        for (int i = 0; i < num_monitors; i++) {
            char* monitor_name = XGetAtomName(display, monitors[i].name);
            if (monitor_name && std::strcmp(monitor_name, name.c_str()) == 0) {
                result = std::make_unique<X11Display>(name);
                XFree(monitor_name);
                break;
            }
            if (monitor_name) XFree(monitor_name);
        }
        XRRFreeMonitors(monitors);
    }
    
    XCloseDisplay(display);
    return result;
}

std::unique_ptr<DisplayOutput> X11Display::create_window(int x, int y, int width, int height) {
    return std::make_unique<X11Display>(x, y, width, height);
}

bool X11Display::render_image_data(const unsigned char* image_data, int img_width, int img_height, ScalingMode scaling) {
    if (!image_data) {
        std::cerr << "ERROR: No image data available" << std::endl;
        return false;
    }
    
    current_scaling_ = scaling;
    
    // For windowed mode, use the specialized renderers
    if (windowed_mode_) {
        if (!image_renderer_) {
            std::cerr << "ERROR: No image renderer available for windowed mode" << std::endl;
            return false;
        }
        
        // For images, prefer CPU-based X11 rendering (reliable and fast)
        std::cout << "DEBUG: Using X11 image rendering" << std::endl;
        return image_renderer_->render_image_x11(image_data, img_width, img_height,
                                                width_, height_, scaling, windowed_mode_);
    }
    
    // For background mode, render directly to the image buffer
    std::cout << "DEBUG: Using X11 background image rendering" << std::endl;
    if (!image_data_ || !ximage_) {
        std::cerr << "ERROR: Image buffer not initialized for background mode" << std::endl;
        return false;
    }
    
    return render_to_image_buffer(image_data, img_width, img_height, scaling);
}

bool X11Display::render_video_frame(const unsigned char* frame_data, int frame_width, int frame_height, ScalingMode scaling) {
    if (!frame_data) {
        std::cerr << "ERROR: No video frame data available" << std::endl;
        return false;
    }
    
    
    current_scaling_ = scaling;
    
    // For windowed mode, use the video renderer directly
    if (windowed_mode_ && video_renderer_) {
        // CPU-based rendering (always reliable)
        std::cout << "DEBUG: Using CPU video rendering for window" << std::endl;
        return video_renderer_->render_rgb_frame_x11(frame_data, frame_width, frame_height,
                                                     width_, height_, scaling, windowed_mode_);
    }
    
    // For background mode, use the same approach as images - render to internal buffer
    std::cout << "DEBUG: Using X11 background video rendering" << std::endl;
    return render_image_data(frame_data, frame_width, frame_height, scaling);
}

// EGL context management methods
bool X11Display::initialize_egl() {
    if (egl_initialized_) {
        return true;
    }
    
    // Get EGL display
    egl_display_ = eglGetDisplay((EGLNativeDisplayType)display_);
    if (egl_display_ == EGL_NO_DISPLAY) {
        std::cerr << "DEBUG: Failed to get EGL display" << std::endl;
        return false;
    }
    
    // Initialize EGL
    EGLint major, minor;
    if (!eglInitialize(egl_display_, &major, &minor)) {
        std::cerr << "DEBUG: Failed to initialize EGL" << std::endl;
        return false;
    }
    
    std::cout << "DEBUG: EGL version: " << major << "." << minor << std::endl;
    
    // Choose EGL config
    if (!choose_egl_config()) {
        std::cerr << "DEBUG: Failed to choose EGL config" << std::endl;
        cleanup_egl();
        return false;
    }
    
    // Create EGL context
    if (!create_egl_context()) {
        std::cerr << "DEBUG: Failed to create EGL context" << std::endl;
        cleanup_egl();
        return false;
    }
    
    // Create EGL surface (only for windowed mode)
    if (windowed_mode_) {
        if (!create_egl_surface()) {
            std::cerr << "DEBUG: Failed to create EGL surface" << std::endl;
            cleanup_egl();
            return false;
        }
    }
    
    egl_initialized_ = true;
    std::cout << "DEBUG: EGL initialization complete" << std::endl;
    return true;
}

bool X11Display::make_egl_current() {
    if (!egl_initialized_) {
        return false;
    }
    
    if (windowed_mode_ && egl_surface_ != EGL_NO_SURFACE) {
        return eglMakeCurrent(egl_display_, egl_surface_, egl_surface_, egl_context_);
    } else {
        // For background mode, we don't have a surface
        return eglMakeCurrent(egl_display_, EGL_NO_SURFACE, EGL_NO_SURFACE, egl_context_);
    }
}

void X11Display::cleanup_egl() {
    if (egl_surface_ != EGL_NO_SURFACE) {
        eglDestroySurface(egl_display_, egl_surface_);
        egl_surface_ = EGL_NO_SURFACE;
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

bool X11Display::create_egl_context() {
    // Bind OpenGL ES API (or OpenGL API)
    if (!eglBindAPI(EGL_OPENGL_API)) {
        std::cerr << "DEBUG: Failed to bind OpenGL API" << std::endl;
        return false;
    }
    
    // Context attributes for OpenGL
    EGLint context_attribs[] = {
        EGL_CONTEXT_CLIENT_VERSION, 2,
        EGL_NONE
    };
    
    egl_context_ = eglCreateContext(egl_display_, egl_config_, EGL_NO_CONTEXT, context_attribs);
    if (egl_context_ == EGL_NO_CONTEXT) {
        EGLint error = eglGetError();
        std::cerr << "DEBUG: Failed to create EGL context: " << error << std::endl;
        return false;
    }
    
    return true;
}

bool X11Display::choose_egl_config() {
    // Configuration attributes
    EGLint config_attribs[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_BLUE_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_RED_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_DEPTH_SIZE, 16,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
        EGL_NONE
    };
    
    EGLint num_configs;
    if (!eglChooseConfig(egl_display_, config_attribs, &egl_config_, 1, &num_configs) || num_configs == 0) {
        std::cerr << "DEBUG: Failed to choose EGL config" << std::endl;
        return false;
    }
    
    return true;
}

bool X11Display::create_egl_surface() {
    if (!windowed_mode_ || !window_) {
        std::cerr << "DEBUG: Cannot create EGL surface without window" << std::endl;
        return false;
    }
    
    egl_surface_ = eglCreateWindowSurface(egl_display_, egl_config_, (EGLNativeWindowType)window_, nullptr);
    if (egl_surface_ == EGL_NO_SURFACE) {
        EGLint error = eglGetError();
        std::cerr << "DEBUG: Failed to create EGL surface: " << error << std::endl;
        return false;
    }
    
    return true;
}

bool X11Display::init_image_buffer() {
    if (windowed_mode_) {
        return true; // No image buffer needed for windowed mode
    }
    
    // Create pixmap for background rendering (like reference implementation)
    pixmap_ = XCreatePixmap(display_, root_window_, width_, height_, 24);
    if (!pixmap_) {
        std::cerr << "ERROR: Failed to create pixmap for background" << std::endl;
        return false;
    }
    
    // Create graphics context for pixmap
    gc_ = XCreateGC(display_, pixmap_, 0, nullptr);
    if (!gc_) {
        std::cerr << "ERROR: Failed to create graphics context for pixmap" << std::endl;
        XFreePixmap(display_, pixmap_);
        pixmap_ = 0;
        return false;
    }
    
    // Pre-fill pixmap with black
    XSetForeground(display_, gc_, BlackPixel(display_, screen_));
    XFillRectangle(display_, pixmap_, gc_, 0, 0, width_, height_);
    
    // Set the pixmap as window background
    XSetWindowBackgroundPixmap(display_, root_window_, pixmap_);
    
    // Allocate space for image buffer (CPU buffer for rendering)
    image_size_ = width_ * height_ * 4; // RGBA
    image_data_ = new char[image_size_];
    memset(image_data_, 0, image_size_);
    
    // Create XImage for copying data to pixmap
    ximage_ = XCreateImage(display_, CopyFromParent, 24, ZPixmap, 0, image_data_, 
                          width_, height_, 32, 0);
    if (!ximage_) {
        std::cerr << "ERROR: Failed to create XImage for background buffer" << std::endl;
        cleanup_image_buffer();
        return false;
    }
    
    std::cout << "DEBUG: X11 image buffer initialized (" << width_ << "x" << height_ << ")" << std::endl;
    return true;
}

void X11Display::cleanup_image_buffer() {
    if (ximage_) {
        XDestroyImage(ximage_); // This also frees image_data_
        ximage_ = nullptr;
        image_data_ = nullptr; // Don't delete separately, XDestroyImage handles it
    } else if (image_data_) {
        delete[] image_data_;
        image_data_ = nullptr;
    }
    
    if (gc_) {
        XFreeGC(display_, gc_);
        gc_ = 0;
    }
    
    if (pixmap_) {
        XFreePixmap(display_, pixmap_);
        pixmap_ = 0;
    }
}

void X11Display::update_background_from_buffer() {
    if (!image_data_ || !pixmap_ || !gc_ || !ximage_) {
        return;
    }
    
    // Copy image buffer to pixmap
    XPutImage(display_, pixmap_, gc_, ximage_, 0, 0, 0, 0, width_, height_);
    
    // Update root window properties for compositor compatibility (like reference)
    Atom prop_root = XInternAtom(display_, "_XROOTPMAP_ID", False);
    Atom prop_esetroot = XInternAtom(display_, "ESETROOT_PMAP_ID", False);
    XChangeProperty(display_, root_window_, prop_root, XA_PIXMAP, 32, PropModeReplace,
                   (unsigned char*)&pixmap_, 1);
    XChangeProperty(display_, root_window_, prop_esetroot, XA_PIXMAP, 32, PropModeReplace,
                   (unsigned char*)&pixmap_, 1);
    
    // Clear and refresh the root window
    XClearWindow(display_, root_window_);
    XFlush(display_);
}

bool X11Display::render_to_image_buffer(const unsigned char* image_data, int img_width, int img_height, ScalingMode scaling) {
    if (!image_data_ || !image_data) {
        return false;
    }
    
    // Calculate scaled dimensions based on scaling mode
    int dest_width = width_;
    int dest_height = height_;
    int dest_x = 0;
    int dest_y = 0;
    
    switch (scaling) {
        case ScalingMode::STRETCH:
            // Use full buffer size (already set)
            break;
            
        case ScalingMode::FIT: {
            // Scale to fit within buffer, maintaining aspect ratio
            double img_aspect = (double)img_width / img_height;
            double buffer_aspect = (double)width_ / height_;
            
            if (img_aspect > buffer_aspect) {
                // Image is wider than buffer
                dest_width = width_;
                dest_height = (int)(width_ / img_aspect);
                dest_y = (height_ - dest_height) / 2;
            } else {
                // Image is taller than buffer
                dest_height = height_;
                dest_width = (int)(height_ * img_aspect);
                dest_x = (width_ - dest_width) / 2;
            }
            break;
        }
        
        case ScalingMode::FILL: {
            // Scale to fill buffer, maintaining aspect ratio (may crop)
            double img_aspect = (double)img_width / img_height;
            double buffer_aspect = (double)width_ / height_;
            
            if (img_aspect > buffer_aspect) {
                // Image is wider, scale to height
                dest_height = height_;
                dest_width = (int)(height_ * img_aspect);
                dest_x = (width_ - dest_width) / 2;
            } else {
                // Image is taller, scale to width
                dest_width = width_;
                dest_height = (int)(width_ / img_aspect);
                dest_y = (height_ - dest_height) / 2;
            }
            break;
        }
        
        case ScalingMode::DEFAULT:
            // Use original image size, centered
            dest_width = std::min(img_width, width_);
            dest_height = std::min(img_height, height_);
            dest_x = (width_ - dest_width) / 2;
            dest_y = (height_ - dest_height) / 2;
            break;
    }
    
    // Clear the buffer first (black background)
    memset(image_data_, 0, image_size_);
    
    // Copy and scale image data to buffer with conditional Y-axis flip
    // ============================================================================
    // CONDITIONAL Y-AXIS ORIENTATION FIX FOR X11 IMAGE BUFFER COPYING
    // This function is only used for background mode, so windowed_mode is always false.
    // Therefore, NO Y-axis flip should be applied for background mode.
    // ============================================================================
    
    
    for (int y = 0; y < dest_height; y++) {
        for (int x = 0; x < dest_width; x++) {
            // Calculate source pixel position
            int src_x = (x * img_width) / dest_width;
            // NO Y-axis flip for background mode - read from top of source image
            int src_y = (y * img_height) / dest_height;
            
            if (src_x >= img_width) src_x = img_width - 1;
            if (src_y < 0) src_y = 0;
            if (src_y >= img_height) src_y = img_height - 1;
            
            // Calculate buffer position (offset by dest_x, dest_y)
            int buf_x = x + dest_x;
            int buf_y = y + dest_y;
            
            // ============================================================================
            // CRITICAL BOUNDS CHECKING - PREVENTS SEGMENTATION FAULT - DO NOT MODIFY
            // 
            // This bounds check prevents segmentation faults when FILL mode generates
            // negative buffer coordinates during cropping operations. This fix was
            // added to prevent crashes and must remain in place.
            // ============================================================================
            // CRITICAL FIX: Check for negative values to prevent segmentation fault in FILL mode
            if (buf_x < 0 || buf_y < 0 || buf_x >= width_ || buf_y >= height_) continue;
            
            int src_idx = (src_y * img_width + src_x) * 4; // RGBA input
            int buf_idx = (buf_y * width_ + buf_x) * 4;   // RGBA buffer
            
            // Copy pixel data (convert from RGBA to BGRA for X11)
            image_data_[buf_idx + 0] = image_data[src_idx + 2]; // B
            image_data_[buf_idx + 1] = image_data[src_idx + 1]; // G
            image_data_[buf_idx + 2] = image_data[src_idx + 0]; // R
            image_data_[buf_idx + 3] = image_data[src_idx + 3]; // A
        }
    }
    
    // ============================================================================
    // END CONDITIONAL Y-AXIS ORIENTATION FIX FOR X11 IMAGE BUFFER COPYING
    // ============================================================================
    
    // Update the background from buffer
    update_background_from_buffer();
    
    std::cout << "DEBUG: Rendered image (" << img_width << "x" << img_height << ") to background buffer (" 
              << dest_width << "x" << dest_height << ") at (" << dest_x << "," << dest_y << ")" << std::endl;
    
    return true;
}
