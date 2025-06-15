#include "x11_video_renderer.h"
#include <iostream>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <X11/Xutil.h>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

X11VideoRenderer::X11VideoRenderer()
    : initialized_(false), x11_display_(nullptr), window_(0), screen_(0),
      graphics_context_(0), format_context_(nullptr), codec_context_(nullptr),
      codec_(nullptr), frame_(nullptr), rgb_frame_(nullptr), sws_context_(nullptr),
      stream_index_(-1), frame_buffer_(nullptr) {}

X11VideoRenderer::~X11VideoRenderer() {
    cleanup();
}

bool X11VideoRenderer::initialize(Display* x11_display, Window window, int screen) {
    if (!x11_display || !window) {
        std::cerr << "ERROR: Invalid X11 display or window" << std::endl;
        return false;
    }
    
    x11_display_ = x11_display;
    window_ = window;
    screen_ = screen;
    
    // Create graphics context
    graphics_context_ = XCreateGC(x11_display_, window_, 0, nullptr);
    if (!graphics_context_) {
        std::cerr << "ERROR: Failed to create X11 graphics context" << std::endl;
        return false;
    }
    
    initialized_ = true;
    std::cout << "DEBUG: X11VideoRenderer initialized for CPU rendering" << std::endl;
    return true;
}

void X11VideoRenderer::cleanup() {
    cleanup_ffmpeg();
    
    if (graphics_context_) {
        XFreeGC(x11_display_, graphics_context_);
        graphics_context_ = 0;
    }
    
    if (frame_buffer_) {
        delete[] frame_buffer_;
        frame_buffer_ = nullptr;
    }
    
    initialized_ = false;
    x11_display_ = nullptr;
    window_ = 0;
}

bool X11VideoRenderer::initialize_ffmpeg(const std::string& video_path) {
    if (!initialized_) {
        std::cerr << "ERROR: X11 video renderer not initialized" << std::endl;
        return false;
    }
    
    cleanup_ffmpeg();
    
    // Open video file
    if (avformat_open_input(&format_context_, video_path.c_str(), nullptr, nullptr) < 0) {
        std::cerr << "ERROR: Could not open video file: " << video_path << std::endl;
        return false;
    }
    
    // Find stream information
    if (avformat_find_stream_info(format_context_, nullptr) < 0) {
        std::cerr << "ERROR: Could not find stream information" << std::endl;
        avformat_close_input(&format_context_);
        return false;
    }
    
    // Find video stream
    stream_index_ = -1;
    for (unsigned int i = 0; i < format_context_->nb_streams; i++) {
        if (format_context_->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            stream_index_ = i;
            break;
        }
    }
    
    if (stream_index_ == -1) {
        std::cerr << "ERROR: No video stream found" << std::endl;
        avformat_close_input(&format_context_);
        return false;
    }
    
    // Get codec parameters
    AVCodecParameters* codecpar = format_context_->streams[stream_index_]->codecpar;
    
    // Find decoder
    codec_ = avcodec_find_decoder(codecpar->codec_id);
    if (!codec_) {
        std::cerr << "ERROR: Unsupported codec" << std::endl;
        avformat_close_input(&format_context_);
        return false;
    }
    
    // Create codec context
    codec_context_ = avcodec_alloc_context3(codec_);
    if (!codec_context_) {
        std::cerr << "ERROR: Could not allocate codec context" << std::endl;
        avformat_close_input(&format_context_);
        return false;
    }
    
    // Copy codec parameters to context
    if (avcodec_parameters_to_context(codec_context_, codecpar) < 0) {
        std::cerr << "ERROR: Could not copy codec parameters" << std::endl;
        avcodec_free_context(&codec_context_);
        avformat_close_input(&format_context_);
        return false;
    }
    
    // Open codec
    if (avcodec_open2(codec_context_, codec_, nullptr) < 0) {
        std::cerr << "ERROR: Could not open codec" << std::endl;
        avcodec_free_context(&codec_context_);
        avformat_close_input(&format_context_);
        return false;
    }
    
    // Allocate frames
    frame_ = av_frame_alloc();
    rgb_frame_ = av_frame_alloc();
    
    if (!frame_ || !rgb_frame_) {
        std::cerr << "ERROR: Could not allocate frames" << std::endl;
        cleanup_ffmpeg();
        return false;
    }
    
    // Setup RGB frame - use BGRA for better color compatibility
    int width = codec_context_->width;
    int height = codec_context_->height;
    
    int buffer_size = av_image_get_buffer_size(AV_PIX_FMT_BGRA, width, height, 32);
    frame_buffer_ = new uint8_t[buffer_size];
    
    av_image_fill_arrays(rgb_frame_->data, rgb_frame_->linesize, 
                        frame_buffer_, AV_PIX_FMT_BGRA, width, height, 32);
    
    // Initialize sws context for color conversion to BGRA
    sws_context_ = sws_getContext(width, height, codec_context_->pix_fmt,
                                 width, height, AV_PIX_FMT_BGRA,
                                 SWS_BILINEAR, nullptr, nullptr, nullptr);
    
    if (!sws_context_) {
        std::cerr << "ERROR: Could not initialize sws context" << std::endl;
        cleanup_ffmpeg();
        return false;
    }
    
    std::cout << "DEBUG: FFmpeg initialized for video: " << video_path << std::endl;
    std::cout << "DEBUG: Video dimensions: " << width << "x" << height << std::endl;
    return true;
}

