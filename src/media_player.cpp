#include "media_player.h"
#include <iostream>
#include <filesystem>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <clocale>
#include <chrono>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

MediaPlayer::MediaPlayer() 
    : initialized_(false), playing_(false), 
      width_(0), height_(0), has_video_(false), has_audio_(false),
      media_type_(MediaType::UNKNOWN), image_data_(nullptr),
      format_context_(nullptr), codec_context_(nullptr), codec_(nullptr),
      frame_(nullptr), rgb_frame_(nullptr), sws_context_(nullptr),
      video_stream_index_(-1), frame_buffer_(nullptr),
      decoder_initialized_(false), frame_rate_(30.0), current_time_(0.0),
      frame_duration_(1.0/30.0) {}

MediaPlayer::~MediaPlayer() {
    cleanup();
}

bool MediaPlayer::initialize() {
    if (initialized_) {
        return true;
    }
    
    std::cout << "DEBUG: Initializing MediaPlayer with pure FFmpeg backend" << std::endl;
    
    // Initialize FFmpeg (only needs to be done once globally)
    static bool ffmpeg_initialized = false;
    if (!ffmpeg_initialized) {
        // av_register_all() is deprecated in FFmpeg 4.0+ and not needed
        avformat_network_init();
        ffmpeg_initialized = true;
    }
    
    // Use CPU-only video rendering for all video playback
    std::cout << "DEBUG: Using CPU-only video rendering" << std::endl;
    
    initialized_ = true;
    std::cout << "DEBUG: MediaPlayer initialized successfully" << std::endl;
    return true;
}

void MediaPlayer::cleanup() {
    cleanup_ffmpeg_decoder();
    free_image_data();
    initialized_ = false;
    playing_ = false;
}

bool MediaPlayer::load_media(const std::string& media_path) {
    if (!initialized_) {
        std::cerr << "MediaPlayer not initialized" << std::endl;
        return false;
    }
    
    if (!std::filesystem::exists(media_path)) {
        std::cerr << "Media file does not exist: " << media_path << std::endl;
        return false;
    }
    
    std::cout << "DEBUG: Loading media: " << media_path << std::endl;
    
    // Clean up previous media
    cleanup_ffmpeg_decoder();
    free_image_data();
    
    current_media_ = media_path;
    media_type_ = detect_media_type(media_path);
    
    switch (media_type_) {
        case MediaType::IMAGE:
        case MediaType::GIF:
            return load_image_ffmpeg(media_path);
        case MediaType::VIDEO:
            return load_video_ffmpeg(media_path);
        default:
            std::cerr << "Unsupported media type: " << media_path << std::endl;
            return false;
    }
}

bool MediaPlayer::play() {
    if (!initialized_ || !has_video_) {
        return false;
    }
    
    playing_ = true;
    std::cout << "DEBUG: Starting video playback" << std::endl;
    return true;
}

bool MediaPlayer::pause() {
    playing_ = false;
    std::cout << "DEBUG: Video playback paused" << std::endl;
    return true;
}

bool MediaPlayer::stop() {
    playing_ = false;
    current_time_ = 0.0;
    std::cout << "DEBUG: Video playback stopped" << std::endl;
    return true;
}

void MediaPlayer::set_volume(int volume) {
    // Future implementation for audio integration
    std::cout << "DEBUG: Volume set to " << volume << " (not implemented yet)" << std::endl;
}

void MediaPlayer::set_muted(bool muted) {
    // Future implementation for audio integration
    std::cout << "DEBUG: Muted set to " << (muted ? "true" : "false") << " (not implemented yet)" << std::endl;
}

void MediaPlayer::set_fps_limit(int fps) {
    if (fps > 0) {
        frame_rate_ = static_cast<double>(fps);
        frame_duration_ = 1.0 / frame_rate_;
        std::cout << "DEBUG: FPS limit set to " << fps << std::endl;
    }
}

bool MediaPlayer::is_playing() const {
    return playing_;
}

bool MediaPlayer::is_video() const {
    return has_video_;
}

