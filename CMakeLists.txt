cmake_minimum_required(VERSION 3.20)
cmake_policy(SET CMP0135 NEW)
project(nuc_display VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Add compiler warnings
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wpedantic")

find_package(PkgConfig REQUIRED)

include(FetchContent)
FetchContent_Declare(
    json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
)
FetchContent_MakeAvailable(json)

# Core Graphics
pkg_check_modules(DRM REQUIRED libdrm)
pkg_check_modules(GBM REQUIRED gbm)
pkg_check_modules(EGL REQUIRED egl)
pkg_check_modules(GLESv2 REQUIRED glesv2)

# Hardware Acceleration & Video
pkg_check_modules(VA REQUIRED libva)
pkg_check_modules(VA_DRM REQUIRED libva-drm)

# FFmpeg for Video and Audio Containers
pkg_check_modules(AVFORMAT REQUIRED libavformat)
pkg_check_modules(AVCODEC REQUIRED libavcodec)
pkg_check_modules(SWSCALE REQUIRED libswscale)
pkg_check_modules(SWRESAMPLE REQUIRED libswresample)
pkg_check_modules(AVUTIL REQUIRED libavutil)

# Audio
pkg_check_modules(ALSA REQUIRED alsa)

# Fonts & Text
pkg_check_modules(FREETYPE REQUIRED freetype2)
pkg_check_modules(HARFBUZZ REQUIRED harfbuzz)

# Image Decoders
pkg_check_modules(LIBJPEG REQUIRED libjpeg)
pkg_check_modules(LIBPNG REQUIRED libpng)

# Networking
pkg_check_modules(CURL REQUIRED libcurl)

# Concurrency support
find_package(Threads REQUIRED)

include_directories(
    ${DRM_INCLUDE_DIRS}
    ${GBM_INCLUDE_DIRS}
    ${EGL_INCLUDE_DIRS}
    ${GLESv2_INCLUDE_DIRS}
    ${VA_INCLUDE_DIRS}
    ${AVFORMAT_INCLUDE_DIRS}
    ${ALSA_INCLUDE_DIRS}
    ${FREETYPE_INCLUDE_DIRS}
    ${HARFBUZZ_INCLUDE_DIRS}
    ${LIBJPEG_INCLUDE_DIRS}
    ${LIBPNG_INCLUDE_DIRS}
    ${CURL_INCLUDE_DIRS}
    src
)

# Core Source Files
set(SRC_FILES
    src/main.cpp
    src/core/display_manager.cpp
    src/core/renderer.cpp
    src/modules/image_loader.cpp
    src/modules/text_renderer.cpp
    src/modules/video_decoder.cpp
    src/modules/container_reader.cpp
    src/modules/weather_module.cpp
    src/modules/config_module.cpp
    src/modules/config_validator.cpp
    src/modules/stock_module.cpp
    src/modules/screenshot_module.cpp
    src/modules/news_module.cpp
    src/modules/input_module.cpp
    src/modules/performance_monitor.cpp
)

add_executable(nuc_display ${SRC_FILES})

target_link_libraries(nuc_display
    ${DRM_LIBRARIES}
    ${GBM_LIBRARIES}
    ${EGL_LIBRARIES}
    ${GLESv2_LIBRARIES}
    ${VA_LIBRARIES}
    ${VA_DRM_LIBRARIES}
    ${AVFORMAT_LIBRARIES}
    ${AVCODEC_LIBRARIES}
    ${SWSCALE_LIBRARIES}
    ${SWRESAMPLE_LIBRARIES}
    ${AVUTIL_LIBRARIES}
    ${ALSA_LIBRARIES}
    ${FREETYPE_LIBRARIES}
    ${HARFBUZZ_LIBRARIES}
    ${LIBJPEG_LIBRARIES}
    ${LIBPNG_LIBRARIES}
    ${CURL_LIBRARIES}
    nlohmann_json::nlohmann_json
    Threads::Threads
    m
)

# --- Testing and Coverage ---
enable_testing()

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    message(STATUS "Debug build: enabling coverage flags")
    add_compile_options(--coverage)
    add_link_options(--coverage)
endif()


add_subdirectory(tests)

# Screenshot Tool
add_executable(screenshot_tool src/screenshot_tool.cpp)