bool X11VideoRenderer::render_video_shm(int surface_width, int surface_height, ScalingMode scaling) {
    if (!initialized_ || !format_context_ || !codec_context_) {
        std::cerr << "ERROR: Invalid state for X11 video rendering" << std::endl;
        return false;
    }
    
    // Read a frame from the video
    AVPacket* packet = av_packet_alloc();
    if (!packet) {
        return false;
    }
    
    while (av_read_frame(format_context_, packet) >= 0) {
        if (packet->stream_index == stream_index_) {
            // Send packet to decoder
            int ret = avcodec_send_packet(codec_context_, packet);
            if (ret < 0) {
                av_packet_unref(packet);
                continue;
            }
            
            // Receive decoded frame
            ret = avcodec_receive_frame(codec_context_, frame_);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                av_packet_unref(packet);
                continue;
            } else if (ret < 0) {
                av_packet_unref(packet);
                av_packet_free(&packet);
                std::cerr << "ERROR: Error during decoding" << std::endl;
                return false;
            }
            
            // Standard video frame conversion without flipping
            sws_scale(sws_context_, frame_->data, frame_->linesize, 0, 
                     codec_context_->height, rgb_frame_->data, rgb_frame_->linesize);
            
            // Render the RGB frame
            bool result = render_rgb_frame_x11(rgb_frame_->data[0], codec_context_->width, 
                                             codec_context_->height, surface_width, 
                                             surface_height, scaling);
            
            av_packet_unref(packet);
            av_packet_free(&packet);
            return result;
        }
        av_packet_unref(packet);
    }
    
    // End of stream or no suitable frame found
    av_packet_free(&packet);
    return false;
}

bool X11VideoRenderer::render_video_x11(int surface_width, int surface_height, ScalingMode scaling) {
    // Alias for render_video_shm for compatibility
    return render_video_shm(surface_width, surface_height, scaling);
}

