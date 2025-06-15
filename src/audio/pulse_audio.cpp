#include "pulse_audio.h"
#include <iostream>
#include <cstring>
#include <algorithm>

PulseAudio::PulseAudio() 
    : mainloop_(nullptr), context_(nullptr), initialized_(false), 
      auto_mute_enabled_(true), other_app_playing_(false),
      audio_stream_(nullptr), audio_stream_ready_(false),
      playback_volume_(100), playback_muted_(false) {
    
    // Initialize audio spec with default values
    audio_spec_.format = PA_SAMPLE_S16LE;
    audio_spec_.rate = 44100;
    audio_spec_.channels = 2;
}

PulseAudio::~PulseAudio() {
    cleanup();
}

bool PulseAudio::initialize() {
    if (initialized_) {
        return true;
    }
    
    // Create main loop
    mainloop_ = pa_threaded_mainloop_new();
    if (!mainloop_) {
        std::cerr << "Failed to create PulseAudio main loop" << std::endl;
        return false;
    }
    
    // Get main loop API
    pa_mainloop_api* api = pa_threaded_mainloop_get_api(mainloop_);
    if (!api) {
        std::cerr << "Failed to get PulseAudio main loop API" << std::endl;
        cleanup();
        return false;
    }
    
    // Create context
    context_ = pa_context_new(api, "linux-wallpaperengine-ext");
    if (!context_) {
        std::cerr << "Failed to create PulseAudio context" << std::endl;
        cleanup();
        return false;
    }
    
    // Set state callback
    pa_context_set_state_callback(context_, context_state_callback, this);
    
    // Start main loop
    if (pa_threaded_mainloop_start(mainloop_) < 0) {
        std::cerr << "Failed to start PulseAudio main loop" << std::endl;
        cleanup();
        return false;
    }
    
    // Connect to server
    pa_threaded_mainloop_lock(mainloop_);
    
    if (pa_context_connect(context_, nullptr, PA_CONTEXT_NOFLAGS, nullptr) < 0) {
        std::cerr << "Failed to connect to PulseAudio server" << std::endl;
        pa_threaded_mainloop_unlock(mainloop_);
        cleanup();
        return false;
    }
    
    // Wait for connection
    while (pa_context_get_state(context_) != PA_CONTEXT_READY) {
        if (pa_context_get_state(context_) == PA_CONTEXT_FAILED) {
            std::cerr << "PulseAudio context connection failed" << std::endl;
            pa_threaded_mainloop_unlock(mainloop_);
            cleanup();
            return false;
        }
        pa_threaded_mainloop_wait(mainloop_);
    }
    
    pa_threaded_mainloop_unlock(mainloop_);
    
    initialized_ = true;
    std::cout << "PulseAudio initialized successfully" << std::endl;
    return true;
}

void PulseAudio::cleanup() {
    destroy_audio_stream();
    
    if (context_) {
        pa_context_disconnect(context_);
        pa_context_unref(context_);
        context_ = nullptr;
    }
    
    if (mainloop_) {
        pa_threaded_mainloop_stop(mainloop_);
        pa_threaded_mainloop_free(mainloop_);
        mainloop_ = nullptr;
    }
    
    initialized_ = false;
}

bool PulseAudio::is_any_application_playing_audio() {
    if (!initialized_) {
        return false;
    }
    
    update_playback_status();
    return other_app_playing_;
}

void PulseAudio::set_auto_mute_enabled(bool enabled) {
    auto_mute_enabled_ = enabled;
}

bool PulseAudio::should_mute_background_audio() {
    return auto_mute_enabled_ && is_any_application_playing_audio();
}

void PulseAudio::context_state_callback(pa_context* context, void* userdata) {
    PulseAudio* pulse = static_cast<PulseAudio*>(userdata);
    
    switch (pa_context_get_state(context)) {
        case PA_CONTEXT_READY:
        case PA_CONTEXT_FAILED:
        case PA_CONTEXT_TERMINATED:
            pa_threaded_mainloop_signal(pulse->mainloop_, 0);
            break;
        default:
            break;
    }
}

