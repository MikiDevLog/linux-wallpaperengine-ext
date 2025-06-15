#include "x11_image_renderer.h"
#include <iostream>
#include <cstring>
#include <cmath>
#include <algorithm>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

X11ImageRenderer::X11ImageRenderer()
    : initialized_(false), egl_mode_(false), x11_display_(nullptr), window_(0), screen_(0),
      graphics_context_(0), egl_display_(EGL_NO_DISPLAY), egl_config_(nullptr),
      egl_context_(EGL_NO_CONTEXT), egl_surface_(EGL_NO_SURFACE), glew_initialized_(false) {}

X11ImageRenderer::~X11ImageRenderer() {
    cleanup();
}

bool X11ImageRenderer::initialize(Display* x11_display, Window window, int screen) {
    if (!x11_display || !window) {
        std::cerr << "ERROR: Invalid X11 display or window" << std::endl;
        return false;
    }
    
    x11_display_ = x11_display;
    window_ = window;
    screen_ = screen;
    egl_mode_ = false;
    
    // Create graphics context
    graphics_context_ = XCreateGC(x11_display_, window_, 0, nullptr);
    if (!graphics_context_) {
        std::cerr << "ERROR: Failed to create X11 graphics context" << std::endl;
        return false;
    }
    
    initialized_ = true;
    std::cout << "DEBUG: X11ImageRenderer initialized in X11 mode" << std::endl;
    return true;
}

bool X11ImageRenderer::initialize_egl(Display* x11_display, Window window, int screen,
                                     EGLDisplay egl_display, EGLConfig egl_config, EGLContext egl_context) {
    if (!initialize(x11_display, window, screen)) {
        return false;
    }
    
    egl_display_ = egl_display;
    egl_config_ = egl_config;
    egl_context_ = egl_context;
    egl_mode_ = true;
    
    std::cout << "DEBUG: X11ImageRenderer initialized in EGL mode" << std::endl;
    return true;
}

void X11ImageRenderer::cleanup() {
    if (graphics_context_) {
        XFreeGC(x11_display_, graphics_context_);
        graphics_context_ = 0;
    }
    
    initialized_ = false;
    egl_mode_ = false;
    x11_display_ = nullptr;
    window_ = 0;
    egl_display_ = EGL_NO_DISPLAY;
    egl_config_ = nullptr;
    egl_context_ = EGL_NO_CONTEXT;
    egl_surface_ = EGL_NO_SURFACE;
}