bool X11VideoRenderer::render_rgb_frame_x11(const unsigned char* frame_data, int frame_width, int frame_height,
                                           int surface_width, int surface_height, ScalingMode scaling,
                                           bool windowed_mode) {
    if (!initialized_ || !frame_data) {
        std::cerr << "ERROR: Invalid data for X11 frame rendering" << std::endl;
        return false;
    }
    
    std::cout << "DEBUG: *** X11 VIDEO ORIENTATION DEBUG *** render_rgb_frame_x11 called" << std::endl;
    std::cout << "DEBUG: *** X11 VIDEO ORIENTATION DEBUG *** windowed_mode = " << (windowed_mode ? "TRUE (window mode)" : "FALSE (background mode)") << std::endl;
    std::cout << "DEBUG: *** X11 VIDEO ORIENTATION DEBUG *** Frame dimensions: " << frame_width << "x" << frame_height << std::endl;
    std::cout << "DEBUG: *** X11 VIDEO ORIENTATION DEBUG *** Y-axis flip will be " << (windowed_mode ? "ENABLED" : "DISABLED") << " for this render" << std::endl;
    
    // Check if we have a valid graphics context
    if (!graphics_context_) {
        std::cerr << "ERROR: No valid graphics context for X11 rendering" << std::endl;
        return false;
    }
    
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
            double frame_aspect = (double)frame_width / frame_height;
            double surface_aspect = (double)surface_width / surface_height;
            
            if (frame_aspect > surface_aspect) {
                dest_width = surface_width;
                dest_height = (int)(surface_width / frame_aspect);
                dest_y = (surface_height - dest_height) / 2;
            } else {
                dest_height = surface_height;
                dest_width = (int)(surface_height * frame_aspect);
                dest_x = (surface_width - dest_width) / 2;
            }
            break;
        }
        
        case ScalingMode::FILL: {
            // Scale to fill surface, maintaining aspect ratio (crop if needed)
            // For FILL mode, we always use the full surface size and crop the content
            dest_width = surface_width;
            dest_height = surface_height;
            dest_x = 0;
            dest_y = 0;
            break;
        }
        
        case ScalingMode::DEFAULT:
            // Use original frame size, centered
            dest_width = frame_width;
            dest_height = frame_height;
            dest_x = (surface_width - dest_width) / 2;
            dest_y = (surface_height - dest_height) / 2;
            break;
    }
    
    // Convert BGRA to X11 format
    unsigned char* x11_data = nullptr;
    int bytes_per_pixel = 0;
    convert_bgra_to_x11_format(frame_data, frame_width, frame_height, &x11_data, &bytes_per_pixel);
    
    if (!x11_data) {
        std::cerr << "ERROR: Failed to convert frame data to X11 format" << std::endl;
        return false;
    }
    
    // Scale the frame data if needed
    unsigned char* scaled_data = nullptr;
    if (dest_width != frame_width || dest_height != frame_height) {
        scaled_data = new unsigned char[dest_width * dest_height * bytes_per_pixel];
        apply_scaling_x11(x11_data, frame_width, frame_height, scaled_data, dest_width, dest_height, scaling, bytes_per_pixel, windowed_mode);
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
        std::cerr << "ERROR: Failed to create XImage for video frame" << std::endl;
        return false;
    }
    
    // Clear the window if using FIT mode to avoid artifacts
    if (scaling == ScalingMode::FIT && window_ != 0) {
        XClearWindow(x11_display_, window_);
    }
    
    // Draw the frame
    int result = XPutImage(x11_display_, window_, graphics_context_, ximage, 0, 0, dest_x, dest_y, dest_width, dest_height);
    
    if (result == BadDrawable || result == BadGC || result == BadMatch) {
        std::cerr << "ERROR: XPutImage failed with error code: " << result << std::endl;
        XDestroyImage(ximage);
        return false;
    }
    
    // Cleanup
    XDestroyImage(ximage); // This also frees x11_data
    XFlush(x11_display_);
    
    return true;
}
void X11VideoRenderer::cleanup_ffmpeg() {
    if (sws_context_) {
        sws_freeContext(sws_context_);
        sws_context_ = nullptr;
    }
    
    if (frame_) {
        av_frame_free(&frame_);
    }
    
    if (rgb_frame_) {
        av_frame_free(&rgb_frame_);
    }
    
    if (codec_context_) {
        avcodec_free_context(&codec_context_);
    }
    
    if (format_context_) {
        avformat_close_input(&format_context_);
    }
    
    if (frame_buffer_) {
        delete[] frame_buffer_;
        frame_buffer_ = nullptr;
    }
    
    codec_ = nullptr;
    stream_index_ = -1;
}

bool X11VideoRenderer::get_video_dimensions(int* width, int* height) {
    if (!codec_context_) {
        return false;
    }
    
    *width = codec_context_->width;
    *height = codec_context_->height;
    return (*width > 0 && *height > 0);
}

void X11VideoRenderer::seek_to_time(double time_seconds) {
    if (!format_context_) {
        return;
    }
    
    int64_t timestamp = time_seconds * AV_TIME_BASE;
    av_seek_frame(format_context_, -1, timestamp, AVSEEK_FLAG_BACKWARD);
    
    if (codec_context_) {
        avcodec_flush_buffers(codec_context_);
    }
}

