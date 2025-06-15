#pragma once

#include <pulse/pulseaudio.h>
#include <memory>
#include <string>

class PulseAudio {
public:
    PulseAudio();
    ~PulseAudio();
    
    bool initialize();
    void cleanup();
    
    bool is_any_application_playing_audio();
    void set_auto_mute_enabled(bool enabled);
    bool should_mute_background_audio();

private:
    pa_threaded_mainloop* mainloop_;
    pa_context* context_;
    bool initialized_;
    bool auto_mute_enabled_;
    bool other_app_playing_;
    
    static void context_state_callback(pa_context* context, void* userdata);
    static void sink_input_list_callback(pa_context* context, const pa_sink_input_info* info, 
                                        int eol, void* userdata);
    
    void update_playback_status();
};
