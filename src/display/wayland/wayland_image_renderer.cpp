#include "wayland_image_renderer.h"
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

WaylandImageRenderer::WaylandImageRenderer()
    : initialized_(false) {}

WaylandImageRenderer::~WaylandImageRenderer() {
    cleanup();
}

bool WaylandImageRenderer::initialize() {
    if (initialized_) {
        return true;
    }
    
    std::cout << "DEBUG: WaylandImageRenderer initialized for SHM-only rendering" << std::endl;
    initialized_ = true;
    return true;
}

void WaylandImageRenderer::cleanup() {
    initialized_ = false;
}

bool WaylandImageRenderer::render_image_shm(const unsigned char* image_data, int img_width, int img_height,
                                           void* shm_data, int surface_width, int surface_height,
                                           ScalingMode scaling, bool windowed_mode) {
    if (!initialized_ || !image_data || !shm_data) {
        std::cerr << "ERROR: Invalid parameters for SHM image rendering" << std::endl;
        return false;
    }
    
    std::cout << "DEBUG: WaylandImageRenderer::render_image_shm - Start" << std::endl;
    std::cout << "DEBUG: Image dimensions: " << img_width << "x" << img_height << std::endl;
    std::cout << "DEBUG: Surface dimensions: " << surface_width << "x" << surface_height << std::endl;
    std::cout << "DEBUG: *** ORIENTATION DEBUG *** windowed_mode = " << (windowed_mode ? "TRUE (window mode)" : "FALSE (background mode)") << std::endl;
    std::cout << "DEBUG: *** ORIENTATION DEBUG *** Y-axis flip will be " << (windowed_mode ? "ENABLED" : "DISABLED") << " for this render" << std::endl;
    
    // Clear the SHM buffer
    size_t buffer_size = surface_width * surface_height * 4;
    memset(shm_data, 0, buffer_size);
    
    // Check if image needs resizing for better performance
    unsigned char* resized_data = nullptr;
    int final_width = img_width;
    int final_height = img_height;
    
    check_and_resize_image(image_data, img_width, img_height, &resized_data, &final_width, &final_height);
    const unsigned char* data_to_use = resized_data ? resized_data : image_data;
    
    // Apply scaling and render to SHM buffer
    apply_scaling_shm(data_to_use, final_width, final_height,
                     static_cast<unsigned char*>(shm_data), surface_width, surface_height,
                     scaling, windowed_mode);
    
    // Clean up temporary data
    if (resized_data) {
        delete[] resized_data;
    }
    
    std::cout << "DEBUG: WaylandImageRenderer::render_image_shm - Complete" << std::endl;
    return true;
}

