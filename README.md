# NUC Display Dashboard

A high-performance, bare-metal dashboard system for Intel NUC devices, built directly on the Linux DRM/KMS stack.

## Objective
To create a zero-overhead, ultra-low resource dashboard that utilizes native Intel hardware features for 3D graphics and video playback without the need for an X server or Wayland compositor.

---

## 🛠 Installation & Deployment

### 1. Automated Installation
The project includes a comprehensive installation script that handles dependencies, permissions, and service setup.

```bash
# Clone the repository
git clone https://github.com/arunj123/nuc_display.git
cd nuc_display

# Run the installer
chmod +x scripts/install.sh
./scripts/install.sh
```

**What this script does:**
- Installs all system dependencies (FFmpeg, DRM, ALSA, FreeType, etc.).
- Adds your user to critical hardware groups (`video`, `render`, `audio`, `plugdev`).
- Builds the project using CMake.
- Installs and enables the `nuc_display.service` for automatic startup on boot.

### 2. Manual Build
If you prefer to build manually:
```bash
mkdir build && cd build
cmake ..
make -j$(nproc)
sudo systemctl restart nuc_display
```

---

## ⚙️ Configuration (`config.json`)

The application is configured via `config.json`. A default file is generated on first run.

### Global Settings
- `location`: Set your address. If `lat`/`lon` are `0.0`, it will auto-geocode on first launch.
- `stocks`: Array of stock objects with `symbol`, `name`, and `currency_symbol`.

### Multi-Region Video Configuration
The dashboard supports multiple, independent hardware-accelerated video streams.

```json
{
    "videos": [
        {
            "enabled": true,
            "x": 0.70, "y": 0.03, "w": 0.25, "h": 0.20,
            "playlists": ["tests/samples/bbb_sunflower_1080p.mp4"],
            "audio_enabled": true,
            "audio_device": "default"
        },
        {
            "enabled": true,
            "x": 0.03, "y": 0.80, "w": 0.30, "h": 0.15,
            "playlists": ["tests/samples/sintel_trailer.mp4"],
            "audio_enabled": false
        }
    ]
}
```

| Field | Description |
| :--- | :--- |
| `x, y, w, h` | Destination normalized coordinates (0.0 to 1.0) on the display. |
| `src_x, src_y, src_w, src_h` | (Optional) Source cropping region within the video. |
| `playlists` | Array of file paths to loop through. |
| `audio_enabled` | Enable/Disable ALSA audio for this region. |
| `audio_device` | ALSA device name (e.g., `default`, `plughw:0,3`). |

---

## 🌐 Web Configuration Portal

The application offers an embedded, premium web portal to manage settings, adjust coordinates, inspect screen layouts, and execute remote commands.

### Running the Web Server
To launch the dashboard with the web server enabled, pass the `--http-server` flag:
```bash
./build/nuc_display --http-server
```
You can customize the port using the `--http-port` option (defaults to `8080`):
```bash
./build/nuc_display --http-server --http-port 8080
```

### Key Features
- **Real-Time Layout Preview**: A visual 16:9 canvas representing the screen, showing the exact position, size, and drawing order (z-index) of your active video and camera regions.
- **Double Range Sliders**: Adjust layout dimensions and cropping areas visually.
- **Dynamic Configuration**: Edit location coordinates, geocode addresses via Open-Meteo, add/remove stock tickers, change RSS news sources, manage video playlists, and configure camera inputs.
- **Duplicate Key Validation**: Detects and highlights overlapping keyboard shortcut assignments client-side before saving.
- **Virtual Remote Control**: Send keystrokes directly to the display engine to navigate stocks, chart intervals, play/pause videos, and toggle layout groups.

---

## 🖥 Service Management

The application runs as a systemd service, ensuring high availability.

```bash
# Check status and health metrics
systemctl status nuc_display

# View real-time logs (including performance snapshots)
journalctl -u nuc_display -f

# Restart the engine
sudo systemctl restart nuc_display
```

### Stock Navigation Keys
Configure keyboard shortcuts to manually cycle stocks and chart timeframes in `config.json`:

```json
{
    "stock_keys": {
        "next_stock": "dot",
        "prev_stock": "comma",
        "next_chart": "equal",
        "prev_chart": "minus"
    }
}
```

| Key | Action |
| :--- | :--- |
| `prev_stock` / `next_stock` | Cycle through configured stock symbols |
| `prev_chart` / `next_chart` | Cycle through chart timeframes |

**Available Timeframes:** 1D, 5D, 1M, 3M, 6M, YTD, 1Y

The first key press switches from auto-cycling to manual mode.

### Performance Monitoring
The engine logs hardware stats every 30 seconds:
`[Perf] CPU: 35% | RAM: 270MB | GPU: 100/700 MHz | Temp: 48°C | Uptime: 3600s`

---

## 📸 Headless Screenshots

Since there is no window manager, manual screenshots require a signal:

```bash
# Build the utility
cmake --build build --target screenshot_tool

# Trigger a capture on the running service
./build/screenshot_tool
```
This saves a PNG to `manual_screenshot.png`.

---

## 📜 Video Credits
This project uses samples from the Blender Foundation and other open sources. See [tests/samples/CREDITS.md](tests/samples/CREDITS.md) for full attributions.

## ⚖️ License
[Insert License Here - e.g., MIT]