bool X11ImageRenderer::render_image_x11(const unsigned char* image_data, int img_width, int img_height,
                                        int surface_width, int surface_height, ScalingMode scaling,
                                        bool windowed_mode) {
    if (!initialized_ || !image_data) {
        std::cerr << "ERROR: X11 image renderer not initialized or no image data" << std::endl;
        return false;
    }
    
    std::cout << "DEBUG: X11ImageRenderer::render_image_x11 - Start" << std::endl;
    std::cout << "DEBUG: Image dimensions: " << img_width << "x" << img_height << std::endl;
    std::cout << "DEBUG: Surface dimensions: " << surface_width << "x" << surface_height << std::endl;
    std::cout << "DEBUG: *** X11 ORIENTATION DEBUG *** windowed_mode = " << (windowed_mode ? "TRUE (window mode)" : "FALSE (background mode)") << std::endl;
    std::cout << "DEBUG: *** X11 ORIENTATION DEBUG *** Y-axis flip will be " << (windowed_mode ? "ENABLED" : "DISABLED") << " for this render" << std::endl;
    
    // Calculate scaled dimensions based on scaling mode
    int dest_width = surface_width;
    int dest_height = surface_height;
    int dest_x = 0;
    int dest_y = 0;
    
    switch (scaling) {
        case ScalingMode::STRETCH:
            // Use full surface size (already set)
            break;
            
        case ScalingMode::FIT: {
            // Scale to fit within surface, maintaining aspect ratio
            double img_aspect = (double)img_width / img_height;
            double surface_aspect = (double)surface_width / surface_height;
            
            if (img_aspect > surface_aspect) {
                // Image is wider than surface
                dest_width = surface_width;
                dest_height = (int)(surface_width / img_aspect);
                dest_y = (surface_height - dest_height) / 2;
            } else {
                // Image is taller than surface
                dest_height = surface_height;
                dest_width = (int)(surface_height * img_aspect);
                dest_x = (surface_width - dest_width) / 2;
            }
            break;
        }
        
        case ScalingMode::FILL: {
            // Scale to fill surface, maintaining aspect ratio (may crop)
            double img_aspect = (double)img_width / img_height;
            double surface_aspect = (double)surface_width / surface_height;
            
            if (img_aspect > surface_aspect) {
                // Image is wider, scale to height and crop horizontally - use negative offset
                dest_height = surface_height;
                dest_width = (int)(surface_height * img_aspect);
                dest_x = -(dest_width - surface_width) / 2;  // Negative offset for cropping
                dest_y = 0;
            } else {
                // Image is taller, scale to width and crop vertically - use negative offset
                dest_width = surface_width;
                dest_height = (int)(surface_width / img_aspect);
                dest_x = 0;
                dest_y = -(dest_height - surface_height) / 2;  // Negative offset for cropping
            }
            break;
        }
        
        case ScalingMode::DEFAULT:
            // Use original image size, centered
            dest_width = img_width;
            dest_height = img_height;
            dest_x = (surface_width - dest_width) / 2;
            dest_y = (surface_height - dest_height) / 2;
            break;
    }
    
    // Convert image data to X11 format
    unsigned char* x11_data = nullptr;
    int bytes_per_pixel = 0;
    convert_rgba_to_x11_format(image_data, img_width, img_height, &x11_data, &bytes_per_pixel);
    
    if (!x11_data) {
        std::cerr << "ERROR: Failed to convert image data to X11 format" << std::endl;
        return false;
    }
    
    // Scale the image data if needed
    unsigned char* scaled_data = nullptr;
    if (dest_width != img_width || dest_height != img_height) {
        scaled_data = new unsigned char[dest_width * dest_height * bytes_per_pixel];
        apply_scaling_x11(x11_data, img_width, img_height, scaled_data, dest_width, dest_height, scaling, bytes_per_pixel, windowed_mode);
        delete[] x11_data;
        x11_data = scaled_data;
    }
    
    // Create XImage
    Visual* visual = DefaultVisual(x11_display_, screen_);
    int depth = DefaultDepth(x11_display_, screen_);
    
    XImage* ximage = XCreateImage(x11_display_, visual, depth, ZPixmap, 0,
                                 (char*)x11_data, dest_width, dest_height,
                                 bytes_per_pixel * 8, dest_width * bytes_per_pixel);
    
    if (!ximage) {
        delete[] x11_data;
        std::cerr << "ERROR: Failed to create XImage" << std::endl;
        return false;
    }
    
    // Clear the window if using FIT mode to avoid artifacts
    if (scaling == ScalingMode::FIT) {
        XClearWindow(x11_display_, window_);
    }
    
    // Draw the image
    XPutImage(x11_display_, window_, graphics_context_, ximage, 0, 0, dest_x, dest_y, dest_width, dest_height);
    
    // Cleanup
    XDestroyImage(ximage); // This also frees x11_data
    XFlush(x11_display_);
    
    std::cout << "DEBUG: X11ImageRenderer::render_image_x11 - Complete" << std::endl;
    return true;
}

