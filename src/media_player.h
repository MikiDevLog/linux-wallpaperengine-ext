#pragma once

#include <string>
#include <memory>

// Forward declarations for FFmpeg
extern "C" {
struct AVFormatContext;
struct AVCodecContext;
struct AVCodec;
struct AVFrame;
struct SwsContext;
}

enum class MediaType {
    VIDEO,
    IMAGE,
    GIF,
    UNKNOWN
};

class MediaPlayer {
public:
    MediaPlayer();
    ~MediaPlayer();
    
    bool initialize();
    void cleanup();
    
    bool load_media(const std::string& media_path);
    bool play();
    bool pause();
    bool stop();
    
    void set_volume(int volume); // 0-100 (for future audio integration)
    void set_muted(bool muted);
    void set_fps_limit(int fps);
    
    bool is_playing() const;
    bool is_video() const;
    bool is_audio_enabled() const;
    MediaType get_media_type() const;
    
    // Get video dimensions
    int get_width() const;
    int get_height() const;
    
    // Get image data for rendering (static images)
    const unsigned char* get_image_data() const;
    
    // Get current video frame data using pure FFmpeg (CPU rendering)
    bool get_video_frame_ffmpeg(unsigned char** frame_data, int* width, int* height);
    
    // Get video frame using CPU-only decoding (alias for FFmpeg method)  
    bool get_video_frame_cpu(unsigned char** frame_data, int* width, int* height);
    
    // Get video frame data (primary method - uses best available method)
    bool get_video_frame(unsigned char** frame_data, int* width, int* height);
    
    // Set X11 window for video rendering context
    bool set_x11_window(void* display, unsigned long window, int screen);
    
    void update();
    
    // Media type detection
    MediaType detect_media_type(const std::string& file_path);

    // Enhanced method to get AVFrame directly (for hardware acceleration)
    bool get_video_frame_native(AVFrame** frame);

    // Get FFmpeg context for sharing with renderers
    AVFormatContext* get_format_context() const { return format_context_; }
    AVCodecContext* get_codec_context() const { return codec_context_; }

private:
    bool initialized_;
    bool playing_;
    std::string current_media_;
    MediaType media_type_;
    
    // Video properties
    int width_;
    int height_;
    bool has_video_;
    bool has_audio_;
    
    // Image data for static images
    unsigned char* image_data_;
    
    // FFmpeg context for pure video/image decoding
    AVFormatContext* format_context_;
    AVCodecContext* codec_context_;
    const AVCodec* codec_;
    AVFrame* frame_;
    AVFrame* rgb_frame_;
    SwsContext* sws_context_;
    int video_stream_index_;
    unsigned char* frame_buffer_;
    
    // Video timing
    bool decoder_initialized_;
    double frame_rate_;
    double current_time_;
    double frame_duration_;
    
    // Private methods
    bool setup_ffmpeg_decoder();
    void cleanup_ffmpeg_decoder();
    bool load_image_ffmpeg(const std::string& image_path);
    bool load_video_ffmpeg(const std::string& video_path);
    void free_image_data();
    bool extract_next_frame();
    
    // Media file validation
    bool is_supported_format(const std::string& file_path);
};
