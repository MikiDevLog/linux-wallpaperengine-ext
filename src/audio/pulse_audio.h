#pragma once

#include <pulse/pulseaudio.h>
#include <memory>
#include <string>
#include <queue>
#include <mutex>

class PulseAudio {
public:
    PulseAudio();
    ~PulseAudio();
    
    bool initialize();
    void cleanup();
    
    bool is_any_application_playing_audio();
    void set_auto_mute_enabled(bool enabled);
    bool should_mute_background_audio();
    
    // Audio playback functionality
    bool create_audio_stream(int sample_rate, int channels);
    void destroy_audio_stream();
    bool write_audio_data(const uint8_t* data, size_t size);
    void set_playback_volume(int volume);  // 0-100
    void set_playback_muted(bool muted);
    bool is_audio_stream_active() const;

private:
    // Monitoring functionality
    pa_threaded_mainloop* mainloop_;
    pa_context* context_;
    bool initialized_;
    bool auto_mute_enabled_;
    bool other_app_playing_;
    
    // Audio playback functionality
    pa_stream* audio_stream_;
    pa_sample_spec audio_spec_;
    bool audio_stream_ready_;
    int playback_volume_;
    bool playback_muted_;
    
    // Audio buffer for thread-safe audio data handling
    std::queue<std::vector<uint8_t>> audio_buffer_;
    std::mutex audio_buffer_mutex_;
    
    static void context_state_callback(pa_context* context, void* userdata);
    static void sink_input_list_callback(pa_context* context, const pa_sink_input_info* info, 
                                        int eol, void* userdata);
    static void stream_state_callback(pa_stream* stream, void* userdata);
    static void stream_write_callback(pa_stream* stream, size_t nbytes, void* userdata);
    
    void update_playback_status();
    void process_audio_buffer();
};
