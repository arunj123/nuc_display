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

## Debug Screenshots
The application includes a built-in headless screenshot module using `glReadPixels`. To capture a screenshot of the current DRM/GBM framebuffer render state, the application automatically dumps a `debug_weather.png` file to the working directory ~4 seconds after execution once the API calls have resolved and rendering has commenced.

## Advanced Features
- **GPU Weather Visualizations:** Real-time 3D volumetric Fractional Brownian Motion (fBM) clouds.
- **Day/Night Cycle:** The weather shader respects the actual Sunrise/Sunset times from your geographical location, rendering a bright Sun or a crescent Moon with stars.
- **Dynamic Warnings:** Color-coded layout warnings for High UV Index and Glatteis (Black Ice) based on the temperature + precipitation correlation.
- **Multi-layered Rain & Snow Animations:** Complex falling speeds with varying transparency depths using native GLSL rather than looping PNGs.
- **Hybrid News Ticker:** Implements vertical and horizontal state-machine logic to cleanly display the latest Tech headlines from NewsAPI.
- **Resilient Offline Architecture:** If networking fails, the dashboard renders offline placeholders ("Waiting for data...") and automatically recovers once connectivity is restored.
