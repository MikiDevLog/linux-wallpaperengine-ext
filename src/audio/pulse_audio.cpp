#include "pulse_audio.h"
#include <iostream>
#include <cstring>

PulseAudio::PulseAudio() 
    : mainloop_(nullptr), context_(nullptr), initialized_(false), 
      auto_mute_enabled_(true), other_app_playing_(false) {}

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
        // Check if this sink input is playing and not from our application
        const char* app_name = pa_proplist_gets(info->proplist, PA_PROP_APPLICATION_NAME);
        if (app_name && std::strcmp(app_name, "linux-wallpaperengine-ext") != 0) {
            // Check if the sink input is actually playing (not just connected)
            if (info->index != PA_INVALID_INDEX) {
                pulse->other_app_playing_ = true;
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