bool MediaPlayer::is_audio_enabled() const {
    return has_audio_;
}

MediaType MediaPlayer::get_media_type() const {
    return media_type_;
}

int MediaPlayer::get_width() const {
    return width_;
}

int MediaPlayer::get_height() const {
    return height_;
}

const unsigned char* MediaPlayer::get_image_data() const {
    return image_data_;
}

bool MediaPlayer::get_video_frame_ffmpeg(unsigned char** frame_data, int* width, int* height) {
    if (!initialized_ || !has_video_ || !decoder_initialized_) {
        std::cerr << "MediaPlayer not ready for frame extraction" << std::endl;
        return false;
    }
    
    *width = width_;
    *height = height_;
    
    // If we have a current frame, return it
    if (frame_buffer_) {
        *frame_data = frame_buffer_;
        return true;
    }
    
    // Extract next frame
    return extract_next_frame() && (*frame_data = frame_buffer_) != nullptr;
}

bool MediaPlayer::get_video_frame_cpu(unsigned char** frame_data, int* width, int* height) {
    // CPU-only video frame extraction - alias for FFmpeg method
    return get_video_frame_ffmpeg(frame_data, width, height);
}

bool MediaPlayer::get_video_frame(unsigned char** frame_data, int* width, int* height) {
    // Primary video frame extraction method - uses CPU-only FFmpeg rendering
    return get_video_frame_ffmpeg(frame_data, width, height);
}

bool MediaPlayer::set_x11_window(void* display, unsigned long window, int screen) {
    // Store X11 window context for potential video rendering integration
    // For now, this is a no-op since we're using FFmpeg CPU rendering
    std::cout << "DEBUG: X11 window context set for MediaPlayer" << std::endl;
    return true;
}

void MediaPlayer::update() {
    if (!initialized_) {
        return;
    }
    
    // For video playback, advance frame timing
    if (has_video_ && playing_) {
        static auto last_time = std::chrono::high_resolution_clock::now();
        auto current_time = std::chrono::high_resolution_clock::now();
        auto elapsed = std::chrono::duration<double>(current_time - last_time).count();
        
        if (elapsed >= frame_duration_) {
            current_time_ += elapsed;
            extract_next_frame();
            last_time = current_time;
        }
    }
}

MediaType MediaPlayer::detect_media_type(const std::string& file_path) {
    std::string extension = std::filesystem::path(file_path).extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(), ::tolower);
    
    if (extension == ".jpg" || extension == ".jpeg" || extension == ".png" || 
        extension == ".bmp" || extension == ".tiff" || extension == ".webp") {
        return MediaType::IMAGE;
    } else if (extension == ".gif") {
        MediaType::GIF;
    } else if (extension == ".mp4" || extension == ".avi" || extension == ".mkv" || 
               extension == ".mov" || extension == ".webm" || extension == ".flv") {
        return MediaType::VIDEO;
    }
    
    return MediaType::UNKNOWN;
}

// Private methods