bool WaylandImageRenderer::load_image_from_file(const std::string& image_path, unsigned char** image_data,
                                               int* width, int* height) {
    std::cout << "DEBUG: Loading image from file: " << image_path << std::endl;
    
    // Initialize FFmpeg if not already done
    static bool ffmpeg_initialized = false;
    if (!ffmpeg_initialized) {
        av_log_set_level(AV_LOG_ERROR); // Reduce FFmpeg log spam
        ffmpeg_initialized = true;
    }
    
    AVFormatContext* format_ctx = nullptr;
    if (avformat_open_input(&format_ctx, image_path.c_str(), nullptr, nullptr) != 0) {
        std::cerr << "ERROR: Could not open image file: " << image_path << std::endl;
        return false;
    }
    
    if (avformat_find_stream_info(format_ctx, nullptr) < 0) {
        std::cerr << "ERROR: Could not find stream info for image: " << image_path << std::endl;
        avformat_close_input(&format_ctx);
        return false;
    }
    
    // Find video stream (images are treated as single-frame videos)
    int video_stream_index = -1;
    for (unsigned int i = 0; i < format_ctx->nb_streams; i++) {
        if (format_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            video_stream_index = i;
            break;
        }
    }
    
    if (video_stream_index == -1) {
        std::cerr << "ERROR: No video stream found in image file" << std::endl;
        avformat_close_input(&format_ctx);
        return false;
    }
    
    AVStream* video_stream = format_ctx->streams[video_stream_index];
    const AVCodec* codec = avcodec_find_decoder(video_stream->codecpar->codec_id);
    if (!codec) {
        std::cerr << "ERROR: Codec not found for image" << std::endl;
        avformat_close_input(&format_ctx);
        return false;
    }
    
    AVCodecContext* codec_ctx = avcodec_alloc_context3(codec);
    if (!codec_ctx) {
        std::cerr << "ERROR: Could not allocate codec context" << std::endl;
        avformat_close_input(&format_ctx);
        return false;
    }
    
    if (avcodec_parameters_to_context(codec_ctx, video_stream->codecpar) < 0) {
        std::cerr << "ERROR: Could not copy codec parameters" << std::endl;
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
    
    // Read and decode the first frame
    AVPacket* packet = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();
    AVFrame* frame_rgb = av_frame_alloc();
    
    if (!packet || !frame || !frame_rgb) {
        std::cerr << "ERROR: Could not allocate frame structures" << std::endl;
        if (packet) av_packet_free(&packet);
        if (frame) av_frame_free(&frame);
        if (frame_rgb) av_frame_free(&frame_rgb);
        avcodec_free_context(&codec_ctx);
        avformat_close_input(&format_ctx);
        return false;
    }
    
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
        av_packet_free(&packet);
        av_frame_free(&frame);
        av_frame_free(&frame_rgb);
        avcodec_free_context(&codec_ctx);
        avformat_close_input(&format_ctx);
        return false;
    }
    
    // Read and decode frame
    bool frame_decoded = false;
    while (av_read_frame(format_ctx, packet) >= 0) {
        if (packet->stream_index == video_stream_index) {
            if (avcodec_send_packet(codec_ctx, packet) == 0) {
                if (avcodec_receive_frame(codec_ctx, frame) == 0) {
                    // Standard RGBA conversion without flipping
                    sws_scale(sws_ctx, frame->data, frame->linesize, 0, *height,
                             frame_rgb->data, frame_rgb->linesize);
                    // ============================================================================
                    // END CRITICAL Y-AXIS ORIENTATION FIX
                    // ============================================================================
                    frame_decoded = true;
                    break;
                }
            }
        }
        av_packet_unref(packet);
    }
    
    // Clean up
    sws_freeContext(sws_ctx);
    av_packet_free(&packet);
    av_frame_free(&frame);
    av_frame_free(&frame_rgb);
    avcodec_free_context(&codec_ctx);
    avformat_close_input(&format_ctx);
    
    if (!frame_decoded) {
        std::cerr << "ERROR: Could not decode image frame" << std::endl;
        delete[] *image_data;
        return false;
    }
    
    std::cout << "DEBUG: Successfully loaded image: " << *width << "x" << *height << std::endl;
    return true;
}

void WaylandImageRenderer::free_image_data(unsigned char* image_data) {
    if (image_data) {
        delete[] image_data;
    }
}

