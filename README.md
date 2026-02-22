# NUC Display Dashboard

A high-performance, bare-metal dashboard system for Intel NUC devices, built directly on the Linux DRM/KMS stack.

## Objective
To create a zero-overhead, ultra-low resource dashboard that utilizes native Intel hardware features for 3D graphics and video playback without the need for an X server or Wayland compositor.

## Design Philosophy
- **Bare Metal Performance:** Direct interaction with Linux Kernel subsystems (`DRM`, `KMS`, `GBM`, `EGL`).
- **Minimal Dependencies:** Avoid heavy abstraction layers (like SDL2, Qt, X11) to maximize resource efficiency.
- **Hardware Acceleration:** Leverage Intel Quick Sync (VAAPI) for zero-copy video decoding directly into OpenGL textures via DMA-BUFs.
- **Audio Integration:** Synchronized ALSA audio playback directly from the video container.
- **Resilience:** Built-in handling for display hotplugging and graceful hardware teardown.

## Current Tech Stack
- **Graphics API:** OpenGL ES 2.0 (via EGL).
- **Buffer Management:** GBM (Generic Buffer Management).
- **Display Mode Setting:** KMS/DRM (Kernel Mode Setting).
- **Build System:** CMake.
- **CI/CD:** GitHub Actions (Ubuntu standard runners).

## Project Structure
- `src/main.cpp`: Core display initialization and rendering loop.
- `CMakeLists.txt`: Build configuration.
- `.github/workflows/`: CI/CD pipelines.
- `.cursorrules`: Strict development rules for AI assistants.

## User Permissions
To run the application without `sudo`, your user must be part of the `video` and `render` groups:

```bash
sudo usermod -aG video,render $USER
```
*Note: You must log out and back in for these changes to take effect.*

## Local Build & Run
```bash
cmake -S . -B build
cmake --build build
./build/nuc_display
```

## Configuration (`config.json`)
The application is fully configurable via `config.json`, which is auto-generated in the working directory on the first launch if it does not exist.
You can configure your location for weather and sunrise/sunset times, as well as customize the stocks displayed.

```json
{
    "location": {
        "address": "Hasenbuk, Nürnberg, Germany",
        "lat": 0.0,
        "lon": 0.0
    },
    "stocks": [
        {"symbol": "^IXIC", "name": "NASDAQ", "currency_symbol": "$"},
        {"symbol": "APC.F", "name": "Apple", "currency_symbol": "€"}
    ]
}
```
*Note: If `lat` and `lon` are 0.0, the application will automatically call the Open-Meteo Geocoding API to resolve the address string.*

## Standalone Screenshot Tool
The application includes a built-in headless screenshot module using `glReadPixels`. To capture a screenshot manually at any time without restarting the app, a separate utility is provided:

```bash
# Build the tool
cmake --build build --target screenshot_tool

# Run the tool to trigger a screenshot on the running nuc_display process
./build/screenshot_tool
```
This tool sends a `SIGUSR1` signal to the `nuc_display` process, which triggers a capture of the current framebuffer to `manual_screenshot.png`.

## Advanced Features
- **GPU Weather Visualizations:** Real-time 3D volumetric Fractional Brownian Motion (fBM) clouds.
- **Hardware-Accelerated Video Playback:** Supports a `playlists` array in `config.json` with multi-video looping and hardware-accurate NV12 color correction.
- **Integrated Audio:** Decodes and writes interleaved audio samples to the ALSA `default` device synchronously with the video.
- **Top-Right Corner Overlay:** Video playback is anchored to the top-right corner to avoid obscuring dashboard data.
- **Zero-Copy DRM Pipeline:** Frames are passed from the VAAPI decoder to EGL via DMA-BUFs, bypassing CPU memory copies.
- **Day/Night Cycle:** The weather shader respects the actual Sunrise/Sunset times from your geographical location.
- **Dynamic Warnings:** Color-coded layout warnings for High UV Index and Glatteis (Black Ice).
- **Multi-layered Rain & Snow Animations:** Complex falling speeds using native GLSL.
- **Hybrid News Ticker:** Implements vertical and horizontal state-machine logic.
- **Resilient Offline Architecture:** Recovers automatically once connectivity is restored.