bool X11ImageRenderer::render_image_egl(const unsigned char* image_data, int img_width, int img_height,
                                        EGLSurface egl_surface, int surface_width, int surface_height,
                                        ScalingMode scaling) {
    if (!initialized_ || !egl_mode_ || !image_data) {
        std::cerr << "ERROR: X11 EGL image renderer not initialized or no image data" << std::endl;
        return false;
    }
    
    std::cout << "DEBUG: X11ImageRenderer::render_image_egl - Start" << std::endl;
    
    // Validate EGL surface
    if (egl_surface == EGL_NO_SURFACE) {
        std::cerr << "ERROR: Invalid EGL surface provided to X11 image renderer" << std::endl;
        return false;
    }
    
    // Make sure EGL context is current
    if (!eglMakeCurrent(egl_display_, egl_surface, egl_surface, egl_context_)) {
        EGLint error = eglGetError();
        std::cerr << "ERROR: Failed to make EGL context current in X11 image renderer: " << error << std::endl;
        return false;
    }
    
    // Initialize GLEW if needed
    if (!init_glew_if_needed()) {
        std::cerr << "ERROR: Failed to initialize GLEW in X11 image renderer" << std::endl;
        return false;
    }
    
    // Set viewport
    glViewport(0, 0, surface_width, surface_height);
    
    // Clear the background
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    
    // Check if image needs resizing for GPU compatibility
    unsigned char* resized_data = nullptr;
    int final_width = img_width;
    int final_height = img_height;
    
    // EGL rendering is typically used for window mode, so default windowed_mode to true
    check_and_resize_image(image_data, img_width, img_height, &resized_data, &final_width, &final_height, true);
    const unsigned char* data_to_use = resized_data ? resized_data : image_data;
    
    // Process image data for OpenGL
    unsigned char* processed_data = nullptr;
    process_image_for_opengl(data_to_use, final_width, final_height, &processed_data);
    
    // Create OpenGL texture
    unsigned int texture_id = 0;
    create_opengl_texture(processed_data, final_width, final_height, &texture_id);
    
    if (texture_id == 0) {
        std::cerr << "ERROR: Failed to create OpenGL texture" << std::endl;
        if (processed_data) delete[] processed_data;
        if (resized_data) delete[] resized_data;
        return false;
    }
    
    // Render textured quad with scaling
    render_textured_quad(final_width, final_height, surface_width, surface_height, scaling);
    
    // Clean up
    glDeleteTextures(1, &texture_id);
    if (processed_data) delete[] processed_data;
    if (resized_data) delete[] resized_data;
    
    // Check for OpenGL errors
    GLenum gl_error = glGetError();
    if (gl_error != GL_NO_ERROR) {
        std::cerr << "ERROR: OpenGL error in X11 image renderer: " << gl_error << std::endl;
        return false;
    }
    
    std::cout << "DEBUG: X11ImageRenderer::render_image_egl - Complete" << std::endl;
    return true;
}

