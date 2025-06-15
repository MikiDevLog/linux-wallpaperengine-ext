#pragma once

#include <string>
#include <memory>
#include <thread>
#include <atomic>

// Forward declarations for FFmpeg
extern "C" {
struct AVFormatContext;
struct AVCodecContext;
struct AVCodec;
struct AVFrame;
struct SwsContext;
struct AVRational;
}

// Forward declaration for PulseAudio
class PulseAudio;

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
    
    void set_volume(int volume);    // 0-100 audio volume control
    void set_muted(bool muted);     // Audio mute control  
    void set_fps_limit(int fps);    // Frame rate limiting for video playback
    
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

    // Display timing control
    bool should_display_frame(); // Check if it's time to display current frame based on display FPS

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
    
    // Audio decoding context
    AVCodecContext* audio_codec_context_;
    const AVCodec* audio_codec_;
    int audio_stream_index_;
    
    // Video timing and playback control
    bool decoder_initialized_;
    double frame_rate_;              // Video's native frame rate from stream
    double current_time_;
    double frame_duration_;          // Video's native frame duration
    
    // FPS limiting variables (from old system - keeping for compatibility)
    double target_frame_rate_;       // User-requested frame rate limit
    double target_frame_duration_;   // Duration for user-requested FPS
    double last_decode_time_;        // Last time we decoded a frame
    double video_time_;              // Current video time
    bool frame_rate_limiting_enabled_; // Whether FPS limiting is active
    bool frame_available_;           // Whether a frame is available for display
    
    // PTS-based timing for proper sync
    double video_pts_;               // Current video presentation timestamp
    double audio_pts_;               // Current audio presentation timestamp  
    double master_clock_;            // Master clock for sync (usually audio)
    double playback_start_time_;     // When playback started (real time)
    double last_frame_pts_;          // PTS of last decoded frame
    
    // Display control (separate from decode timing)
    double target_display_fps_;      // User-requested display refresh rate
    double last_display_time_;       // When we last displayed a frame
    bool has_cached_frame_;          // Whether we have a decoded frame ready
    
    // Audio control
    int volume_;        // 0-100
    bool muted_;
    
    // Audio playback
    std::unique_ptr<PulseAudio> audio_player_;
    AVFrame* audio_frame_;          // Frame for audio decoding
    bool audio_playback_enabled_;   // Whether audio playback is active
    
    // Audio threading
    std::unique_ptr<std::thread> audio_thread_;
    std::atomic<bool> audio_thread_running_;
    std::string audio_file_path_;   // Separate path for audio thread
    
    // Private methods
    bool setup_ffmpeg_decoder();
    void cleanup_ffmpeg_decoder();
    bool load_image_ffmpeg(const std::string& image_path);
    bool load_video_ffmpeg(const std::string& video_path);
    void free_image_data();
    bool extract_next_frame();       // Extract next frame from video stream
    bool decode_next_video_frame();  // Decode next frame when needed based on PTS
    bool process_audio_frame();  // Process and output audio frames
    void audio_thread_function(); // Audio processing thread function
    void process_audio_frame_data(AVFrame* frame, AVCodecContext* codec_ctx); // Helper for audio conversion
    double get_master_clock();   // Get master clock time for sync
    double get_video_clock();    // Get video clock time
    
    // Media file validation
    bool is_supported_format(const std::string& file_path);
};
