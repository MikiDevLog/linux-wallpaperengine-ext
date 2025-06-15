cmake_minimum_required(VERSION 3.20)
project(linux-wallpaperengine-ext VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find required packages
find_package(PkgConfig REQUIRED)

# SDL2 support for universal windowing
find_package(SDL2 REQUIRED)
if(NOT SDL2_FOUND)
    pkg_check_modules(SDL2 REQUIRED sdl2)
endif()

# X11 support
find_package(X11 REQUIRED)
pkg_check_modules(XRANDR REQUIRED xrandr)

# Wayland support
pkg_check_modules(WAYLAND_CLIENT REQUIRED wayland-client)
pkg_check_modules(WAYLAND_EGL REQUIRED wayland-egl)
pkg_check_modules(WAYLAND_PROTOCOLS REQUIRED wayland-protocols)
pkg_check_modules(WAYLAND_SCANNER REQUIRED wayland-scanner)

# EGL and OpenGL support for Wayland
find_package(OpenGL REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(EGL REQUIRED egl)

# GLEW for OpenGL extensions
find_package(GLEW REQUIRED)

# SDL2 include directories
if(SDL2_INCLUDE_DIRS)
    include_directories(${SDL2_INCLUDE_DIRS})
endif()

# Get wayland-protocols datadir
execute_process(
    COMMAND ${PKG_CONFIG_EXECUTABLE} wayland-protocols --variable=pkgdatadir
    OUTPUT_VARIABLE WAYLAND_PROTOCOLS_DIR
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

# Generate protocols
find_program(WAYLAND_SCANNER_EXECUTABLE wayland-scanner)

# XDG Shell protocol
set(XDG_SHELL_PROTOCOL "${WAYLAND_PROTOCOLS_DIR}/stable/xdg-shell/xdg-shell.xml")
set(XDG_SHELL_CLIENT_HEADER "${CMAKE_CURRENT_BINARY_DIR}/xdg-shell-client-protocol.h")
set(XDG_SHELL_CLIENT_SOURCE "${CMAKE_CURRENT_BINARY_DIR}/xdg-shell-client-protocol.c")

add_custom_command(
    OUTPUT ${XDG_SHELL_CLIENT_HEADER}
    COMMAND ${WAYLAND_SCANNER_EXECUTABLE} client-header ${XDG_SHELL_PROTOCOL} ${XDG_SHELL_CLIENT_HEADER}
    DEPENDS ${XDG_SHELL_PROTOCOL}
)

add_custom_command(
    OUTPUT ${XDG_SHELL_CLIENT_SOURCE}
    COMMAND ${WAYLAND_SCANNER_EXECUTABLE} private-code ${XDG_SHELL_PROTOCOL} ${XDG_SHELL_CLIENT_SOURCE}
    DEPENDS ${XDG_SHELL_PROTOCOL}
)

# WLR Layer Shell protocol (download if not available)
set(WLR_LAYER_SHELL_PROTOCOL "${CMAKE_CURRENT_SOURCE_DIR}/protocols/wlr-layer-shell-unstable-v1.xml")
set(WLR_LAYER_SHELL_CLIENT_HEADER "${CMAKE_CURRENT_BINARY_DIR}/wlr-layer-shell-unstable-v1-client-protocol.h")
set(WLR_LAYER_SHELL_CLIENT_SOURCE "${CMAKE_CURRENT_BINARY_DIR}/wlr-layer-shell-unstable-v1-client-protocol.c")

# Download the protocol if it doesn't exist
if(NOT EXISTS ${WLR_LAYER_SHELL_PROTOCOL})
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/protocols")
    message(STATUS "Downloading wlr-layer-shell protocol...")
    file(DOWNLOAD 
        "https://raw.githubusercontent.com/swaywm/wlr-protocols/master/unstable/wlr-layer-shell-unstable-v1.xml"
        ${WLR_LAYER_SHELL_PROTOCOL}
        SHOW_PROGRESS
        STATUS download_status
    )
    
    list(GET download_status 0 status_code)
    if(NOT status_code EQUAL 0)
        message(WARNING "Failed to download wlr-layer-shell protocol, using local copy if available")
    endif()
endif()

add_custom_command(
    OUTPUT ${WLR_LAYER_SHELL_CLIENT_HEADER}
    COMMAND ${WAYLAND_SCANNER_EXECUTABLE} client-header ${WLR_LAYER_SHELL_PROTOCOL} ${WLR_LAYER_SHELL_CLIENT_HEADER}
    DEPENDS ${WLR_LAYER_SHELL_PROTOCOL}
)

add_custom_command(
    OUTPUT ${WLR_LAYER_SHELL_CLIENT_SOURCE}
    COMMAND ${WAYLAND_SCANNER_EXECUTABLE} private-code ${WLR_LAYER_SHELL_PROTOCOL} ${WLR_LAYER_SHELL_CLIENT_SOURCE}
    DEPENDS ${WLR_LAYER_SHELL_PROTOCOL}
)

# Create a custom target for the protocol files
add_custom_target(wayland-protocols-generated 
    DEPENDS ${XDG_SHELL_CLIENT_HEADER} ${XDG_SHELL_CLIENT_SOURCE}
            ${WLR_LAYER_SHELL_CLIENT_HEADER} ${WLR_LAYER_SHELL_CLIENT_SOURCE}
)

# MPV for video playback
pkg_check_modules(MPV REQUIRED mpv)

# PulseAudio for audio
pkg_check_modules(PULSEAUDIO REQUIRED libpulse)

# FFmpeg for GIF support and video rendering
pkg_check_modules(FFMPEG REQUIRED libavformat libavcodec libavutil libswscale)

# Add include directories
include_directories(${X11_INCLUDE_DIR})
include_directories(${XRANDR_INCLUDE_DIRS})
include_directories(${WAYLAND_CLIENT_INCLUDE_DIRS})
include_directories(${WAYLAND_EGL_INCLUDE_DIRS})
include_directories(${MPV_INCLUDE_DIRS})
include_directories(${PULSEAUDIO_INCLUDE_DIRS})
include_directories(${FFMPEG_INCLUDE_DIRS})
include_directories(${EGL_INCLUDE_DIRS})
include_directories(${CMAKE_CURRENT_BINARY_DIR})

# Source files
set(SOURCES
    src/main.cpp
    src/application.cpp
    src/media_player.cpp
    src/argument_parser.cpp
    src/display/display_manager.cpp
    src/display/x11/x11_display.cpp
    src/display/x11/x11_image_renderer.cpp
    src/display/x11/x11_video_renderer.cpp
    src/display/wayland/wayland_display.cpp
    src/display/wayland/wayland_image_renderer.cpp
    src/display/wayland/wayland_video_renderer.cpp
    src/display/sdl2_window_display.cpp
    src/audio/pulse_audio.cpp
    ${XDG_SHELL_CLIENT_SOURCE}
    ${WLR_LAYER_SHELL_CLIENT_SOURCE}
)

# Create executable
add_executable(${PROJECT_NAME} ${SOURCES})

# Ensure protocol files are generated before compilation
add_dependencies(${PROJECT_NAME} wayland-protocols-generated)

# Link libraries
target_link_libraries(${PROJECT_NAME}
    ${X11_LIBRARIES}
    ${XRANDR_LIBRARIES}
    ${WAYLAND_CLIENT_LIBRARIES}
    ${WAYLAND_EGL_LIBRARIES}
    ${MPV_LIBRARIES}
    ${PULSEAUDIO_LIBRARIES}
    ${FFMPEG_LIBRARIES}
    ${EGL_LIBRARIES}
    ${OPENGL_LIBRARIES}
    GLEW::GLEW
    ${SDL2_LIBRARIES}
    pthread
    dl
)

# Compiler-specific options
target_compile_definitions(${PROJECT_NAME} PRIVATE
    $<$<CONFIG:Debug>:DEBUG>
    WAYLAND_DISPLAY=1
    X11_DISPLAY=1
)

# Install
install(TARGETS ${PROJECT_NAME} DESTINATION bin)
