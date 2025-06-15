#include "wayland_video_renderer.h"
#include <iostream>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <sys/mman.h>
#include <unistd.h>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

WaylandVideoRenderer::WaylandVideoRenderer()
    : initialized_(false), wayland_display_(nullptr), shm_(nullptr),
      format_context_(nullptr), codec_context_(nullptr), codec_(nullptr),
      frame_(nullptr), rgb_frame_(nullptr), sws_context_(nullptr),
      stream_index_(-1), frame_buffer_(nullptr) {}

WaylandVideoRenderer::~WaylandVideoRenderer() {
    cleanup();
}

bool WaylandVideoRenderer::initialize(struct wl_display* wayland_display, struct wl_shm* shm) {
    if (initialized_) {
        return true;
    }
    
    if (!wayland_display || !shm) {
        std::cerr << "ERROR: Invalid Wayland display or shm provided to video renderer" << std::endl;
        return false;
    }
    
    wayland_display_ = wayland_display;
    shm_ = shm;
    
    initialized_ = true;
    std::cout << "DEBUG: WaylandVideoRenderer initialized for CPU rendering" << std::endl;
    return true;
}

void WaylandVideoRenderer::cleanup() {
    cleanup_ffmpeg();
    
    if (frame_buffer_) {
        delete[] frame_buffer_;
        frame_buffer_ = nullptr;
    }
    
    initialized_ = false;
    wayland_display_ = nullptr;
    shm_ = nullptr;
}

