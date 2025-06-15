#include "media_player.h"
#include "audio/pulse_audio.h"
#include <iostream>
#include <filesystem>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <clocale>
#include <chrono>
#include <vector>
#include <memory>
#include <iomanip>
#include <thread>

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
      audio_codec_context_(nullptr), audio_codec_(nullptr), audio_stream_index_(-1),
      decoder_initialized_(false), frame_rate_(30.0), current_time_(0.0),
      frame_duration_(1.0/30.0), target_frame_rate_(30.0), target_frame_duration_(1.0/30.0),
      last_decode_time_(0.0), video_time_(0.0), frame_rate_limiting_enabled_(false),
      frame_available_(false), volume_(100), muted_(false),
      audio_player_(nullptr), audio_frame_(nullptr), audio_playback_enabled_(false),
      audio_thread_(nullptr), audio_thread_running_(false) {}

MediaPlayer::~MediaPlayer() {
    cleanup();
}

bool MediaPlayer::initialize() {
    if (initialized_) {
        return true;
    }
    
    // Initialize FFmpeg (only needs to be done once globally)
    static bool ffmpeg_initialized = false;
    if (!ffmpeg_initialized) {
        // av_register_all() is deprecated in FFmpeg 4.0+ and not needed
        avformat_network_init();
        ffmpeg_initialized = true;
    }
    
    // Create audio player for audio playback
    audio_player_ = std::make_unique<PulseAudio>();
    if (!audio_player_->initialize()) {
        std::cerr << "Warning: Failed to initialize audio player. Audio playback will be disabled." << std::endl;
        audio_player_.reset();
    }
    
    initialized_ = true;
    return true;
}

void MediaPlayer::cleanup() {
    // Stop audio thread first
    if (audio_thread_running_) {
        audio_thread_running_ = false;
        if (audio_thread_ && audio_thread_->joinable()) {
            audio_thread_->join();
        }
        audio_thread_.reset();
    }
    
    cleanup_ffmpeg_decoder();
    free_image_data();
    
    // Clean up audio playback
    if (audio_player_) {
        audio_player_->destroy_audio_stream();
        audio_player_.reset();
    }
    
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
    return true;
}

bool MediaPlayer::pause() {
    playing_ = false;
    return true;
}

bool MediaPlayer::stop() {
    playing_ = false;
    current_time_ = 0.0;
    return true;
}

void MediaPlayer::set_volume(int volume) {
    // Clamp volume to valid range
    volume_ = std::max(0, std::min(100, volume));
    std::cout << "DEBUG: MediaPlayer volume set to " << volume_ << "%" << std::endl;
    
    // Apply to audio stream if active
    if (audio_player_ && audio_playback_enabled_) {
        audio_player_->set_playback_volume(volume_);
    }
}

void MediaPlayer::set_muted(bool muted) {
    muted_ = muted;
    std::cout << "DEBUG: MediaPlayer mute set to " << (muted_ ? "ON" : "OFF") << std::endl;
    
    // Apply to audio stream if active
    if (audio_player_ && audio_playback_enabled_) {
        audio_player_->set_playback_muted(muted_);
    }
}