bool MediaPlayer::setup_ffmpeg_decoder() {
    if (decoder_initialized_) {
        return true;
    }
    
    if (current_media_.empty()) {
        std::cerr << "No media file loaded" << std::endl;
        return false;
    }
    
    std::cout << "DEBUG: Setting up FFmpeg decoder for: " << current_media_ << std::endl;
    
    // Open input file
    if (avformat_open_input(&format_context_, current_media_.c_str(), nullptr, nullptr) < 0) {
        std::cerr << "Could not open video file: " << current_media_ << std::endl;
        return false;
    }
    
    // Retrieve stream information
    if (avformat_find_stream_info(format_context_, nullptr) < 0) {
        std::cerr << "Could not find stream information" << std::endl;
        avformat_close_input(&format_context_);
        return false;
    }
    
    // Find the video stream
    video_stream_index_ = -1;
    for (unsigned int i = 0; i < format_context_->nb_streams; i++) {
        if (format_context_->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            video_stream_index_ = i;
            break;
        }
    }
    
    if (video_stream_index_ == -1) {
        std::cerr << "Could not find video stream" << std::endl;
        avformat_close_input(&format_context_);
        return false;
    }
    
    // Find decoder
    codec_ = avcodec_find_decoder(format_context_->streams[video_stream_index_]->codecpar->codec_id);
    if (!codec_) {
        std::cerr << "Unsupported codec" << std::endl;
        avformat_close_input(&format_context_);
        return false;
    }
    
    // Allocate codec context
    codec_context_ = avcodec_alloc_context3(codec_);
    if (!codec_context_) {
        std::cerr << "Could not allocate codec context" << std::endl;
        avformat_close_input(&format_context_);
        return false;
    }
    
    // Copy codec parameters
    if (avcodec_parameters_to_context(codec_context_, format_context_->streams[video_stream_index_]->codecpar) < 0) {
        std::cerr << "Could not copy codec parameters" << std::endl;
        avcodec_free_context(&codec_context_);
        avformat_close_input(&format_context_);
        return false;
    }
    
    // Open codec
    if (avcodec_open2(codec_context_, codec_, nullptr) < 0) {
        std::cerr << "Could not open codec" << std::endl;
        avcodec_free_context(&codec_context_);
        avformat_close_input(&format_context_);
        return false;
    }
    
    // Allocate frames
    frame_ = av_frame_alloc();
    rgb_frame_ = av_frame_alloc();
    if (!frame_ || !rgb_frame_) {
        std::cerr << "Could not allocate frames" << std::endl;
        cleanup_ffmpeg_decoder();
        return false;
    }
    
    // Set video dimensions
    width_ = codec_context_->width;
    height_ = codec_context_->height;
    has_video_ = true;
    
    // Calculate frame rate
    AVRational frame_rate = format_context_->streams[video_stream_index_]->r_frame_rate;
    if (frame_rate.num > 0 && frame_rate.den > 0) {
        frame_rate_ = (double)frame_rate.num / frame_rate.den;
        frame_duration_ = 1.0 / frame_rate_;
    }
    
    // Allocate buffer for RGB frame
    int buffer_size = av_image_get_buffer_size(AV_PIX_FMT_RGBA, width_, height_, 1);
    frame_buffer_ = (unsigned char*)av_malloc(buffer_size);
    if (!frame_buffer_) {
        std::cerr << "Could not allocate frame buffer" << std::endl;
        cleanup_ffmpeg_decoder();
        return false;
    }
    
    av_image_fill_arrays(rgb_frame_->data, rgb_frame_->linesize, frame_buffer_, 
                        AV_PIX_FMT_RGBA, width_, height_, 1);
    
    // Create scaling context with vertical flip to fix coordinate system differences
    sws_context_ = sws_getContext(width_, height_, codec_context_->pix_fmt,
                                 width_, height_, AV_PIX_FMT_RGBA,
                                 SWS_BILINEAR, nullptr, nullptr, nullptr);
    if (!sws_context_) {
        std::cerr << "Could not create scaling context" << std::endl;
        cleanup_ffmpeg_decoder();
        return false;
    }
    
    decoder_initialized_ = true;
    std::cout << "DEBUG: FFmpeg decoder setup complete for " << width_ << "x" << height_ << " @ " << frame_rate_ << " FPS" << std::endl;
    return true;
}

void MediaPlayer::cleanup_ffmpeg_decoder() {
    if (sws_context_) {
        sws_freeContext(sws_context_);
        sws_context_ = nullptr;
    }
    
    if (frame_buffer_) {
        av_free(frame_buffer_);
        frame_buffer_ = nullptr;
    }
    
    if (rgb_frame_) {
        av_frame_free(&rgb_frame_);
    }
    
    if (frame_) {
        av_frame_free(&frame_);
    }
    
    if (codec_context_) {
        avcodec_free_context(&codec_context_);
    }
    
    if (format_context_) {
        avformat_close_input(&format_context_);
    }
    
    decoder_initialized_ = false;
    video_stream_index_ = -1;
}