void X11VideoRenderer::apply_scaling_x11(const unsigned char* src_data, int src_width, int src_height,
                                         unsigned char* dst_data, int dst_width, int dst_height,
                                         ScalingMode scaling, int bytes_per_pixel, bool windowed_mode) {
    std::cout << "DEBUG: *** X11 VIDEO SCALING ORIENTATION DEBUG *** apply_scaling_x11 called" << std::endl;
    std::cout << "DEBUG: *** X11 VIDEO SCALING ORIENTATION DEBUG *** windowed_mode = " << (windowed_mode ? "TRUE (window mode)" : "FALSE (background mode)") << std::endl;
    std::cout << "DEBUG: *** X11 VIDEO SCALING ORIENTATION DEBUG *** Y-axis flip will be " << (windowed_mode ? "ENABLED" : "DISABLED") << " for video scaling operation" << std::endl;
    if (scaling == ScalingMode::FILL) {
        // For FILL mode, we need to crop the content to maintain aspect ratio
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
        // CONDITIONAL Y-AXIS ORIENTATION FIX FOR X11 VIDEO RENDERING (FILL MODE)
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
        // END CONDITIONAL Y-AXIS ORIENTATION FIX FOR X11 VIDEO RENDERING (FILL MODE)
        // ============================================================================
    } else {
        // Simple nearest-neighbor scaling for other modes with conditional Y-axis flip
        // ============================================================================
        // CONDITIONAL Y-AXIS ORIENTATION FIX FOR X11 VIDEO RENDERING
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
        // END CONDITIONAL Y-AXIS ORIENTATION FIX FOR X11 VIDEO RENDERING
        // ============================================================================
    }
}

void X11VideoRenderer::convert_bgra_to_x11_format(const unsigned char* src_data, int width, int height,
                                                  unsigned char** dst_data, int* bytes_per_pixel) {
    // Get X11 display properties
    Visual* visual = DefaultVisual(x11_display_, screen_);
    int depth = DefaultDepth(x11_display_, screen_);
    
    *bytes_per_pixel = (depth + 7) / 8;
    
    if (*bytes_per_pixel == 4) {
        // 32-bit display - convert BGRA to BGRA (X11 format)
        *dst_data = new unsigned char[width * height * 4];
        
        for (int i = 0; i < width * height; i++) {
            int src_idx = i * 4;
            int dst_idx = i * 4;
            
            (*dst_data)[dst_idx + 0] = src_data[src_idx + 0]; // B (already in correct position)
            (*dst_data)[dst_idx + 1] = src_data[src_idx + 1]; // G (already in correct position)
            (*dst_data)[dst_idx + 2] = src_data[src_idx + 2]; // R (already in correct position)
            (*dst_data)[dst_idx + 3] = src_data[src_idx + 3]; // A (use from source)
        }
    } else if (*bytes_per_pixel == 3) {
        // 24-bit display - convert BGRA to BGR
        *dst_data = new unsigned char[width * height * 3];
        
        for (int i = 0; i < width * height; i++) {
            int src_idx = i * 4;
            int dst_idx = i * 3;
            
            (*dst_data)[dst_idx + 0] = src_data[src_idx + 0]; // B
            (*dst_data)[dst_idx + 1] = src_data[src_idx + 1]; // G
            (*dst_data)[dst_idx + 2] = src_data[src_idx + 2]; // R
        }
    } else {
        // Fallback - expand BGRA to 32-bit BGRA
        *bytes_per_pixel = 4;
        *dst_data = new unsigned char[width * height * 4];
        
        for (int i = 0; i < width * height; i++) {
            int src_idx = i * 4;
            int dst_idx = i * 4;
            
            (*dst_data)[dst_idx + 0] = src_data[src_idx + 0]; // B
            (*dst_data)[dst_idx + 1] = src_data[src_idx + 1]; // G
            (*dst_data)[dst_idx + 2] = src_data[src_idx + 2]; // R
            (*dst_data)[dst_idx + 3] = src_data[src_idx + 3]; // A
        }
    }
}