void MediaPlayer::set_fps_limit(int fps) {
    if (fps > 0) {
        target_display_fps_ = static_cast<double>(fps);
        
        std::cout << "DEBUG: Display FPS set to: " << fps << " fps" << std::endl;
        std::cout << "DEBUG: Video will decode at native rate (" << frame_rate_ 
                  << " fps) and display at " << target_display_fps_ << " fps" << std::endl;
        
        if (target_display_fps_ > frame_rate_) {
            std::cout << "DEBUG: Display FPS > Video FPS - frames will be repeated for smooth display" << std::endl;
        } else if (target_display_fps_ < frame_rate_) {
            std::cout << "DEBUG: Display FPS < Video FPS - some video frames may be skipped" << std::endl;
        } else {
            std::cout << "DEBUG: Display FPS matches Video FPS - perfect sync" << std::endl;
        }
    } else {
        // fps == -1 or invalid: Use native video frame rate
        target_display_fps_ = frame_rate_;
        std::cout << "DEBUG: FPS limiting disabled, using native video FPS: " << frame_rate_ << std::endl;
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
    
    // For video playback, use timing-controlled frame extraction
    if (media_type_ == MediaType::VIDEO) {
        bool success = extract_next_frame();
        if (success && frame_buffer_) {
            *frame_data = frame_buffer_;
            return true;
        }
        return false;
    } else {
        // For images, use cached frame if available
        if (frame_buffer_) {
            *frame_data = frame_buffer_;
            return true;
        }
        
        // Extract frame for images (only once)
        bool success = extract_next_frame();
        if (success && frame_buffer_) {
            *frame_data = frame_buffer_;
            return true;
        }
        
        return false;
    }
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
    return true;
}

void MediaPlayer::update() {
    if (!initialized_) {
        return;
    }
    
    // For video playback, let the extract_next_frame() method handle timing
    // The application's display loop will control the actual display refresh rate
    // Video decoding happens at native rate, display happens at target rate
    
    // Audio processing is handled by a separate thread and runs independently
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
    
    // Find the video and audio streams
    video_stream_index_ = -1;
    audio_stream_index_ = -1;
    for (unsigned int i = 0; i < format_context_->nb_streams; i++) {
        if (format_context_->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO && video_stream_index_ == -1) {
            video_stream_index_ = i;
        } else if (format_context_->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO && audio_stream_index_ == -1) {
            audio_stream_index_ = i;
        }
    }
    
    if (video_stream_index_ == -1) {
        std::cerr << "Could not find video stream" << std::endl;
        avformat_close_input(&format_context_);
        return false;
    }
    
    // Check if audio stream was found
    if (audio_stream_index_ != -1) {
        has_audio_ = true;
        std::cout << "DEBUG: Found audio stream at index " << audio_stream_index_ << std::endl;
    } else {
        has_audio_ = false;
        std::cout << "DEBUG: No audio stream found in media" << std::endl;
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
    
    // Setup audio codec if audio stream exists
    if (has_audio_) {
        // Find audio decoder
        audio_codec_ = avcodec_find_decoder(format_context_->streams[audio_stream_index_]->codecpar->codec_id);
        if (audio_codec_) {
            // Create audio codec context
            audio_codec_context_ = avcodec_alloc_context3(audio_codec_);
            if (audio_codec_context_) {
                // Copy audio codec parameters
                if (avcodec_parameters_to_context(audio_codec_context_, format_context_->streams[audio_stream_index_]->codecpar) >= 0) {
                    // Open audio codec
                    if (avcodec_open2(audio_codec_context_, audio_codec_, nullptr) >= 0) {
                        std::cout << "DEBUG: Audio codec initialized - " 
                                  << audio_codec_context_->sample_rate << "Hz, " 
                                  << audio_codec_context_->ch_layout.nb_channels << " channels" << std::endl;
                        
                        // Create PulseAudio stream for audio playback
                        if (audio_player_) {
                            if (audio_player_->create_audio_stream(audio_codec_context_->sample_rate, 
                                                                  audio_codec_context_->ch_layout.nb_channels)) {
                                audio_playback_enabled_ = true;
                                
                                // Allocate audio frame for decoding
                                audio_frame_ = av_frame_alloc();
                                if (!audio_frame_) {
                                    std::cerr << "WARNING: Could not allocate audio frame" << std::endl;
                                    audio_player_->destroy_audio_stream();
                                    audio_playback_enabled_ = false;
                                } else {
                                    std::cout << "DEBUG: Audio stream created successfully" << std::endl;
                                    
                                    // Apply current volume and mute settings
                                    audio_player_->set_playback_volume(volume_);
                                    audio_player_->set_playback_muted(muted_);
                                    
                                    // Start audio processing thread
                                    audio_file_path_ = current_media_;
                                    audio_thread_running_ = true;
                                    audio_thread_ = std::make_unique<std::thread>(&MediaPlayer::audio_thread_function, this);
                                }
                            } else {
                                std::cerr << "WARNING: Could not create audio stream" << std::endl;
                            }
                        }
                    } else {
                        std::cerr << "WARNING: Could not open audio codec" << std::endl;
                        avcodec_free_context(&audio_codec_context_);
                        audio_codec_context_ = nullptr;
                        has_audio_ = false;
                    }
                } else {
                    std::cerr << "WARNING: Could not copy audio codec parameters" << std::endl;
                    avcodec_free_context(&audio_codec_context_);
                    audio_codec_context_ = nullptr;
                    has_audio_ = false;
                }
            } else {
                std::cerr << "WARNING: Could not allocate audio codec context" << std::endl;
                has_audio_ = false;
            }
        } else {
            std::cerr << "WARNING: Unsupported audio codec" << std::endl;
            has_audio_ = false;
        }
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
        std::cout << "DEBUG: Video native frame rate: " << frame_rate_ << " fps" << std::endl;
    } else {
        // Fallback to average frame rate if r_frame_rate is not available
        AVRational avg_frame_rate = format_context_->streams[video_stream_index_]->avg_frame_rate;
        if (avg_frame_rate.num > 0 && avg_frame_rate.den > 0) {
            frame_rate_ = (double)avg_frame_rate.num / avg_frame_rate.den;
            frame_duration_ = 1.0 / frame_rate_;
            std::cout << "DEBUG: Video average frame rate: " << frame_rate_ << " fps" << std::endl;
        } else {
            // Final fallback to 30 fps
            frame_rate_ = 30.0;
            frame_duration_ = 1.0 / 30.0;
            std::cout << "DEBUG: Could not detect frame rate, using default: " << frame_rate_ << " fps" << std::endl;
        }
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
    
    // Initialize timing variables for PTS-based playback
    video_pts_ = 0.0;
    audio_pts_ = 0.0;
    master_clock_ = 0.0;
    playback_start_time_ = 0.0;
    target_display_fps_ = target_frame_rate_; // Use target FPS for display
    last_display_time_ = 0.0;
    last_frame_pts_ = 0.0;
    has_cached_frame_ = false;
    
    std::cout << "DEBUG: Video decoder initialized with PTS timing" << std::endl;
    
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
    
    // Clean up audio codec context
    if (audio_codec_context_) {
        avcodec_free_context(&audio_codec_context_);
    }
    
    // Clean up audio frame
    if (audio_frame_) {
        av_frame_free(&audio_frame_);
    }
    
    // Destroy audio stream
    if (audio_player_ && audio_playback_enabled_) {
        audio_player_->destroy_audio_stream();
        audio_playback_enabled_ = false;
    }
    
    if (format_context_) {
        avformat_close_input(&format_context_);
    }
    
    decoder_initialized_ = false;
    video_stream_index_ = -1;
    audio_stream_index_ = -1;
}

bool MediaPlayer::load_image_ffmpeg(const std::string& image_path) {
    
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
    
    // Get current real time for playback timing
    auto now = std::chrono::high_resolution_clock::now();
    double current_real_time = std::chrono::duration<double>(now.time_since_epoch()).count();
    
    // Initialize playback start time on first frame
    if (playback_start_time_ == 0.0) {
        playback_start_time_ = current_real_time;
        video_pts_ = 0.0;
    }
    
    // FIXED: When FPS limiting is active (target_display_fps_ < frame_rate_), 
    // don't wait for "correct" timing - let the display FPS control handle timing
    bool fps_limiting_active = (target_display_fps_ < frame_rate_);
    
    AVPacket packet;
    while (av_read_frame(format_context_, &packet) >= 0) {
        if (packet.stream_index == video_stream_index_) {
            if (avcodec_send_packet(codec_context_, &packet) >= 0) {
                if (avcodec_receive_frame(codec_context_, frame_) >= 0) {
                    // Calculate frame PTS in seconds
                    AVRational time_base = format_context_->streams[video_stream_index_]->time_base;
                    double frame_pts = frame_->pts * av_q2d(time_base);
                    
                    if (!fps_limiting_active) {
                        // Only do timing control when NOT FPS limiting (i.e., running at native speed)
                        double expected_time = playback_start_time_ + frame_pts;
                        
                        // If we're ahead of schedule, wait (this maintains proper video speed)
                        if (current_real_time < expected_time) {
                            double wait_time = expected_time - current_real_time;
                            if (wait_time > 0.0 && wait_time < 0.1) { // Wait max 100ms
                                std::this_thread::sleep_for(std::chrono::duration<double>(wait_time));
                            }
                        }
                    }
                    // When FPS limiting is active, skip timing control and let should_display_frame() handle it
                    
                    // Update video clock with current frame PTS
                    video_pts_ = frame_pts;
                    
                    // Standard RGBA conversion without flipping
                    uint8_t* dst_data[4] = { rgb_frame_->data[0], nullptr, nullptr, nullptr };
                    int dst_linesize[4] = { rgb_frame_->linesize[0], 0, 0, 0 };
                    
                    sws_scale(sws_context_, frame_->data, frame_->linesize, 0, height_,
                             dst_data, dst_linesize);
                    
                    av_packet_unref(&packet);
                    has_cached_frame_ = true;
                    last_frame_pts_ = frame_pts;
                    
                    return true;
                }
            }
        }
        av_packet_unref(&packet);
    }
    
    // End of file, loop back to beginning for continuous playback
    av_seek_frame(format_context_, video_stream_index_, 0, AVSEEK_FLAG_BACKWARD);
    video_pts_ = 0.0;
    playback_start_time_ = current_real_time; // Reset timing for loop
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

bool MediaPlayer::process_audio_frame() {
    if (!has_audio_ || !audio_playback_enabled_ || !audio_player_ || !audio_codec_context_ || !audio_frame_) {
        return false;
    }
    
    // Simplified audio processing - don't try to read packets directly
    // Instead, rely on the existing packet reading mechanism and only process
    // audio when we encounter audio packets during video processing
    
    // For now, just return false to avoid deadlocks
    // TODO: Implement proper audio/video synchronization
    return false;
}

void MediaPlayer::audio_thread_function() {
    std::cout << "DEBUG: Audio thread started" << std::endl;
    
    // Create separate FFmpeg context for audio processing
    AVFormatContext* audio_format_context = nullptr;
    AVCodecContext* audio_codec_context = nullptr;
    const AVCodec* audio_codec = nullptr;
    AVFrame* audio_frame = nullptr;
    int audio_stream_index = -1;
    
    // Open input file for audio
    if (avformat_open_input(&audio_format_context, audio_file_path_.c_str(), nullptr, nullptr) < 0) {
        std::cerr << "ERROR: Audio thread could not open file: " << audio_file_path_ << std::endl;
        return;
    }
    
    // Find stream information
    if (avformat_find_stream_info(audio_format_context, nullptr) < 0) {
        std::cerr << "ERROR: Audio thread could not find stream information" << std::endl;
        avformat_close_input(&audio_format_context);
        return;
    }
    
    // Find audio stream
    for (unsigned int i = 0; i < audio_format_context->nb_streams; i++) {
        if (audio_format_context->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            audio_stream_index = i;
            break;
        }
    }
    
    if (audio_stream_index == -1) {
        std::cerr << "ERROR: Audio thread could not find audio stream" << std::endl;
        avformat_close_input(&audio_format_context);
        return;
    }
    
    // Find decoder
    audio_codec = avcodec_find_decoder(audio_format_context->streams[audio_stream_index]->codecpar->codec_id);
    if (!audio_codec) {
        std::cerr << "ERROR: Audio thread unsupported audio codec" << std::endl;
        avformat_close_input(&audio_format_context);
        return;
    }
    
    // Create codec context
    audio_codec_context = avcodec_alloc_context3(audio_codec);
    if (!audio_codec_context) {
        std::cerr << "ERROR: Audio thread could not allocate codec context" << std::endl;
        avformat_close_input(&audio_format_context);
        return;
    }
    
    // Copy codec parameters
    if (avcodec_parameters_to_context(audio_codec_context, audio_format_context->streams[audio_stream_index]->codecpar) < 0) {
        std::cerr << "ERROR: Audio thread could not copy codec parameters" << std::endl;
        avcodec_free_context(&audio_codec_context);
        avformat_close_input(&audio_format_context);
        return;
    }
    
    // Open codec
    if (avcodec_open2(audio_codec_context, audio_codec, nullptr) < 0) {
        std::cerr << "ERROR: Audio thread could not open codec" << std::endl;
        avcodec_free_context(&audio_codec_context);
        avformat_close_input(&audio_format_context);
        return;
    }
    
    // Allocate frame
    audio_frame = av_frame_alloc();
    if (!audio_frame) {
        std::cerr << "ERROR: Audio thread could not allocate frame" << std::endl;
        avcodec_free_context(&audio_codec_context);
        avformat_close_input(&audio_format_context);
        return;
    }
    
    std::cout << "DEBUG: Audio thread initialized successfully" << std::endl;
    
    // Audio processing loop
    AVPacket* packet = av_packet_alloc();
    while (audio_thread_running_ && packet) {
        // Check if playback is active and not muted
        if (!playing_ || muted_) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }
        
        // Read audio packet
        if (av_read_frame(audio_format_context, packet) < 0) {
            // End of file, seek to beginning for loop playback
            av_seek_frame(audio_format_context, audio_stream_index, 0, AVSEEK_FLAG_BACKWARD);
            continue;
        }
        
        // Process only audio packets
        if (packet->stream_index == audio_stream_index) {
            // Send packet to decoder
            if (avcodec_send_packet(audio_codec_context, packet) >= 0) {
                // Receive decoded frame
                if (avcodec_receive_frame(audio_codec_context, audio_frame) >= 0) {
                    // Convert and send audio data to PulseAudio
                    process_audio_frame_data(audio_frame, audio_codec_context);
                }
            }
        }
        
        av_packet_unref(packet);
        
        // Small delay to prevent overwhelming the audio system
        std::this_thread::sleep_for(std::chrono::microseconds(100));
    }
    
    // Cleanup
    if (packet) {
        av_packet_free(&packet);
    }
    if (audio_frame) {
        av_frame_free(&audio_frame);
    }
    if (audio_codec_context) {
        avcodec_free_context(&audio_codec_context);
    }
    if (audio_format_context) {
        avformat_close_input(&audio_format_context);
    }
    
    std::cout << "DEBUG: Audio thread ended" << std::endl;
}

void MediaPlayer::process_audio_frame_data(AVFrame* frame, AVCodecContext* codec_ctx) {
    if (!frame || !codec_ctx || !audio_player_) {
        return;
    }
    
    // Get audio frame properties
    int samples_per_channel = frame->nb_samples;
    int channels = codec_ctx->ch_layout.nb_channels;
    
    // Calculate output buffer size (S16LE = 2 bytes per sample)
    size_t output_size = samples_per_channel * channels * 2;
    
    // Create output buffer
    std::vector<uint8_t> output_buffer(output_size);
    
    // Convert audio frame to the format expected by PulseAudio (S16LE)
    if (frame->format == AV_SAMPLE_FMT_S16) {
        // Already in the right format, just copy
        memcpy(output_buffer.data(), frame->data[0], output_size);
    } else if (frame->format == AV_SAMPLE_FMT_FLTP) {
        // Convert from planar float to interleaved S16LE
        float** planes = reinterpret_cast<float**>(frame->data);
        int16_t* output = reinterpret_cast<int16_t*>(output_buffer.data());
        
        for (int sample = 0; sample < samples_per_channel; sample++) {
            for (int ch = 0; ch < channels; ch++) {
                float sample_val = planes[ch][sample];
                // Clamp and convert to S16
                sample_val = std::max(-1.0f, std::min(1.0f, sample_val));
                output[sample * channels + ch] = static_cast<int16_t>(sample_val * 32767.0f);
            }
        }
    } else if (frame->format == AV_SAMPLE_FMT_S16P) {
        // Convert from planar S16 to interleaved S16LE
        int16_t** planes = reinterpret_cast<int16_t**>(frame->data);
        int16_t* output = reinterpret_cast<int16_t*>(output_buffer.data());
        
        for (int sample = 0; sample < samples_per_channel; sample++) {
            for (int ch = 0; ch < channels; ch++) {
                output[sample * channels + ch] = planes[ch][sample];
            }
        }
    } else {
        // Unsupported format, fill with silence
        memset(output_buffer.data(), 0, output_size);
        std::cerr << "WARNING: Unsupported audio format " << frame->format 
                  << ", outputting silence" << std::endl;
    }
    
    // Send audio data to PulseAudio
    if (!audio_player_->write_audio_data(output_buffer.data(), output_size)) {
        // If write fails, don't spam errors, just continue
        static int error_count = 0;
        if (error_count < 5) {
            std::cerr << "WARNING: Failed to write audio data" << std::endl;
            error_count++;
        }
    }
}

bool MediaPlayer::should_display_frame() {
    // FIXED: Use steady_clock for more accurate timing and prevent drift
    static auto last_display_time = std::chrono::steady_clock::now();
    auto now = std::chrono::steady_clock::now();
    
    // Calculate time since last display
    auto time_since_last_display = std::chrono::duration_cast<std::chrono::microseconds>(now - last_display_time);
    
    // Calculate target frame interval based on display FPS (in microseconds for precision)
    auto target_interval = std::chrono::microseconds(static_cast<int64_t>(1000000.0 / target_display_fps_));
    
    // If enough time has passed according to display FPS, we should display
    if (time_since_last_display >= target_interval) {
        last_display_time = now;
        return true;
    }
    
    return false;
}