bool X11ImageRenderer::load_image_from_file(const std::string& image_path, unsigned char** image_data,
                                           int* width, int* height) {
    // Use FFmpeg to load the image (same as Wayland implementation)
    AVFormatContext* format_ctx = nullptr;
    if (avformat_open_input(&format_ctx, image_path.c_str(), nullptr, nullptr) != 0) {
        std::cerr << "ERROR: Could not open image file: " << image_path << std::endl;
        return false;
    }
    
    if (avformat_find_stream_info(format_ctx, nullptr) < 0) {
        std::cerr << "ERROR: Could not find stream information" << std::endl;
        avformat_close_input(&format_ctx);
        return false;
    }
    
    // Find video stream (images are treated as single-frame videos)
    int video_stream = -1;
    for (unsigned int i = 0; i < format_ctx->nb_streams; i++) {
        if (format_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            video_stream = i;
            break;
        }
    }
    
    if (video_stream == -1) {
        std::cerr << "ERROR: No video stream found in image" << std::endl;
        avformat_close_input(&format_ctx);
        return false;
    }
    
    // Get codec parameters
    AVCodecParameters* codec_params = format_ctx->streams[video_stream]->codecpar;
    const AVCodec* codec = avcodec_find_decoder(codec_params->codec_id);
    if (!codec) {
        std::cerr << "ERROR: Codec not found" << std::endl;
        avformat_close_input(&format_ctx);
        return false;
    }
    
    // Create codec context
    AVCodecContext* codec_ctx = avcodec_alloc_context3(codec);
    if (!codec_ctx) {
        std::cerr << "ERROR: Could not allocate codec context" << std::endl;
        avformat_close_input(&format_ctx);
        return false;
    }
    
    if (avcodec_parameters_to_context(codec_ctx, codec_params) < 0) {
        std::cerr << "ERROR: Could not copy codec parameters to context" << std::endl;
        avcodec_free_context(&codec_ctx);
        avformat_close_input(&format_ctx);
        return false;
    }
    
    if (avcodec_open2(codec_ctx, codec, nullptr) < 0) {
        std::cerr << "ERROR: Could not open codec" << std::endl;
        avcodec_free_context(&codec_ctx);
        avformat_close_input(&format_ctx);
        return false;
    }
    
    // Allocate frames
    AVFrame* frame = av_frame_alloc();
    AVFrame* frame_rgb = av_frame_alloc();
    
    if (!frame || !frame_rgb) {
        std::cerr << "ERROR: Could not allocate frames" << std::endl;
        if (frame) av_frame_free(&frame);
        if (frame_rgb) av_frame_free(&frame_rgb);
        avcodec_free_context(&codec_ctx);
        avformat_close_input(&format_ctx);
        return false;
    }
    
    // Set output dimensions
    *width = codec_ctx->width;
    *height = codec_ctx->height;
    
    // Allocate buffer for RGBA data
    int buffer_size = av_image_get_buffer_size(AV_PIX_FMT_RGBA, *width, *height, 32);
    *image_data = new unsigned char[buffer_size];
    
    av_image_fill_arrays(frame_rgb->data, frame_rgb->linesize, *image_data,
                        AV_PIX_FMT_RGBA, *width, *height, 32);
    
    // Create scaling context
    SwsContext* sws_ctx = sws_getContext(*width, *height, codec_ctx->pix_fmt,
                                        *width, *height, AV_PIX_FMT_RGBA,
                                        SWS_BILINEAR, nullptr, nullptr, nullptr);
    
    if (!sws_ctx) {
        std::cerr << "ERROR: Could not create scaling context" << std::endl;
        delete[] *image_data;
        av_frame_free(&frame);
        av_frame_free(&frame_rgb);
        avcodec_free_context(&codec_ctx);
        avformat_close_input(&format_ctx);
        return false;
    }
    
    // Read and decode frame
    AVPacket packet;
    bool frame_decoded = false;
    
    while (av_read_frame(format_ctx, &packet) >= 0 && !frame_decoded) {
        if (packet.stream_index == video_stream) {
            int ret = avcodec_send_packet(codec_ctx, &packet);
            if (ret < 0) {
                av_packet_unref(&packet);
                continue;
            }
            
            ret = avcodec_receive_frame(codec_ctx, frame);
            if (ret == 0) {
                // Standard RGBA conversion without flipping
                sws_scale(sws_ctx, frame->data, frame->linesize, 0, *height,
                         frame_rgb->data, frame_rgb->linesize);
                frame_decoded = true;
            }
        }
        av_packet_unref(&packet);
    }
    
    // Cleanup
    sws_freeContext(sws_ctx);
    av_frame_free(&frame);
    av_frame_free(&frame_rgb);
    avcodec_free_context(&codec_ctx);
    avformat_close_input(&format_ctx);
    
    if (!frame_decoded) {
        std::cerr << "ERROR: Could not decode image frame" << std::endl;
        delete[] *image_data;
        return false;
    }
    
    std::cout << "DEBUG: Loaded image: " << *width << "x" << *height << " from " << image_path << std::endl;
    return true;
}

void X11ImageRenderer::free_image_data(unsigned char* image_data) {
    delete[] image_data;
}