void PulseAudio::sink_input_list_callback(pa_context* context, const pa_sink_input_info* info, 
                                         int eol, void* userdata) {
    PulseAudio* pulse = static_cast<PulseAudio*>(userdata);
    
    if (eol) {
        pa_threaded_mainloop_signal(pulse->mainloop_, 0);
        return;
    }
    
    if (info) {
        // Check if this sink input is from another application
        const char* app_name = pa_proplist_gets(info->proplist, PA_PROP_APPLICATION_NAME);
        if (app_name && std::strcmp(app_name, "linux-wallpaperengine-ext") != 0) {
            // Check if the sink input is valid, not muted, and not corked (not paused)
            if (info->index != PA_INVALID_INDEX && !info->mute && !info->corked) {
                const char* media_role = pa_proplist_gets(info->proplist, PA_PROP_MEDIA_ROLE);
                
                // Check if volume is above a threshold (indicates actual audio playback)
                pa_volume_t avg_volume = pa_cvolume_avg(&info->volume);
                pa_volume_t threshold = PA_VOLUME_NORM * 0.01; // 1% of normal volume
                
                // Debug: Show what applications are detected
                std::cout << "DEBUG: Detected audio app: '" << app_name 
                          << "' (role: " << (media_role ? media_role : "none") 
                          << ", muted: " << (info->mute ? "yes" : "no")
                          << ", corked: " << (info->corked ? "yes" : "no")
                          << ", volume: " << (avg_volume * 100 / PA_VOLUME_NORM) << "%)" << std::endl;
                
                // Only trigger auto-mute if:
                // 1. The sink input is not corked (actively playing, not paused)
                // 2. Volume is above threshold 
                // 3. It's a media application (music, video, game) or no role specified
                if (avg_volume > threshold && 
                    (!media_role || 
                     std::strcmp(media_role, "music") == 0 || 
                     std::strcmp(media_role, "video") == 0 || 
                     std::strcmp(media_role, "game") == 0 ||
                     std::strcmp(media_role, "phone") == 0)) {
                    std::cout << "DEBUG: Application '" << app_name << "' triggers auto-mute (actively playing audio)" << std::endl;
                    pulse->other_app_playing_ = true;
                }
            } else {
                // Debug info for paused/inactive applications
                const char* media_role = pa_proplist_gets(info->proplist, PA_PROP_MEDIA_ROLE);
                std::cout << "DEBUG: Ignoring audio app: '" << app_name 
                          << "' (corked: " << (info->corked ? "yes" : "no")
                          << ", muted: " << (info->mute ? "yes" : "no") << ")" << std::endl;
            }
        }
    }
}

void PulseAudio::update_playback_status() {
    if (!initialized_) {
        return;
    }
    
    pa_threaded_mainloop_lock(mainloop_);
    
    // Reset status
    other_app_playing_ = false;
    
    // Get sink input list
    pa_operation* op = pa_context_get_sink_input_info_list(context_, 
                                                          sink_input_list_callback, this);
    if (op) {
        // Wait for the operation to complete
        while (pa_operation_get_state(op) == PA_OPERATION_RUNNING) {
            pa_threaded_mainloop_wait(mainloop_);
        }
        pa_operation_unref(op);
    }
    
    pa_threaded_mainloop_unlock(mainloop_);
}

// Audio playback functionality
bool PulseAudio::create_audio_stream(int sample_rate, int channels) {
    if (!initialized_ || audio_stream_) {
        return false;
    }
    
    pa_threaded_mainloop_lock(mainloop_);
    
    // Set up sample spec
    audio_spec_.format = PA_SAMPLE_S16LE;
    audio_spec_.rate = sample_rate;
    audio_spec_.channels = channels;
    
    if (!pa_sample_spec_valid(&audio_spec_)) {
        std::cerr << "ERROR: Invalid audio sample specification" << std::endl;
        pa_threaded_mainloop_unlock(mainloop_);
        return false;
    }
    
    // Create stream
    audio_stream_ = pa_stream_new(context_, "linux-wallpaperengine-ext", &audio_spec_, nullptr);
    if (!audio_stream_) {
        std::cerr << "ERROR: Could not create audio stream" << std::endl;
        pa_threaded_mainloop_unlock(mainloop_);
        return false;
    }
    
    // Set stream callbacks
    pa_stream_set_state_callback(audio_stream_, stream_state_callback, this);
    pa_stream_set_write_callback(audio_stream_, stream_write_callback, this);
    
    // Set up buffer attributes for low latency
    pa_buffer_attr buffer_attr;
    buffer_attr.maxlength = -1;
    buffer_attr.tlength = pa_usec_to_bytes(50000, &audio_spec_); // 50ms buffer
    buffer_attr.prebuf = -1;
    buffer_attr.minreq = -1;
    buffer_attr.fragsize = -1;
    
    // Connect stream for playback
    int result = pa_stream_connect_playback(audio_stream_, nullptr, &buffer_attr,
                                          static_cast<pa_stream_flags_t>(
                                              PA_STREAM_ADJUST_LATENCY |
                                              PA_STREAM_AUTO_TIMING_UPDATE),
                                          nullptr, nullptr);
    
    if (result < 0) {
        std::cerr << "ERROR: Could not connect audio stream for playback" << std::endl;
        pa_stream_unref(audio_stream_);
        audio_stream_ = nullptr;
        pa_threaded_mainloop_unlock(mainloop_);
        return false;
    }
    
    // Wait for stream to be ready
    while (pa_stream_get_state(audio_stream_) != PA_STREAM_READY) {
        if (pa_stream_get_state(audio_stream_) == PA_STREAM_FAILED) {
            std::cerr << "ERROR: Audio stream failed to connect" << std::endl;
            pa_stream_unref(audio_stream_);
            audio_stream_ = nullptr;
            pa_threaded_mainloop_unlock(mainloop_);
            return false;
        }
        pa_threaded_mainloop_wait(mainloop_);
    }
    
    audio_stream_ready_ = true;
    pa_threaded_mainloop_unlock(mainloop_);
    
    std::cout << "DEBUG: Audio stream created successfully - " 
              << sample_rate << "Hz, " << channels << " channels" << std::endl;
    return true;
}

