#pragma once

#include <string>
#include <memory>
#include <vector>

enum class DisplayProtocol {
    X11,
    WAYLAND,
    UNKNOWN
};

enum class ScalingMode {
    STRETCH,
    FIT,
    FILL,
    DEFAULT
};

class DisplayOutput {
public:
    virtual ~DisplayOutput() = default;
    virtual bool initialize() = 0;
    virtual void cleanup() = 0;
    virtual bool set_background(const std::string& media_path, ScalingMode scaling) = 0;
    virtual void update() = 0;
    virtual std::string get_name() const = 0;
};

class DisplayManager {
public:
    DisplayManager();
    ~DisplayManager();
    
    DisplayProtocol detect_protocol();
    DisplayProtocol get_protocol() const { return protocol_; }
    bool initialize();
    void cleanup();
    
    std::vector<std::unique_ptr<DisplayOutput>> get_outputs();
    std::unique_ptr<DisplayOutput> get_output_by_name(const std::string& name);
    
    // For windowed mode
    std::unique_ptr<DisplayOutput> create_window(int x, int y, int width, int height);

private:
    DisplayProtocol protocol_;
    bool initialized_;
};