bool WaylandVideoRenderer::initialize_ffmpeg(const std::string& video_path) {
    if (!initialized_) {
        std::cerr << "ERROR: Wayland video renderer not initialized" << std::endl;
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
    
    // Setup RGB frame - use BGRA for Wayland SHM compatibility
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

bool WaylandVideoRenderer::render_video_shm(void* shm_data, int surface_width, int surface_height,
                                           ScalingMode scaling) {
    if (!initialized_ || !format_context_ || !codec_context_ || !shm_data) {
        std::cerr << "ERROR: Invalid state for SHM video rendering" << std::endl;
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
            
            // Clear the SHM buffer
            size_t buffer_size = surface_width * surface_height * 4;
            memset(shm_data, 0, buffer_size);
            
            // Apply scaling and convert to SHM format
            apply_scaling_shm(rgb_frame_->data[0], codec_context_->width, codec_context_->height,
                             reinterpret_cast<unsigned char*>(shm_data),
                             surface_width, surface_height, scaling, false); // false = background mode
            
            av_packet_unref(packet);
            av_packet_free(&packet);
            return true;
        }
        av_packet_unref(packet);
    }
    
    // End of stream or no suitable frame found
    av_packet_free(&packet);
    return false;
}

bool WaylandVideoRenderer::render_rgb_frame_shm(const unsigned char* frame_data, int frame_width, int frame_height,
                                               void* shm_data, int surface_width, int surface_height,
                                               ScalingMode scaling, bool windowed_mode) {
    if (!frame_data || !shm_data) {
        std::cerr << "ERROR: Invalid data for RGB frame SHM rendering" << std::endl;
        return false;
    }
    
    // Clear the SHM buffer
    size_t buffer_size = surface_width * surface_height * 4;
    memset(shm_data, 0, buffer_size);
    
    // Apply scaling and convert to SHM format
    apply_scaling_shm(frame_data, frame_width, frame_height,
                     reinterpret_cast<unsigned char*>(shm_data),
                     surface_width, surface_height, scaling, windowed_mode);
    
    return true;
}

void WaylandVideoRenderer::cleanup_ffmpeg() {
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

bool WaylandVideoRenderer::get_video_dimensions(int* width, int* height) {
    if (!codec_context_) {
        return false;
    }
    
    *width = codec_context_->width;
    *height = codec_context_->height;
    return (*width > 0 && *height > 0);
}

void WaylandVideoRenderer::seek_to_time(double time_seconds) {
    if (!format_context_) {
        return;
    }
    
    int64_t timestamp = time_seconds * AV_TIME_BASE;
    av_seek_frame(format_context_, -1, timestamp, AVSEEK_FLAG_BACKWARD);
    
    if (codec_context_) {
        avcodec_flush_buffers(codec_context_);
    }
}

bool WaylandVideoRenderer::render_frame_data_shm(const unsigned char* frame_data, int frame_width, int frame_height,
                                                void* shm_data, int surface_width, int surface_height,
                                                ScalingMode scaling, bool windowed_mode) {
    if (!frame_data || !shm_data) {
        std::cerr << "ERROR: Invalid data for frame SHM rendering" << std::endl;
        return false;
    }
    
    static int render_frame_call_count = 0;
    render_frame_call_count++;
    
    std::cout << "DEBUG: *** VIDEO FILL MODE DEBUG *** ====== render_frame_data_shm CALL " << render_frame_call_count << " ======" << std::endl;
    std::cout << "DEBUG: *** VIDEO FILL MODE DEBUG *** windowed_mode = " << (windowed_mode ? "TRUE (window mode)" : "FALSE (background mode)") << std::endl;
    std::cout << "DEBUG: *** VIDEO FILL MODE DEBUG *** scaling = " << static_cast<int>(scaling) << " (0=DEFAULT, 1=STRETCH, 2=FIT, 3=FILL)" << std::endl;
    std::cout << "DEBUG: *** VIDEO FILL MODE DEBUG *** Frame dimensions: " << frame_width << "x" << frame_height << std::endl;
    std::cout << "DEBUG: *** VIDEO FILL MODE DEBUG *** Surface dimensions: " << surface_width << "x" << surface_height << std::endl;
    std::cout << "DEBUG: *** VIDEO FILL MODE DEBUG *** shm_data pointer = " << shm_data << std::endl;
    
    // Clear the SHM buffer
    size_t buffer_size = surface_width * surface_height * 4;
    std::cout << "DEBUG: *** VIDEO FILL MODE DEBUG *** Clearing SHM buffer of size " << buffer_size << " bytes" << std::endl;
    memset(shm_data, 0, buffer_size);
    std::cout << "DEBUG: *** VIDEO FILL MODE DEBUG *** SHM buffer cleared to black" << std::endl;
    
    // Apply scaling and convert to SHM format
    std::cout << "DEBUG: *** VIDEO FILL MODE DEBUG *** About to call apply_scaling_shm" << std::endl;
    apply_scaling_shm(frame_data, frame_width, frame_height,
                     reinterpret_cast<unsigned char*>(shm_data),
                     surface_width, surface_height, scaling, windowed_mode);
    
    std::cout << "DEBUG: *** VIDEO FILL MODE DEBUG *** apply_scaling_shm completed successfully" << std::endl;
    std::cout << "DEBUG: *** VIDEO FILL MODE DEBUG *** ====== render_frame_data_shm CALL " << render_frame_call_count << " COMPLETE ======" << std::endl;
    return true;
}

void WaylandVideoRenderer::apply_scaling_shm(const unsigned char* src_data, int src_width, int src_height,
                                             unsigned char* dst_data, int dst_width, int dst_height,
                                             ScalingMode scaling, bool windowed_mode) {
    static int apply_scaling_call_count = 0;
    apply_scaling_call_count++;
    
    std::cout << "DEBUG: *** VIDEO FILL MODE DEBUG *** ====== apply_scaling_shm CALL " << apply_scaling_call_count << " ======" << std::endl;
    std::cout << "DEBUG: *** VIDEO FILL MODE DEBUG *** Source dimensions: " << src_width << "x" << src_height << std::endl;
    std::cout << "DEBUG: *** VIDEO FILL MODE DEBUG *** Destination dimensions: " << dst_width << "x" << dst_height << std::endl;
    std::cout << "DEBUG: *** VIDEO FILL MODE DEBUG *** Scaling mode: " << static_cast<int>(scaling) << " (0=DEFAULT, 1=STRETCH, 2=FIT, 3=FILL)" << std::endl;
    std::cout << "DEBUG: *** VIDEO FILL MODE DEBUG *** windowed_mode = " << (windowed_mode ? "TRUE" : "FALSE") << std::endl;
    
    // Calculate actual rendering dimensions based on scaling mode
    int render_width = dst_width;
    int render_height = dst_height;
    int offset_x = 0;
    int offset_y = 0;
    
    switch (scaling) {
        case ScalingMode::STRETCH:
            std::cout << "DEBUG: *** VIDEO FILL MODE DEBUG *** Using STRETCH mode - full destination size" << std::endl;
            // Use full destination size (already set)
            break;
            
        case ScalingMode::FIT: {
            std::cout << "DEBUG: *** VIDEO FILL MODE DEBUG *** Using FIT mode - letterbox/pillarbox" << std::endl;
            // Scale to fit within destination, maintaining aspect ratio
            double src_aspect = (double)src_width / src_height;
            double dst_aspect = (double)dst_width / dst_height;
            
            std::cout << "DEBUG: *** VIDEO FILL MODE DEBUG *** Source aspect: " << src_aspect << ", Destination aspect: " << dst_aspect << std::endl;
            
            if (src_aspect > dst_aspect) {
                render_width = dst_width;
                render_height = (int)(dst_width / src_aspect);
                offset_y = (dst_height - render_height) / 2;
                std::cout << "DEBUG: *** VIDEO FILL MODE DEBUG *** FIT: Source wider - render=" << render_width << "x" << render_height << ", offset_y=" << offset_y << std::endl;
            } else {
                render_height = dst_height;
                render_width = (int)(dst_height * src_aspect);
                offset_x = (dst_width - render_width) / 2;
                std::cout << "DEBUG: *** VIDEO FILL MODE DEBUG *** FIT: Source taller - render=" << render_width << "x" << render_height << ", offset_x=" << offset_x << std::endl;
            }
            break;
        }
        
        case ScalingMode::FILL: {
            std::cout << "DEBUG: *** VIDEO FILL MODE DEBUG *** Using FILL mode - crop to fill destination" << std::endl;
            // Scale to fill destination, maintaining aspect ratio (may crop)
            double src_aspect = (double)src_width / src_height;
            double dst_aspect = (double)dst_width / dst_height;
            
            std::cout << "DEBUG: *** VIDEO FILL MODE DEBUG *** Source aspect: " << src_aspect << ", Destination aspect: " << dst_aspect << std::endl;
            
            if (src_aspect > dst_aspect) {
                // Source is wider, crop horizontally - use negative offset to center crop
                render_height = dst_height;
                render_width = (int)(dst_height * src_aspect);
                offset_x = -(render_width - dst_width) / 2;  // Negative offset for cropping
                offset_y = 0;
                std::cout << "DEBUG: *** VIDEO FILL MODE DEBUG *** FILL: Source wider - crop horizontally" << std::endl;
                std::cout << "DEBUG: *** VIDEO FILL MODE DEBUG *** FILL: render=" << render_width << "x" << render_height << ", offset_x=" << offset_x << " (NEGATIVE - cropping)" << std::endl;
            } else {
                // Source is taller, crop vertically - use negative offset to center crop  
                render_width = dst_width;
                render_height = (int)(dst_width / src_aspect);
                offset_x = 0;
                offset_y = -(render_height - dst_height) / 2;  // Negative offset for cropping
                std::cout << "DEBUG: *** VIDEO FILL MODE DEBUG *** FILL: Source taller - crop vertically" << std::endl;
                std::cout << "DEBUG: *** VIDEO FILL MODE DEBUG *** FILL: render=" << render_width << "x" << render_height << ", offset_y=" << offset_y << " (NEGATIVE - cropping)" << std::endl;
            }
            break;
        }
        
        case ScalingMode::DEFAULT:
            std::cout << "DEBUG: *** VIDEO FILL MODE DEBUG *** Using DEFAULT mode - original size centered" << std::endl;
            // Use original source size, centered
            render_width = src_width;
            render_height = src_height;
            offset_x = (dst_width - render_width) / 2;
            offset_y = (dst_height - render_height) / 2;
            std::cout << "DEBUG: *** VIDEO FILL MODE DEBUG *** DEFAULT: render=" << render_width << "x" << render_height << ", offset=(" << offset_x << "," << offset_y << ")" << std::endl;
            break;
    }
    
    std::cout << "DEBUG: *** VIDEO FILL MODE DEBUG *** Final parameters: render=" << render_width << "x" << render_height << ", offset=(" << offset_x << "," << offset_y << ")" << std::endl;
    
    // For FILL mode, negative offsets are allowed to enable cropping
    // For other modes, clamp offsets to prevent rendering outside bounds
    if (scaling != ScalingMode::FILL) {
        if (offset_x < 0) offset_x = 0;
        if (offset_y < 0) offset_y = 0;
        if (render_width + offset_x > dst_width) render_width = dst_width - offset_x;
        if (render_height + offset_y > dst_height) render_height = dst_height - offset_y;
        std::cout << "DEBUG: *** VIDEO FILL MODE DEBUG *** Non-FILL mode: clamped offsets and render size" << std::endl;
    } else {
        std::cout << "DEBUG: *** VIDEO FILL MODE DEBUG *** FILL mode: allowing negative offsets for cropping" << std::endl;
    }
    
    std::cout << "DEBUG: *** VIDEO FILL MODE DEBUG *** About to start pixel copying loop" << std::endl;
    std::cout << "DEBUG: *** VIDEO FILL MODE DEBUG *** Loop will iterate: y=0 to " << (render_height-1) << ", x=0 to " << (render_width-1) << std::endl;
    
    int pixels_copied = 0;
    int pixels_skipped = 0;
    
    // Scale and copy the image with Y-axis flip (BGRA -> RGBA for Wayland SHM)
    // ============================================================================
    // CRITICAL Y-AXIS ORIENTATION FIX FOR WAYLAND SHM VIDEO RENDERING
    // DO NOT MODIFY: This Y-axis flip corrects upside-down videos at the pixel 
    // copying level, which is the proper place for orientation fixes according 
    // to the reference implementation (Almamu/linux-wallpaperengine).
    // ============================================================================
    
    for (int y = 0; y < render_height; y++) {
        for (int x = 0; x < render_width; x++) {
            // Calculate destination pixel position with offsets
            int dst_x = x + offset_x;
            int dst_y = y + offset_y;
            
            // Skip pixels outside destination bounds (critical for FILL mode with negative offsets)
            if (dst_x < 0 || dst_x >= dst_width || dst_y < 0 || dst_y >= dst_height) {
                pixels_skipped++;
                continue;
            }
            
            // Calculate source pixel position
            int src_x = (x * src_width) / render_width;
            int src_y = (y * src_height) / render_height;
            
            // ============================================================================
            // CRITICAL Y-AXIS ORIENTATION FIX FOR WAYLAND SHM VIDEO RENDERING
            // DO NOT MODIFY: Apply Y-axis flip only for window mode (windowed_mode = true)
            // Background mode works correctly as-is, window mode needs Y-axis flip.
            // ============================================================================
            if (windowed_mode) {
                // Window mode: Apply Y-axis flip to correct upside-down videos
                src_y = src_height - 1 - src_y;
            }
            // Background mode: Use normal Y coordinates (no flip needed)
            
            // Clamp source coordinates to bounds
            if (src_x >= src_width) src_x = src_width - 1;
            if (src_y < 0) src_y = 0;
            if (src_y >= src_height) src_y = src_height - 1;
            
            // Source pixel index (RGB/RGBA from FFmpeg)
            int src_idx = (src_y * src_width + src_x) * 4;
            
            // Destination pixel index for Wayland SHM ARGB8888 format
            int dst_idx = (dst_y * dst_width + dst_x) * 4;
            
            // Convert RGBA to ARGB (Wayland SHM format WL_SHM_FORMAT_ARGB8888)
            // SHM format expects: B, G, R, A in memory (little-endian ARGB)
            dst_data[dst_idx + 0] = src_data[src_idx + 2]; // B
            dst_data[dst_idx + 1] = src_data[src_idx + 1]; // G 
            dst_data[dst_idx + 2] = src_data[src_idx + 0]; // R
            dst_data[dst_idx + 3] = src_data[src_idx + 3]; // A
            
            pixels_copied++;
        }
    }
    
    std::cout << "DEBUG: *** VIDEO FILL MODE DEBUG *** Pixel copying complete: " << pixels_copied << " copied, " << pixels_skipped << " skipped" << std::endl;
    std::cout << "DEBUG: *** VIDEO FILL MODE DEBUG *** ====== apply_scaling_shm CALL " << apply_scaling_call_count << " COMPLETE ======" << std::endl;
    
    // ============================================================================
    // END CRITICAL Y-AXIS ORIENTATION FIX FOR WAYLAND SHM VIDEO RENDERING
    // ============================================================================
}