void PulseAudio::destroy_audio_stream() {
    if (!audio_stream_) {
        return;
    }
    
    pa_threaded_mainloop_lock(mainloop_);
    
    if (audio_stream_) {
        pa_stream_disconnect(audio_stream_);
        pa_stream_unref(audio_stream_);
        audio_stream_ = nullptr;
    }
    
    audio_stream_ready_ = false;
    
    // Clear audio buffer
    std::lock_guard<std::mutex> lock(audio_buffer_mutex_);
    while (!audio_buffer_.empty()) {
        audio_buffer_.pop();
    }
    
    pa_threaded_mainloop_unlock(mainloop_);
    
    std::cout << "DEBUG: Audio stream destroyed" << std::endl;
}

bool PulseAudio::write_audio_data(const uint8_t* data, size_t size) {
    if (!audio_stream_ready_ || !data || size == 0) {
        return false;
    }
    
    // Buffer the audio data for thread-safe handling
    {
        std::lock_guard<std::mutex> lock(audio_buffer_mutex_);
        audio_buffer_.emplace(data, data + size);
    }
    
    // Process buffered audio in the PulseAudio thread context
    pa_threaded_mainloop_lock(mainloop_);
    process_audio_buffer();
    pa_threaded_mainloop_unlock(mainloop_);
    
    return true;
}

void PulseAudio::set_playback_volume(int volume) {
    playback_volume_ = std::max(0, std::min(100, volume));
    
    if (!audio_stream_ready_) {
        return;
    }
    
    pa_threaded_mainloop_lock(mainloop_);
    
    // Get stream index
    uint32_t stream_index = pa_stream_get_index(audio_stream_);
    if (stream_index != PA_INVALID_INDEX) {
        // Set volume
        pa_cvolume cv;
        pa_cvolume_set(&cv, audio_spec_.channels, 
                      PA_VOLUME_NORM * playback_volume_ / 100);
        
        pa_operation* op = pa_context_set_sink_input_volume(context_, stream_index, 
                                                           &cv, nullptr, nullptr);
        if (op) {
            pa_operation_unref(op);
        }
    }
    
    pa_threaded_mainloop_unlock(mainloop_);
    
    std::cout << "DEBUG: Audio playback volume set to " << playback_volume_ << "%" << std::endl;
}

void PulseAudio::set_playback_muted(bool muted) {
    playback_muted_ = muted;
    
    if (!audio_stream_ready_) {
        return;
    }
    
    pa_threaded_mainloop_lock(mainloop_);
    
    // Get stream index
    uint32_t stream_index = pa_stream_get_index(audio_stream_);
    if (stream_index != PA_INVALID_INDEX) {
        pa_operation* op = pa_context_set_sink_input_mute(context_, stream_index, 
                                                         muted, nullptr, nullptr);
        if (op) {
            pa_operation_unref(op);
        }
    }
    
    pa_threaded_mainloop_unlock(mainloop_);
    
    std::cout << "DEBUG: Audio playback mute set to " << (muted ? "ON" : "OFF") << std::endl;
}

bool PulseAudio::is_audio_stream_active() const {
    return audio_stream_ready_;
}

// Static callbacks
void PulseAudio::stream_state_callback(pa_stream* stream, void* userdata) {
    PulseAudio* pulse = static_cast<PulseAudio*>(userdata);
    pa_threaded_mainloop_signal(pulse->mainloop_, 0);
}

void PulseAudio::stream_write_callback(pa_stream* stream, size_t nbytes, void* userdata) {
    PulseAudio* pulse = static_cast<PulseAudio*>(userdata);
    pulse->process_audio_buffer();
}

void PulseAudio::process_audio_buffer() {
    if (!audio_stream_ready_) {
        return;
    }
    
    size_t writable_size = pa_stream_writable_size(audio_stream_);
    if (writable_size == 0 || writable_size == static_cast<size_t>(-1)) {
        return;
    }
    
    std::lock_guard<std::mutex> lock(audio_buffer_mutex_);
    
    while (!audio_buffer_.empty() && writable_size > 0) {
        const auto& buffer = audio_buffer_.front();
        size_t write_size = std::min(buffer.size(), writable_size);
        
        if (pa_stream_write(audio_stream_, buffer.data(), write_size, 
                           nullptr, 0, PA_SEEK_RELATIVE) < 0) {
            std::cerr << "ERROR: Failed to write audio data to stream" << std::endl;
            break;
        }
        
        writable_size -= write_size;
        
        if (write_size == buffer.size()) {
            audio_buffer_.pop();
        } else {
            // Partial write - this shouldn't happen often with proper buffer management
            std::vector<uint8_t> remaining(buffer.begin() + write_size, buffer.end());
            audio_buffer_.front() = std::move(remaining);
            break;
        }
    }
}