void X11ImageRenderer::check_and_resize_image(const unsigned char* src_data, int src_width, int src_height,
                                             unsigned char** dst_data, int* dst_width, int* dst_height,
                                             bool windowed_mode) {
    // Check if image is too large for GPU textures
    if (src_width <= MAX_TEXTURE_SIZE && src_height <= MAX_TEXTURE_SIZE) {
        // No resizing needed
        *dst_data = nullptr;
        *dst_width = src_width;
        *dst_height = src_height;
        return;
    }
    
    // Calculate new dimensions while maintaining aspect ratio
    float scale = std::min(static_cast<float>(MAX_TEXTURE_SIZE) / src_width,
                          static_cast<float>(MAX_TEXTURE_SIZE) / src_height);
    
    *dst_width = static_cast<int>(src_width * scale);
    *dst_height = static_cast<int>(src_height * scale);
    
    std::cout << "DEBUG: Resizing image from " << src_width << "x" << src_height 
              << " to " << *dst_width << "x" << *dst_height << std::endl;
    std::cout << "DEBUG: *** X11 RESIZE ORIENTATION DEBUG *** windowed_mode = " << (windowed_mode ? "TRUE (window mode)" : "FALSE (background mode)") << std::endl;
    std::cout << "DEBUG: *** X11 RESIZE ORIENTATION DEBUG *** Y-axis flip will be " << (windowed_mode ? "ENABLED" : "DISABLED") << " for resize operation" << std::endl;
    
    // Allocate new buffer
    *dst_data = new unsigned char[(*dst_width) * (*dst_height) * 4];
    
    // Simple nearest-neighbor scaling with conditional Y-axis flip
    // ============================================================================
    // CONDITIONAL Y-AXIS ORIENTATION FIX FOR X11 IMAGE RESIZING
    // This Y-axis flip is applied conditionally based on windowed_mode:
    // - Window mode (windowed_mode=true): Apply Y-axis flip for correct orientation
    // - Background mode (windowed_mode=false): No Y-axis flip needed
    // ============================================================================
    
    for (int y = 0; y < *dst_height; y++) {
        for (int x = 0; x < *dst_width; x++) {
            int src_x = static_cast<int>(x / scale);
            // CONDITIONAL: Apply Y-axis flip only for window mode
            int src_y;
            if (windowed_mode) {
                // Window mode: Apply Y-axis flip - read from bottom of source image
                src_y = src_height - 1 - static_cast<int>(y / scale);
            } else {
                // Background mode: No Y-axis flip - read from top of source image
                src_y = static_cast<int>(y / scale);
            }
            
            if (src_x >= src_width) src_x = src_width - 1;
            if (src_y < 0) src_y = 0;
            if (src_y >= src_height) src_y = src_height - 1;
            
            int dst_idx = (y * (*dst_width) + x) * 4;
            int src_idx = (src_y * src_width + src_x) * 4;
            
            (*dst_data)[dst_idx + 0] = src_data[src_idx + 0]; // R
            (*dst_data)[dst_idx + 1] = src_data[src_idx + 1]; // G
            (*dst_data)[dst_idx + 2] = src_data[src_idx + 2]; // B
            (*dst_data)[dst_idx + 3] = src_data[src_idx + 3]; // A
        }
    }
    
    // ============================================================================
    // END CONDITIONAL Y-AXIS ORIENTATION FIX FOR X11 IMAGE RESIZING
    // ============================================================================
}