void WaylandImageRenderer::check_and_resize_image(const unsigned char* src_data, int src_width, int src_height,
                                                 unsigned char** dst_data, int* dst_width, int* dst_height) {
    // Maximum texture size for compatibility
    const int MAX_SIZE = 4096;
    
    if (src_width <= MAX_SIZE && src_height <= MAX_SIZE) {
        // No resize needed
        *dst_data = nullptr;
        *dst_width = src_width;
        *dst_height = src_height;
        return;
    }
    
    // Calculate new dimensions while preserving aspect ratio
    float scale_factor = std::min(static_cast<float>(MAX_SIZE) / src_width,
                                 static_cast<float>(MAX_SIZE) / src_height);
    
    *dst_width = static_cast<int>(src_width * scale_factor);
    *dst_height = static_cast<int>(src_height * scale_factor);
    
    std::cout << "DEBUG: Resizing image from " << src_width << "x" << src_height 
              << " to " << *dst_width << "x" << *dst_height << std::endl;
    
    *dst_data = new unsigned char[(*dst_width) * (*dst_height) * 4];
    
    // Simple nearest-neighbor scaling
    float x_ratio = static_cast<float>(src_width) / (*dst_width);
    float y_ratio = static_cast<float>(src_height) / (*dst_height);
    
    for (int y = 0; y < *dst_height; y++) {
        for (int x = 0; x < *dst_width; x++) {
            int src_x = static_cast<int>(x * x_ratio);
            int src_y = static_cast<int>(y * y_ratio);
            
            // Clamp to source bounds
            src_x = std::min(src_x, src_width - 1);
            src_y = std::min(src_y, src_height - 1);
            
            int dst_idx = (y * (*dst_width) + x) * 4;
            int src_idx = (src_y * src_width + src_x) * 4;
            
            (*dst_data)[dst_idx + 0] = src_data[src_idx + 0]; // R
            (*dst_data)[dst_idx + 1] = src_data[src_idx + 1]; // G
            (*dst_data)[dst_idx + 2] = src_data[src_idx + 2]; // B
            (*dst_data)[dst_idx + 3] = src_data[src_idx + 3]; // A
        }
    }
}