bool MediaPlayer::load_image_ffmpeg(const std::string& image_path) {
    std::cout << "DEBUG: Loading image with FFmpeg: " << image_path << std::endl;
    
    AVFormatContext* format_ctx = nullptr;
    AVCodecContext* codec_ctx = nullptr;
    AVFrame* frame = nullptr;
    AVFrame* rgb_frame = nullptr;
    SwsContext* sws_ctx = nullptr;
    const AVCodec* codec = nullptr;
    int video_stream_index = -1;
    
    // Open input file
    if (avformat_open_input(&format_ctx, image_path.c_str(), nullptr, nullptr) < 0) {
        std::cerr << "Could not open image file: " << image_path << std::endl;
        return false;
    }
    
    if (avformat_find_stream_info(format_ctx, nullptr) < 0) {
        std::cerr << "Could not find stream information" << std::endl;
        goto cleanup;
    }
    
    // Find video stream (images are treated as single-frame videos)
    for (unsigned int i = 0; i < format_ctx->nb_streams; i++) {
        if (format_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            video_stream_index = i;
            break;
        }
    }
    
    if (video_stream_index == -1) {
        std::cerr << "Could not find video stream in image" << std::endl;
        goto cleanup;
    }
    
    codec = avcodec_find_decoder(format_ctx->streams[video_stream_index]->codecpar->codec_id);
    if (!codec) {
        std::cerr << "Could not find codec for image" << std::endl;
        goto cleanup;
    }
    
    codec_ctx = avcodec_alloc_context3(codec);
    if (!codec_ctx) {
        std::cerr << "Could not allocate codec context" << std::endl;
        goto cleanup;
    }
    
    if (avcodec_parameters_to_context(codec_ctx, format_ctx->streams[video_stream_index]->codecpar) < 0) {
        std::cerr << "Could not copy codec parameters" << std::endl;
        goto cleanup;
    }
    
    if (avcodec_open2(codec_ctx, codec, nullptr) < 0) {
        std::cerr << "Could not open codec" << std::endl;
        goto cleanup;
    }
    
    frame = av_frame_alloc();
    rgb_frame = av_frame_alloc();
    if (!frame || !rgb_frame) {
        std::cerr << "Could not allocate frames" << std::endl;
        goto cleanup;
    }
    
    width_ = codec_ctx->width;
    height_ = codec_ctx->height;
    has_video_ = false; // It's a static image
    
    // Allocate image buffer - declare variable at the beginning
    int rgb_buffer_size;
    rgb_buffer_size = av_image_get_buffer_size(AV_PIX_FMT_RGBA, width_, height_, 1);
    free_image_data();
    image_data_ = (unsigned char*)av_malloc(rgb_buffer_size);
    if (!image_data_) {
        std::cerr << "Could not allocate image buffer" << std::endl;
        goto cleanup;
    }
    
    av_image_fill_arrays(rgb_frame->data, rgb_frame->linesize, image_data_, AV_PIX_FMT_RGBA, width_, height_, 1);
    
    sws_ctx = sws_getContext(width_, height_, codec_ctx->pix_fmt,
                           width_, height_, AV_PIX_FMT_RGBA,
                           SWS_BILINEAR, nullptr, nullptr, nullptr);
    if (!sws_ctx) {
        std::cerr << "Could not create scaling context" << std::endl;
        goto cleanup;
    }
    
    // Read and decode the image
    AVPacket packet;
    if (av_read_frame(format_ctx, &packet) >= 0) {
        if (packet.stream_index == video_stream_index) {
            if (avcodec_send_packet(codec_ctx, &packet) >= 0) {
                if (avcodec_receive_frame(codec_ctx, frame) >= 0) {
                    // Standard RGBA conversion without flipping
                    // Let the display backends handle orientation as needed
                    sws_scale(sws_ctx, frame->data, frame->linesize, 0, height_,
                             rgb_frame->data, rgb_frame->linesize);
                    
                    std::cout << "DEBUG: Image loaded successfully: " << width_ << "x" << height_ << std::endl;
                    av_packet_unref(&packet);
                    
                    // Cleanup temporary resources
                    sws_freeContext(sws_ctx);
                    av_frame_free(&frame);
                    av_frame_free(&rgb_frame);
                    avcodec_free_context(&codec_ctx);
                    avformat_close_input(&format_ctx);
                    return true;
                }
            }
        }
        av_packet_unref(&packet);
    }
    
cleanup:
    if (sws_ctx) sws_freeContext(sws_ctx);
    if (frame) av_frame_free(&frame);
    if (rgb_frame) av_frame_free(&rgb_frame);
    if (codec_ctx) avcodec_free_context(&codec_ctx);
    if (format_ctx) avformat_close_input(&format_ctx);
    free_image_data();
    return false;
}