void X11ImageRenderer::apply_scaling_x11(const unsigned char* src_data, int src_width, int src_height,
                                         unsigned char* dst_data, int dst_width, int dst_height,
                                         ScalingMode scaling, int bytes_per_pixel, bool windowed_mode) {
    std::cout << "DEBUG: *** X11 SCALING ORIENTATION DEBUG *** apply_scaling_x11 called" << std::endl;
    std::cout << "DEBUG: *** X11 SCALING ORIENTATION DEBUG *** windowed_mode = " << (windowed_mode ? "TRUE (window mode)" : "FALSE (background mode)") << std::endl;
    std::cout << "DEBUG: *** X11 SCALING ORIENTATION DEBUG *** Y-axis flip will be " << (windowed_mode ? "ENABLED" : "DISABLED") << " for scaling operation" << std::endl;
    
    if (scaling == ScalingMode::FILL) {
        // For FILL mode, we need to crop the content to maintain aspect ratio (similar to X11 video renderer)
        double src_aspect = (double)src_width / src_height;
        double dst_aspect = (double)dst_width / dst_height;
        
        int crop_src_width, crop_src_height, crop_src_x, crop_src_y;
        
        if (src_aspect > dst_aspect) {
            // Source is wider - crop horizontally (crop sides)
            crop_src_height = src_height;
            crop_src_width = (int)(src_height * dst_aspect);
            crop_src_x = (src_width - crop_src_width) / 2;
            crop_src_y = 0;
        } else {
            // Source is taller - crop vertically (crop top/bottom)
            crop_src_width = src_width;
            crop_src_height = (int)(src_width / dst_aspect);
            crop_src_x = 0;
            crop_src_y = (src_height - crop_src_height) / 2;
        }
        
        // Scale the cropped region to destination with conditional Y-axis flip
        // ============================================================================
        // CONDITIONAL Y-AXIS ORIENTATION FIX FOR X11 IMAGE RENDERING (FILL MODE)
        // This Y-axis flip is applied conditionally based on windowed_mode:
        // - Window mode (windowed_mode=true): Apply Y-axis flip for correct orientation
        // - Background mode (windowed_mode=false): No Y-axis flip needed
        // ============================================================================
        
        for (int y = 0; y < dst_height; y++) {
            for (int x = 0; x < dst_width; x++) {
                // Map destination pixel to cropped source region with conditional Y-axis flip
                int src_x = crop_src_x + (x * crop_src_width) / dst_width;
                // CONDITIONAL: Apply Y-axis flip only for window mode
                int crop_y = (y * crop_src_height) / dst_height;
                int src_y;
                if (windowed_mode) {
                    // Window mode: Apply Y-axis flip - read from bottom of cropped region
                    src_y = crop_src_y + (crop_src_height - 1 - crop_y);
                } else {
                    // Background mode: No Y-axis flip - read from top of cropped region
                    src_y = crop_src_y + crop_y;
                }
                
                // Clamp to bounds
                if (src_x >= src_width) src_x = src_width - 1;
                if (src_y >= src_height) src_y = src_height - 1;
                if (src_x < 0) src_x = 0;
                if (src_y < 0) src_y = 0;
                
                int src_idx = (src_y * src_width + src_x) * bytes_per_pixel;
                int dst_idx = (y * dst_width + x) * bytes_per_pixel;
                
                // Copy pixel data
                for (int b = 0; b < bytes_per_pixel; b++) {
                    dst_data[dst_idx + b] = src_data[src_idx + b];
                }
            }
        }
        
        // ============================================================================
        // END CONDITIONAL Y-AXIS ORIENTATION FIX FOR X11 IMAGE RENDERING (FILL MODE)
        // ============================================================================
    } else {
        // Simple nearest-neighbor scaling for other modes with conditional Y-axis flip
        // ============================================================================
        // CONDITIONAL Y-AXIS ORIENTATION FIX FOR X11 IMAGE RENDERING
        // This Y-axis flip is applied conditionally based on windowed_mode:
        // - Window mode (windowed_mode=true): Apply Y-axis flip for correct orientation
        // - Background mode (windowed_mode=false): No Y-axis flip needed
        // ============================================================================
        
        for (int y = 0; y < dst_height; y++) {
            for (int x = 0; x < dst_width; x++) {
                // Calculate source pixel position
                int src_x = (x * src_width) / dst_width;
                // CONDITIONAL: Apply Y-axis flip only for window mode
                int src_y;
                if (windowed_mode) {
                    // Window mode: Apply Y-axis flip - read from bottom of source image
                    src_y = src_height - 1 - ((y * src_height) / dst_height);
                } else {
                    // Background mode: No Y-axis flip - read from top of source image
                    src_y = (y * src_height) / dst_height;
                }
                
                if (src_x >= src_width) src_x = src_width - 1;
                if (src_y < 0) src_y = 0;
                if (src_y >= src_height) src_y = src_height - 1;
                
                int src_idx = (src_y * src_width + src_x) * bytes_per_pixel;
                int dst_idx = (y * dst_width + x) * bytes_per_pixel;
                
                // Copy pixel data based on bytes per pixel
                for (int b = 0; b < bytes_per_pixel; b++) {
                    dst_data[dst_idx + b] = src_data[src_idx + b];
                }
            }
        }
        
        // ============================================================================
        // END CONDITIONAL Y-AXIS ORIENTATION FIX FOR X11 IMAGE RENDERING
        // ============================================================================
    }
}

void X11ImageRenderer::process_image_for_opengl(const unsigned char* src_data, int width, int height,
                                               unsigned char** processed_data) {
    // For OpenGL, we might need to flip the image vertically
    // For now, just copy the data as-is
    *processed_data = new unsigned char[width * height * 4];
    memcpy(*processed_data, src_data, width * height * 4);
}

void X11ImageRenderer::create_opengl_texture(const unsigned char* image_data, int width, int height,
                                            unsigned int* texture_id) {
    glGenTextures(1, texture_id);
    glBindTexture(GL_TEXTURE_2D, *texture_id);
    
    // Upload texture data
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, image_data);
    
    // Set texture parameters
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