void WaylandImageRenderer::apply_scaling_shm(const unsigned char* src_data, int src_width, int src_height,
                                            unsigned char* dst_data, int dst_width, int dst_height,
                                            ScalingMode scaling, bool windowed_mode) {
    std::cout << "DEBUG: Applying SHM scaling mode: " << static_cast<int>(scaling) << std::endl;
    std::cout << "DEBUG: *** ORIENTATION DEBUG *** apply_scaling_shm called with windowed_mode = " << (windowed_mode ? "TRUE" : "FALSE") << std::endl;
    std::cout << "DEBUG: *** ORIENTATION DEBUG *** Source dimensions: " << src_width << "x" << src_height << std::endl;
    std::cout << "DEBUG: *** ORIENTATION DEBUG *** Destination dimensions: " << dst_width << "x" << dst_height << std::endl;
    
    // Calculate scaling parameters based on mode
    int render_width = dst_width;
    int render_height = dst_height;
    int offset_x = 0;
    int offset_y = 0;
    
    float src_aspect = static_cast<float>(src_width) / src_height;
    float dst_aspect = static_cast<float>(dst_width) / dst_height;
    
    // Clear the destination buffer first
    uint32_t* dst_pixels = reinterpret_cast<uint32_t*>(dst_data);
    for (int i = 0; i < dst_width * dst_height; i++) {
        dst_pixels[i] = 0x00000000; // Transparent black
    }
    
    switch (scaling) {
        case ScalingMode::STRETCH:
            // Stretch to fill entire surface
            break;
            
        case ScalingMode::FIT:
            // Scale to fit while maintaining aspect ratio (letterbox/pillarbox)
            if (src_aspect > dst_aspect) {
                // Image is wider - fit to width, add letterbox
                render_height = static_cast<int>(dst_width / src_aspect);
                offset_y = (dst_height - render_height) / 2;
            } else {
                // Image is taller - fit to height, add pillarbox
                render_width = static_cast<int>(dst_height * src_aspect);
                offset_x = (dst_width - render_width) / 2;
            }
            break;
            
        case ScalingMode::FILL:
            // Scale to fill while maintaining aspect ratio (crop if needed)
            if (src_aspect > dst_aspect) {
                // Image is wider - fit to height, crop width
                render_width = static_cast<int>(dst_height * src_aspect);
                offset_x = -(render_width - dst_width) / 2;
            } else {
                // Image is taller - fit to width, crop height
                render_height = static_cast<int>(dst_width / src_aspect);
                offset_y = -(render_height - dst_height) / 2;
            }
            break;
            
        case ScalingMode::DEFAULT:
        default:
            // For now, use FIT as default
            if (src_aspect > dst_aspect) {
                render_height = static_cast<int>(dst_width / src_aspect);
                offset_y = (dst_height - render_height) / 2;
            } else {
                render_width = static_cast<int>(dst_height * src_aspect);
                offset_x = (dst_width - render_width) / 2;
            }
            break;
    }
    
    // Perform the scaling and copying with Y-axis flip
    float x_ratio = static_cast<float>(src_width) / render_width;
    float y_ratio = static_cast<float>(src_height) / render_height;
    
    std::cout << "DEBUG: *** ORIENTATION DEBUG *** Starting pixel copying loop with render dimensions: " << render_width << "x" << render_height << std::endl;
    std::cout << "DEBUG: *** ORIENTATION DEBUG *** Scaling ratios - x_ratio: " << x_ratio << ", y_ratio: " << y_ratio << std::endl;
    
    // ============================================================================
    // CRITICAL Y-AXIS ORIENTATION FIX FOR WAYLAND SHM IMAGE RENDERING
    // DO NOT MODIFY: This Y-axis flip corrects upside-down images at the pixel 
    // copying level, which is the proper place for orientation fixes according 
    // to the reference implementation (Almamu/linux-wallpaperengine).
    // ============================================================================
    
    for (int y = 0; y < render_height; y++) {
        for (int x = 0; x < render_width; x++) {
            int dst_x = x + offset_x;
            int dst_y = y + offset_y;
            
            // Skip pixels outside destination bounds
            if (dst_x < 0 || dst_x >= dst_width || dst_y < 0 || dst_y >= dst_height) {
                continue;
            }
            
            int src_x = static_cast<int>(x * x_ratio);
            int src_y = static_cast<int>(y * y_ratio);
            
            // ============================================================================
            // CRITICAL Y-AXIS ORIENTATION FIX FOR WAYLAND SHM IMAGE RENDERING
            // DO NOT MODIFY: Apply Y-axis flip only for window mode (windowed_mode = true)
            // Background mode works correctly as-is, window mode needs Y-axis flip.
            // ============================================================================
            if (windowed_mode) {
                // Window mode: Apply Y-axis flip to correct upside-down images
                int original_src_y = src_y;
                src_y = src_height - 1 - src_y;
                if (y < 5 && x < 5) { // Debug first few pixels
                    std::cout << "DEBUG: *** ORIENTATION DEBUG *** Window mode Y-flip: original_src_y=" << original_src_y << " -> flipped_src_y=" << src_y << std::endl;
                }
            } else {
                // Background mode: Use normal Y coordinates (no flip needed)
                if (y < 5 && x < 5) { // Debug first few pixels
                    std::cout << "DEBUG: *** ORIENTATION DEBUG *** Background mode: using normal src_y=" << src_y << " (no flip)" << std::endl;
                }
            }
            // ============================================================================
            
            // Clamp to source bounds
            src_x = std::min(src_x, src_width - 1);
            src_y = std::max(0, std::min(src_y, src_height - 1));
            
            int dst_idx = dst_y * dst_width + dst_x;
            int src_idx = (src_y * src_width + src_x) * 4;
            
            // Convert RGBA to ARGB (Wayland SHM format WL_SHM_FORMAT_ARGB8888)
            uint32_t r = src_data[src_idx + 0];
            uint32_t g = src_data[src_idx + 1];
            uint32_t b = src_data[src_idx + 2];
            uint32_t a = src_data[src_idx + 3];
            
            dst_pixels[dst_idx] = (a << 24) | (r << 16) | (g << 8) | b;
        }
    }
    
    // ============================================================================
    // END CRITICAL Y-AXIS ORIENTATION FIX FOR WAYLAND SHM IMAGE RENDERING
    // ============================================================================
}