bool MediaPlayer::load_video_ffmpeg(const std::string& video_path) {
    std::cout << "DEBUG: Loading video with FFmpeg: " << video_path << std::endl;
    return setup_ffmpeg_decoder();
}

void MediaPlayer::free_image_data() {
    if (image_data_) {
        av_free(image_data_);
        image_data_ = nullptr;
    }
}

bool MediaPlayer::extract_next_frame() {
    if (!decoder_initialized_) {
        return false;
    }
    
    AVPacket packet;
    while (av_read_frame(format_context_, &packet) >= 0) {
        if (packet.stream_index == video_stream_index_) {
            if (avcodec_send_packet(codec_context_, &packet) >= 0) {
                if (avcodec_receive_frame(codec_context_, frame_) >= 0) {
                    // Standard RGBA conversion without flipping
                    // Let the display backends handle orientation as needed
                    uint8_t* dst_data[4] = { rgb_frame_->data[0], nullptr, nullptr, nullptr };
                    int dst_linesize[4] = { rgb_frame_->linesize[0], 0, 0, 0 };
                    
                    sws_scale(sws_context_, frame_->data, frame_->linesize, 0, height_,
                             dst_data, dst_linesize);
                    // ============================================================================
                    // END CRITICAL Y-AXIS ORIENTATION FIX
                    // ============================================================================
                    
                    av_packet_unref(&packet);
                    return true;
                }
            }
        }
        av_packet_unref(&packet);
    }
    
    // End of file, loop back to beginning for continuous playback
    av_seek_frame(format_context_, video_stream_index_, 0, AVSEEK_FLAG_BACKWARD);
    current_time_ = 0.0;
    return extract_next_frame();
}

bool MediaPlayer::is_supported_format(const std::string& file_path) {
    MediaType type = detect_media_type(file_path);
    return type != MediaType::UNKNOWN;
}

bool MediaPlayer::get_video_frame_native(AVFrame** frame) {
    if (!has_video_ || !initialized_ || !decoder_initialized_) {
        return false;
    }
    
    if (!frame) {
        return false;
    }
    
    // Extract the next frame using FFmpeg directly
    // This avoids the RGB conversion that happens in get_video_frame_ffmpeg
    
    AVPacket* packet = av_packet_alloc();
    if (!packet) {
        return false;
    }
    
    while (av_read_frame(format_context_, packet) >= 0) {
        if (packet->stream_index == video_stream_index_) {
            // Send packet to decoder
            int ret = avcodec_send_packet(codec_context_, packet);
            if (ret < 0) {
                av_packet_unref(packet);
                continue;
            }
            
            // Receive decoded frame
            ret = avcodec_receive_frame(codec_context_, frame_);
            if (ret == 0) {
                // Successfully decoded frame
                *frame = frame_;
                av_packet_free(&packet);
                
                // Update timing
                current_time_ += frame_duration_;
                
                std::cout << "DEBUG: Native frame extracted " << frame_->width << "x" << frame_->height 
                          << " format=" << frame_->format << std::endl;
                
                return true;
            }
        }
        av_packet_unref(packet);
    }
    
    // End of stream or error
    av_packet_free(&packet);
    
    // End of stream or error
    return false;
}
