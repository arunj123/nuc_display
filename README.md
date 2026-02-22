# NUC Display Dashboard

A high-performance, bare-metal dashboard system for Intel NUC devices, built directly on the Linux DRM/KMS stack.

## Objective
To create a zero-overhead, ultra-low resource dashboard that utilizes native Intel hardware features for 3D graphics and video playback without the need for an X server or Wayland compositor.

## Design Philosophy
- **Bare Metal Performance:** Direct interaction with Linux Kernel subsystems (`DRM`, `KMS`, `GBM`, `EGL`).
- **Minimal Dependencies:** Avoid heavy abstraction layers (like SDL2, Qt, X11) to maximize resource efficiency.
- **Hardware Acceleration:** Leverage Intel Quick Sync (VAAPI) for video decoding and Intel UHD Graphics for UI rendering.
- **Resilience:** Built-in handling for display hotplugging and graceful hardware teardown.

## Current Tech Stack
- **Graphics API:** OpenGL ES 2.0 (via EGL).
- **Buffer Management:** GBM (Generic Buffer Management).
- **Display Mode Setting:** KMS/DRM (Kernel Mode Setting).
- **Build System:** CMake.
- **CI/CD:** GitHub Actions (Ubuntu standard runners).

## Project Structure
- `src/main.c`: Core display initialization and rendering loop.
- `CMakeLists.txt`: Build configuration.
- `.github/workflows/`: CI/CD pipelines.
- `.cursorrules`: Strict development rules for AI assistants.

## Future Roadmap
- [ ] stock market data integration via REST APIs.
- [ ] Animated weather dashboard with GLES2 shaders.
- [ ] Hardware-accelerated video playback using VAAPI.
- [ ] Support for Vulkan rendering path.

## Local Build & Run
```bash
cmake -S . -B build
cmake --build build
sudo ./build/nuc_display
```