void X11ImageRenderer::render_textured_quad(int img_width, int img_height, int surface_width, int surface_height,
                                           ScalingMode scaling) {
    // Enable blending and texturing
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_TEXTURE_2D);
    
    // Calculate scaling based on mode
    float scale_x = 1.0f, scale_y = 1.0f;
    float offset_x = 0.0f, offset_y = 0.0f;
    
    switch (scaling) {
        case ScalingMode::STRETCH:
            // Keep default scale (fills entire surface)
            break;
        case ScalingMode::FIT:
            scale_x = scale_y = std::min(static_cast<float>(surface_width) / img_width,
                                        static_cast<float>(surface_height) / img_height);
            offset_x = (2.0f - 2.0f * scale_x * img_width / surface_width) / 2.0f;
            offset_y = (2.0f - 2.0f * scale_y * img_height / surface_height) / 2.0f;
            break;
        case ScalingMode::FILL:
            scale_x = scale_y = std::max(static_cast<float>(surface_width) / img_width,
                                        static_cast<float>(surface_height) / img_height);
            break;
        case ScalingMode::DEFAULT:
        default:
            scale_x = static_cast<float>(surface_width) / img_width;
            scale_y = static_cast<float>(surface_height) / img_height;
            break;
    }
    
    // Define vertices for a quad with texture coordinates
    float vertices[] = {
        // Positions                                                                          // Texture coords
        -1.0f + offset_x, -1.0f + offset_y,                                                  0.0f, 1.0f,  // Bottom-left
        -1.0f + offset_x + 2.0f * scale_x * img_width / surface_width, -1.0f + offset_y,   1.0f, 1.0f,  // Bottom-right  
        -1.0f + offset_x + 2.0f * scale_x * img_width / surface_width, 
        -1.0f + offset_y + 2.0f * scale_y * img_height / surface_height,                    1.0f, 0.0f,  // Top-right
        -1.0f + offset_x, -1.0f + offset_y + 2.0f * scale_y * img_height / surface_height, 0.0f, 0.0f   // Top-left
    };
    
    unsigned int indices[] = {
        0, 1, 2,   // First triangle
        2, 3, 0    // Second triangle
    };
    
    // Enable vertex arrays
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    
    // Set up vertex and texture coordinate arrays
    glVertexPointer(2, GL_FLOAT, 4 * sizeof(float), vertices);
    glTexCoordPointer(2, GL_FLOAT, 4 * sizeof(float), vertices + 2);
    
    // Draw the quad
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, indices);
    
    // Disable client states
    glDisableClientState(GL_VERTEX_ARRAY);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_BLEND);
}

bool X11ImageRenderer::init_glew_if_needed() {
    if (glew_initialized_) {
        return true;
    }
    
    GLenum err = glewInit();
    if (err != GLEW_OK) {
        std::cerr << "ERROR: Failed to initialize GLEW: " << glewGetErrorString(err) << std::endl;
        return false;
    }
    
    glew_initialized_ = true;
    std::cout << "DEBUG: GLEW initialized successfully" << std::endl;
    return true;
}

void X11ImageRenderer::convert_rgba_to_x11_format(const unsigned char* src_data, int width, int height,
                                                 unsigned char** dst_data, int* bytes_per_pixel) {
    // Get X11 display properties
    Visual* visual = DefaultVisual(x11_display_, screen_);
    int depth = DefaultDepth(x11_display_, screen_);
    
    *bytes_per_pixel = (depth + 7) / 8;
    
    if (*bytes_per_pixel == 4) {
        // 32-bit display - convert RGBA to BGRA
        *dst_data = new unsigned char[width * height * 4];
        
        for (int i = 0; i < width * height; i++) {
            int src_idx = i * 4;
            int dst_idx = i * 4;
            
            (*dst_data)[dst_idx + 0] = src_data[src_idx + 2]; // B
            (*dst_data)[dst_idx + 1] = src_data[src_idx + 1]; // G
            (*dst_data)[dst_idx + 2] = src_data[src_idx + 0]; // R
            (*dst_data)[dst_idx + 3] = src_data[src_idx + 3]; // A
        }
    } else if (*bytes_per_pixel == 3) {
        // 24-bit display - convert RGBA to RGB
        *dst_data = new unsigned char[width * height * 3];
        
        for (int i = 0; i < width * height; i++) {
            int src_idx = i * 4;
            int dst_idx = i * 3;
            
            (*dst_data)[dst_idx + 0] = src_data[src_idx + 0]; // R
            (*dst_data)[dst_idx + 1] = src_data[src_idx + 1]; // G
            (*dst_data)[dst_idx + 2] = src_data[src_idx + 2]; // B
        }
    } else {
        // Fallback - just copy as-is
        *bytes_per_pixel = 4;
        *dst_data = new unsigned char[width * height * 4];
        memcpy(*dst_data, src_data, width * height * 4);
    }
}
