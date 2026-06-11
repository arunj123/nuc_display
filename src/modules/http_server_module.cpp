#include "modules/http_server_module.hpp"
#include "modules/input_module.hpp"
#include "modules/config_module.hpp"
#include "modules/config_validator.hpp"

#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <cstring>
#include <atomic>
#include <mutex>
#include <algorithm>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <ifaddrs.h>
#include <net/if.h>

#include <nlohmann/json.hpp>
#include <qrcodegen.hpp>

namespace nuc_display::modules {

// Embedded modern HTML + CSS + JS Console portal
static const std::string HTML_CONSOLE = R"html(<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>NUC Display Console</title>
    <link href="https://fonts.googleapis.com/css2?family=Inter:wght@300;400;500;600;700&display=swap" rel="stylesheet">
    <style>
        :root {
            --bg-gradient: linear-gradient(135deg, #0f0c1b, #050508);
            --panel-bg: rgba(20, 20, 30, 0.65);
            --accent-cyan: #00f2fe;
            --accent-purple: #7f00ff;
            --text-primary: #f0f0f5;
            --text-secondary: #a0a0b0;
            --border: rgba(255, 255, 255, 0.08);
            --border-focus: rgba(0, 242, 254, 0.5);
            --danger: #ff4757;
            --success: #2ed573;
            --transition: all 0.25s cubic-bezier(0.4, 0, 0.2, 1);
        }

        * {
            box-sizing: border-box;
            margin: 0;
            padding: 0;
        }

        body {
            font-family: 'Inter', sans-serif;
            background: var(--bg-gradient);
            color: var(--text-primary);
            min-height: 100vh;
            display: flex;
            flex-direction: column;
            overflow-x: hidden;
        }

        header {
            display: flex;
            justify-content: space-between;
            align-items: center;
            padding: 1.5rem 2rem;
            background: rgba(10, 10, 15, 0.4);
            backdrop-filter: blur(10px);
            border-bottom: 1px solid var(--border);
        }

        .logo {
            display: flex;
            align-items: center;
            gap: 0.5rem;
            font-weight: 700;
            font-size: 1.25rem;
            letter-spacing: 0.5px;
            background: linear-gradient(to right, var(--accent-cyan), #4facfe);
            -webkit-background-clip: text;
            -webkit-text-fill-color: transparent;
        }

        .logo svg {
            stroke: var(--accent-cyan);
        }

        .status-badge {
            display: flex;
            align-items: center;
            gap: 0.5rem;
            font-size: 0.85rem;
            padding: 0.4rem 0.8rem;
            background: rgba(0, 242, 254, 0.1);
            border: 1px solid rgba(0, 242, 254, 0.2);
            border-radius: 20px;
            color: var(--accent-cyan);
            font-weight: 500;
        }

        .status-dot {
            width: 8px;
            height: 8px;
            background: var(--accent-cyan);
            border-radius: 50%;
            box-shadow: 0 0 8px var(--accent-cyan);
            animation: pulse 1.8s infinite;
        }

        @keyframes pulse {
            0% { opacity: 0.5; }
            50% { opacity: 1; }
            100% { opacity: 0.5; }
        }

        .container {
            flex: 1;
            max-width: 1400px;
            width: 100%;
            margin: 0 auto;
            padding: 2rem;
            display: grid;
            grid-template-columns: 1fr 1fr;
            gap: 2rem;
        }

        @media (max-width: 900px) {
            .container {
                grid-template-columns: 1fr;
            }
        }

        .panel {
            background: var(--panel-bg);
            border: 1px solid var(--border);
            border-radius: 16px;
            padding: 2rem;
            backdrop-filter: blur(20px);
            box-shadow: 0 8px 32px 0 rgba(0, 0, 0, 0.3);
            display: flex;
            flex-direction: column;
            gap: 1.5rem;
        }

        h2 {
            font-size: 1.2rem;
            font-weight: 600;
            display: flex;
            align-items: center;
            gap: 0.5rem;
            border-bottom: 1px solid var(--border);
            padding-bottom: 0.75rem;
            color: var(--text-primary);
        }

        h2 svg {
            stroke: var(--text-secondary);
        }

        .form-group {
            display: flex;
            flex-direction: column;
            gap: 0.5rem;
        }

        label {
            font-size: 0.85rem;
            color: var(--text-secondary);
            font-weight: 500;
        }

        input[type="text"], input[type="number"], select, textarea {
            background: rgba(255, 255, 255, 0.04);
            border: 1px solid var(--border);
            border-radius: 8px;
            padding: 0.75rem 1rem;
            color: var(--text-primary);
            font-family: inherit;
            font-size: 0.95rem;
            outline: none;
            transition: var(--transition);
            width: 100%;
        }

        input:focus, select:focus, textarea:focus {
            border-color: var(--accent-cyan);
            background: rgba(255, 255, 255, 0.08);
            box-shadow: 0 0 10px rgba(0, 242, 254, 0.15);
        }

        .row {
            display: grid;
            grid-template-columns: 1fr 1fr;
            gap: 1rem;
        }

        .btn {
            background: linear-gradient(135deg, var(--accent-purple), #5b00c4);
            color: white;
            border: none;
            border-radius: 8px;
            padding: 0.75rem 1.5rem;
            font-weight: 600;
            cursor: pointer;
            transition: var(--transition);
            display: inline-flex;
            align-items: center;
            justify-content: center;
            gap: 0.5rem;
            font-size: 0.95rem;
        }

        .btn:hover {
            transform: translateY(-2px);
            box-shadow: 0 4px 15px rgba(127, 0, 255, 0.4);
        }

        .btn:active {
            transform: translateY(0);
        }

        .btn-cyan {
            background: linear-gradient(135deg, var(--accent-cyan), #00c6ff);
            color: #0b0c10;
        }

        .btn-cyan:hover {
            box-shadow: 0 4px 15px rgba(0, 242, 254, 0.4);
        }

        .btn-secondary {
            background: rgba(255, 255, 255, 0.08);
            border: 1px solid var(--border);
            color: var(--text-primary);
        }

        .btn-secondary:hover {
            background: rgba(255, 255, 255, 0.15);
            box-shadow: none;
        }

        .btn-danger {
            background: var(--danger);
            color: white;
        }

        .btn-danger:hover {
            box-shadow: 0 4px 15px rgba(255, 71, 87, 0.4);
        }

        /* Remote Control Styling */
        .remote-grid {
            display: grid;
            grid-template-columns: repeat(3, 1fr);
            gap: 0.75rem;
        }

        .remote-btn {
            background: rgba(255, 255, 255, 0.05);
            border: 1px solid var(--border);
            border-radius: 12px;
            padding: 1.25rem 0.5rem;
            color: var(--text-primary);
            font-weight: 500;
            cursor: pointer;
            transition: var(--transition);
            display: flex;
            flex-direction: column;
            align-items: center;
            gap: 0.4rem;
            font-size: 0.8rem;
        }

        .remote-btn svg {
            stroke: var(--text-secondary);
            transition: var(--transition);
        }

        .remote-btn:hover {
            background: rgba(255, 255, 255, 0.1);
            border-color: var(--accent-cyan);
            transform: translateY(-2px);
        }

        .remote-btn:hover svg {
            stroke: var(--accent-cyan);
            transform: scale(1.1);
        }

        .remote-btn:active {
            transform: translateY(0);
        }

        .remote-header {
            grid-column: span 3;
            font-size: 0.75rem;
            text-transform: uppercase;
            letter-spacing: 1px;
            color: var(--text-secondary);
            margin: 0.5rem 0 0.2rem 0;
            border-bottom: 1px solid rgba(255, 255, 255, 0.05);
            padding-bottom: 0.25rem;
        }

        /* Dynamic Stock/Playlist items */
        .list-items {
            display: flex;
            flex-direction: column;
            gap: 0.75rem;
            max-height: 250px;
            overflow-y: auto;
            padding-right: 0.5rem;
        }

        .list-item {
            display: flex;
            gap: 0.5rem;
            align-items: center;
            background: rgba(255, 255, 255, 0.03);
            border: 1px solid var(--border);
            padding: 0.5rem;
            border-radius: 8px;
        }

        .alert {
            padding: 1rem;
            border-radius: 8px;
            font-size: 0.9rem;
            display: none;
            line-height: 1.4;
        }

        .alert-error {
            background: rgba(255, 71, 87, 0.15);
            border: 1px solid var(--danger);
            color: #ff6b81;
        }

        .alert-success {
            background: rgba(46, 213, 115, 0.15);
            border: 1px solid var(--success);
            color: #2ed573;
        }

        /* Power Save Toggle */
        .toggle-container {
            display: flex;
            align-items: center;
            justify-content: space-between;
            background: rgba(255, 255, 255, 0.02);
            border: 1px solid var(--border);
            padding: 0.75rem 1rem;
            border-radius: 8px;
        }

        .switch {
            position: relative;
            display: inline-block;
            width: 46px;
            height: 24px;
        }

        .switch input { 
            opacity: 0;
            width: 0;
            height: 0;
        }

        .slider {
            position: absolute;
            cursor: pointer;
            top: 0; left: 0; right: 0; bottom: 0;
            background-color: rgba(255,255,255,0.1);
            transition: .4s;
            border-radius: 24px;
            border: 1px solid var(--border);
        }

        .slider:before {
            position: absolute;
            content: "";
            height: 16px;
            width: 16px;
            left: 3px;
            bottom: 3px;
            background-color: white;
            transition: .4s;
            border-radius: 50%;
        }

        input:checked + .slider {
            background-color: var(--accent-cyan);
            border-color: var(--accent-cyan);
        }

        input:checked + .slider:before {
            transform: translateX(22px);
        }
    </style>
</head>
<body>
    <header>
        <div class="logo">
            <svg width="24" height="24" viewBox="0 0 24 24" fill="none" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
                <rect x="2" y="2" width="20" height="8" rx="2" ry="2"></rect>
                <rect x="2" y="14" width="20" height="8" rx="2" ry="2"></rect>
                <line x1="6" y1="6" x2="6.01" y2="6"></line>
                <line x1="6" y1="18" x2="6.01" y2="18"></line>
            </svg>
            NUC DISPLAY ENGINE
        </div>
        <div class="status-badge">
            <div class="status-dot"></div>
            CONNECTED
        </div>
    </header>

    <div class="container">
        <!-- Configuration Panel -->
        <div class="panel">
            <h2>
                <svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
                    <circle cx="12" cy="12" r="3"></circle>
                    <path d="M19.4 15a1.65 1.65 0 0 0 .33 1.82l.06.06a2 2 0 1 1-2.83 2.83l-.06-.06a1.65 1.65 0 0 0-1.82-.33 1.65 1.65 0 0 0-1 1.51V21a2 2 0 0 1-4 0v-.09A1.65 1.65 0 0 0 9 19.4a1.65 1.65 0 0 0-1.82.33l-.06.06a2 2 0 1 1-2.83-2.83l.06-.06a1.65 1.65 0 0 0 .33-1.82 1.65 1.65 0 0 0-1.51-1H3a2 2 0 0 1 0-4h.09A1.65 1.65 0 0 0 4.6 9a1.65 1.65 0 0 0-.33-1.82l-.06-.06a2 2 0 1 1 2.83-2.83l.06.06a1.65 1.65 0 0 0 1.82.33H9a1.65 1.65 0 0 0 1-1.51V3a2 2 0 0 1 4 0v.09a1.65 1.65 0 0 0 1 1.51 1.65 1.65 0 0 0 1.82-.33l.06-.06a2 2 0 1 1 2.83 2.83l-.06.06a1.65 1.65 0 0 0-.33 1.82V9a1.65 1.65 0 0 0 1.51 1H21a2 2 0 0 1 0 4h-.09a1.65 1.65 0 0 0-1.51 1z"></path>
                </svg>
                CONFIGURATION
            </h2>

            <div id="errorAlert" class="alert alert-error"></div>
            <div id="successAlert" class="alert alert-success">Saved successfully! Configuration reloaded.</div>

            <!-- Location Section -->
            <div class="form-group">
                <label>City / Location Name</label>
                <div style="display:flex; gap:0.5rem;">
                    <input type="text" id="locName" placeholder="e.g. London, UK">
                    <button class="btn btn-secondary" onclick="geocodeAddress()" style="white-space:nowrap;">Geocode</button>
                </div>
            </div>
            <div class="row">
                <div class="form-group">
                    <label>Latitude</label>
                    <input type="number" step="any" id="locLat">
                </div>
                <div class="form-group">
                    <label>Longitude</label>
                    <input type="number" step="any" id="locLon">
                </div>
            </div>

            <!-- Power Save Section -->
            <div class="toggle-container">
                <label style="color:var(--text-primary); font-weight:600;">Power Save Mode</label>
                <label class="switch">
                    <input type="checkbox" id="psEnabled" onchange="togglePowerSaveFields()">
                    <span class="slider"></span>
                </label>
            </div>
            <div class="row" id="psTimes">
                <div class="form-group">
                    <label>Start Time (HH:MM)</label>
                    <input type="text" id="psStart" placeholder="23:00">
                </div>
                <div class="form-group">
                    <label>End Time (HH:MM)</label>
                    <input type="text" id="psEnd" placeholder="07:00">
                </div>
            </div>

            <!-- Stocks Section -->
            <div class="form-group">
                <div style="display:flex; justify-content:space-between; align-items:center;">
                    <label>Stock Symbols</label>
                    <button class="btn btn-secondary" onclick="addStockItem()" style="padding:0.25rem 0.5rem; font-size:0.8rem;">+ Add</button>
                </div>
                <div id="stocksList" class="list-items"></div>
            </div>

            <!-- Playlists Section -->
            <div class="form-group">
                <div style="display:flex; justify-content:space-between; align-items:center;">
                    <label>Video Playlists (Region 0)</label>
                    <button class="btn btn-secondary" onclick="addPlaylistItem()" style="padding:0.25rem 0.5rem; font-size:0.8rem;">+ Add Video</button>
                </div>
                <div id="playlistList" class="list-items"></div>
            </div>

            <button class="btn btn-cyan" onclick="saveConfig()">
                <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
                    <path d="M19 21H5a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2h11l5 5v11a2 2 0 0 1-2 2z"></path>
                    <polyline points="17 21 17 13 7 13 7 21"></polyline>
                    <polyline points="7 3 7 8 15 8"></polyline>
                </svg>
                Save Settings
            </button>
        </div>

        <!-- Remote Control Panel -->
        <div class="panel">
            <h2>
                <svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
                    <rect x="5" y="2" width="14" height="20" rx="2" ry="2"></rect>
                    <circle cx="12" cy="18" r="2"></circle>
                    <line x1="12" y1="6" x2="12" y2="10"></line>
                </svg>
                VIRTUAL REMOTE
            </h2>

            <div class="remote-grid">
                <div class="remote-header">Stock Dashboard</div>
                <button class="remote-btn" onclick="sendControl('comma')">
                    <svg width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
                        <polygon points="19 20 9 12 19 4 19 20"></polygon>
                        <line x1="5" y1="19" x2="5" y2="5"></line>
                    </svg>
                    Prev Stock
                </button>
                <button class="remote-btn" onclick="sendControl('dot')">
                    <svg width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
                        <polygon points="5 4 15 12 5 20 5 4"></polygon>
                        <line x1="19" y1="5" x2="19" y2="19"></line>
                    </svg>
                    Next Stock
                </button>
                <button class="remote-btn" style="opacity:0.3; cursor:default;" disabled></button>

                <button class="remote-btn" onclick="sendControl('minus')">
                    <svg width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
                        <line x1="5" y1="12" x2="19" y2="12"></line>
                    </svg>
                    Prev Chart
                </button>
                <button class="remote-btn" onclick="sendControl('equal')">
                    <svg width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
                        <line x1="12" y1="5" x2="12" y2="19"></line>
                        <line x1="5" y1="12" x2="19" y2="12"></line>
                    </svg>
                    Next Chart
                </button>
                <button class="remote-btn" style="opacity:0.3; cursor:default;" disabled></button>

                <div class="remote-header">Video Playback</div>
                <button class="remote-btn" onclick="sendControl('left')">
                    <svg width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
                        <polygon points="11 19 2 12 11 5 11 19"></polygon>
                        <polygon points="22 19 13 12 22 5 22 19"></polygon>
                    </svg>
                    Prev Video
                </button>
                <button class="remote-btn" onclick="sendControl('right')">
                    <svg width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
                        <polygon points="13 19 22 12 13 5 13 19"></polygon>
                        <polygon points="2 19 11 12 2 5 2 19"></polygon>
                    </svg>
                    Next Video
                </button>
                <button class="remote-btn" onclick="sendControl('p')">
                    <svg width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
                        <polygon points="5 3 19 12 5 21 5 3"></polygon>
                    </svg>
                    Toggle Video
                </button>

                <button class="remote-btn" onclick="sendControl('down')">
                    <svg width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
                        <polygon points="12 19 5 12 19 12 12 19"></polygon>
                    </svg>
                    Skip Backward
                </button>
                <button class="remote-btn" onclick="sendControl('up')">
                    <svg width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
                        <polygon points="12 5 19 12 5 12 12 5"></polygon>
                    </svg>
                    Skip Forward
                </button>
                <button class="remote-btn" onclick="sendControl('v')">
                    <svg width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
                        <path d="M1 12s4-8 11-8 11 8 11 8-4 8-11 8-11-8-11-8z"></path>
                        <circle cx="12" cy="12" r="3"></circle>
                    </svg>
                    Hide/Show All
                </button>
            </div>
        </div>
    </div>

    <script>
        let fullConfig = null;

        // Fetch current config
        async function fetchConfig() {
            try {
                const res = await fetch('/api/config');
                fullConfig = await res.json();
                
                // Populate location
                document.getElementById('locName').value = fullConfig.location.name || '';
                document.getElementById('locLat').value = fullConfig.location.lat || 0;
                document.getElementById('locLon').value = fullConfig.location.lon || 0;

                // Populate Power Save
                document.getElementById('psEnabled').checked = fullConfig.power_save.enabled || false;
                document.getElementById('psStart').value = fullConfig.power_save.start_time || '23:00';
                document.getElementById('psEnd').value = fullConfig.power_save.end_time || '07:00';
                togglePowerSaveFields();

                // Populate Stocks
                const stocksList = document.getElementById('stocksList');
                stocksList.innerHTML = '';
                if (fullConfig.stocks) {
                    fullConfig.stocks.forEach(stock => {
                        createStockItemDOM(stock.symbol, stock.name, stock.currency_symbol);
                    });
                }

                // Populate Playlist (Video 0)
                const playlistList = document.getElementById('playlistList');
                playlistList.innerHTML = '';
                if (fullConfig.videos && fullConfig.videos.length > 0 && fullConfig.videos[0].playlists) {
                    fullConfig.videos[0].playlists.forEach(path => {
                        createPlaylistItemDOM(path);
                    });
                }
            } catch (err) {
                showError('Failed to load configuration from display engine.');
            }
        }

        function togglePowerSaveFields() {
            const enabled = document.getElementById('psEnabled').checked;
            document.getElementById('psTimes').style.opacity = enabled ? '1' : '0.4';
            document.getElementById('psTimes').querySelectorAll('input').forEach(i => i.disabled = !enabled);
        }

        function createStockItemDOM(symbol = '', name = '', currency = '$') {
            const div = document.createElement('div');
            div.className = 'list-item';
            div.innerHTML = `
                <input type="text" placeholder="Sym" class="stock-sym" value="${symbol}" style="flex:1;">
                <input type="text" placeholder="Name" class="stock-name" value="${name}" style="flex:2;">
                <select class="stock-curr" style="flex:1;">
                    <option value="$" ${currency==='$'?'selected':''}>$</option>
                    <option value="€" ${currency==='€'?'selected':''}>€</option>
                    <option value="£" ${currency==='£'?'selected':''}>£</option>
                    <option value="₹" ${currency==='₹'?'selected':''}>₹</option>
                    <option value="¥" ${currency==='¥'?'selected':''}>¥</option>
                </select>
                <button class="btn btn-danger" onclick="this.parentElement.remove()" style="padding:0.4rem 0.6rem; border-radius:6px; font-size:0.8rem;">×</button>
            `;
            document.getElementById('stocksList').appendChild(div);
        }

        function createPlaylistItemDOM(path = '') {
            const div = document.createElement('div');
            div.className = 'list-item';
            div.innerHTML = `
                <input type="text" class="video-path" placeholder="e.g. tests/sample.mp4" value="${path}" style="flex:1;">
                <button class="btn btn-danger" onclick="this.parentElement.remove()" style="padding:0.4rem 0.6rem; border-radius:6px; font-size:0.8rem;">×</button>
            `;
            document.getElementById('playlistList').appendChild(div);
        }

        function addStockItem() { createStockItemDOM(); }
        function addPlaylistItem() { createPlaylistItemDOM(); }

        // Geocoding via Open-Meteo
        async function geocodeAddress() {
            const name = document.getElementById('locName').value;
            if (!name) {
                showError('Please enter a city name first.');
                return;
            }
            try {
                const res = await fetch(`https://geocoding-api.open-meteo.com/v1/search?name=${encodeURIComponent(name)}&count=1&language=en&format=json`);
                const data = await res.json();
                if (data.results && data.results.length > 0) {
                    const first = data.results[0];
                    document.getElementById('locLat').value = first.latitude;
                    document.getElementById('locLon').value = first.longitude;
                    
                    const fullName = first.name + (first.admin1 ? `, ${first.admin1}` : '') + (first.country ? `, ${first.country}` : '');
                    document.getElementById('locName').value = fullName;
                    
                    hideAlerts();
                } else {
                    showError('Location not found.');
                }
            } catch (err) {
                showError('Geocoding network query failed.');
            }
        }

        // Save Config
        async function saveConfig() {
            hideAlerts();
            if (!fullConfig) return;

            // Update local object
            fullConfig.location.name = document.getElementById('locName').value;
            fullConfig.location.lat = parseFloat(document.getElementById('locLat').value);
            fullConfig.location.lon = parseFloat(document.getElementById('locLon').value);

            fullConfig.power_save.enabled = document.getElementById('psEnabled').checked;
            fullConfig.power_save.start_time = document.getElementById('psStart').value;
            fullConfig.power_save.end_time = document.getElementById('psEnd').value;

            // Collect Stocks
            fullConfig.stocks = [];
            const stockElements = document.querySelectorAll('#stocksList .list-item');
            stockElements.forEach(el => {
                const sym = el.querySelector('.stock-sym').value.trim();
                const name = el.querySelector('.stock-name').value.trim();
                const curr = el.querySelector('.stock-curr').value;
                if (sym) {
                    fullConfig.stocks.push({ symbol: sym, name: name, currency_symbol: curr });
                }
            });

            // Collect Playlist
            const paths = [];
            const playlistElements = document.querySelectorAll('#playlistList .video-path');
            playlistElements.forEach(input => {
                const path = input.value.trim();
                if (path) paths.push(path);
            });
            if (fullConfig.videos && fullConfig.videos.length > 0) {
                fullConfig.videos[0].playlists = paths;
            }

            try {
                const res = await fetch('/api/config', {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify(fullConfig)
                });
                
                if (res.ok) {
                    showSuccess();
                } else {
                    const data = await res.json();
                    if (data.errors && data.errors.length > 0) {
                        showError('Validation failed:<br>' + data.errors.map(e => `• ${e}`).join('<br>'));
                    } else {
                        showError('Failed to save configuration due to server error.');
                    }
                }
            } catch (err) {
                showError('Network error saving configuration.');
            }
        }

        // Send Key Press
        async function sendControl(keyName) {
            try {
                await fetch('/api/control', {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify({ key: keyName })
                });
            } catch (err) {
                console.error('Failed to send control command:', err);
            }
        }

        function showError(msg) {
            const alert = document.getElementById('errorAlert');
            alert.innerHTML = msg;
            alert.style.display = 'block';
            document.getElementById('successAlert').style.display = 'none';
        }

        function showSuccess() {
            document.getElementById('errorAlert').style.display = 'none';
            const alert = document.getElementById('successAlert');
            alert.style.display = 'block';
            setTimeout(() => { alert.style.display = 'none'; }, 4000);
        }

        function hideAlerts() {
            document.getElementById('errorAlert').style.display = 'none';
            document.getElementById('successAlert').style.display = 'none';
        }

        // Init
        fetchConfig();
    </script>
</body>
</html>)html";

// HttpServerModule Implementation

HttpServerModule::HttpServerModule(InputModule* input_module, const std::string& config_path, std::atomic<bool>& reload_flag, int port)
    : input_module_(input_module), config_path_(config_path), reload_flag_(reload_flag), port_(port) {
    ip_address_ = get_local_ip();
    web_address_ = "http://" + ip_address_ + ":" + std::to_string(port_);
    generate_qr_code(web_address_);
}

HttpServerModule::~HttpServerModule() {
    stop();
}

std::string HttpServerModule::get_web_address() const {
    std::lock_guard<std::mutex> lock(ip_mutex_);
    return web_address_;
}

std::string HttpServerModule::get_ip_address() const {
    std::lock_guard<std::mutex> lock(ip_mutex_);
    return ip_address_;
}

int HttpServerModule::get_port() const {
    return port_;
}

QrCodeImage HttpServerModule::get_qr_code_image() {
    std::lock_guard<std::mutex> lock(qr_mutex_);
    qr_code_updated_ = false;
    return qr_image_;
}

void HttpServerModule::start() {
    running_ = true;
    thread_ = std::thread(&HttpServerModule::listen_loop, this);
}

void HttpServerModule::stop() {
    if (!running_) return;
    running_ = false;
    if (server_fd_ >= 0) {
        close(server_fd_);
        server_fd_ = -1;
    }
    if (thread_.joinable()) {
        thread_.join();
    }
}

void HttpServerModule::generate_qr_code(const std::string& text) {
    try {
        qrcodegen::QrCode qr = qrcodegen::QrCode::encodeText(text.c_str(), qrcodegen::QrCode::Ecc::LOW);
        int N = qr.getSize();
        int border = 2;
        int N_bordered = N + border * 2;
        int S = 8;
        int tex_size = N_bordered * S;
        
        std::vector<uint8_t> rgba(tex_size * tex_size * 4, 255); // Default to white
        
        for (int y = 0; y < N; ++y) {
            for (int x = 0; x < N; ++x) {
                if (qr.getModule(x, y)) {
                    // Fill SxS block with black
                    for (int sy = 0; sy < S; ++sy) {
                        for (int sx = 0; sx < S; ++sx) {
                            int out_y = (y + border) * S + sy;
                            int out_x = (x + border) * S + sx;
                            int idx = (out_y * tex_size + out_x) * 4;
                            rgba[idx + 0] = 0;   // R
                            rgba[idx + 1] = 0;   // G
                            rgba[idx + 2] = 0;   // B
                            rgba[idx + 3] = 255; // A
                        }
                    }
                }
            }
        }
        
        std::lock_guard<std::mutex> lock(qr_mutex_);
        qr_image_.rgba_pixels = std::move(rgba);
        qr_image_.size = tex_size;
        qr_code_updated_ = true;
        std::cout << "[HttpServer] Generated QR Code for: " << text << " (Texture Size: " << tex_size << "x" << tex_size << ")\n";
    } catch (const std::exception& e) {
        std::cerr << "[HttpServer] Failed to generate QR Code: " << e.what() << "\n";
    }
}

std::string HttpServerModule::get_local_ip() const {
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock >= 0) {
        struct sockaddr_in serv_addr;
        memset(&serv_addr, 0, sizeof(serv_addr));
        serv_addr.sin_family = AF_INET;
        serv_addr.sin_addr.s_addr = inet_addr("8.8.8.8");
        serv_addr.sin_port = htons(53);

        if (connect(sock, (const struct sockaddr*)&serv_addr, sizeof(serv_addr)) == 0) {
            struct sockaddr_in name;
            socklen_t namelen = sizeof(name);
            if (getsockname(sock, (struct sockaddr*)&name, &namelen) == 0) {
                char ip_str[INET_ADDRSTRLEN];
                if (inet_ntop(AF_INET, &name.sin_addr, ip_str, sizeof(ip_str))) {
                    close(sock);
                    std::string ip(ip_str);
                    if (!ip.empty() && ip != "127.0.0.1" && ip != "127.0.1.1" && ip.rfind("127.", 0) != 0) {
                        return ip;
                    }
                }
            }
        }
        close(sock);
    }

    // Fallback: search all interfaces using getifaddrs
    struct ifaddrs* ifAddrStruct = nullptr;
    std::string ip = "127.0.0.1";

    if (getifaddrs(&ifAddrStruct) == 0) {
        for (struct ifaddrs* ifa = ifAddrStruct; ifa != nullptr; ifa = ifa->ifa_next) {
            if (!ifa->ifa_addr) continue;
            
            // Check it is IPv4 and active (UP)
            if (ifa->ifa_addr->sa_family == AF_INET && (ifa->ifa_flags & IFF_UP)) {
                void* tmpAddrPtr = &((struct sockaddr_in*)ifa->ifa_addr)->sin_addr;
                char addressBuffer[INET_ADDRSTRLEN];
                if (inet_ntop(AF_INET, tmpAddrPtr, addressBuffer, INET_ADDRSTRLEN)) {
                    std::string address_str(addressBuffer);
                    std::string interface_name = ifa->ifa_name;
                    
                    // Exclude loopback interfaces/addresses
                    if (interface_name != "lo" && !(ifa->ifa_flags & IFF_LOOPBACK) && address_str.rfind("127.", 0) != 0) {
                        // Exclude virtual/bridge interfaces (docker, br-, veth, virbr)
                        if (interface_name.rfind("docker", 0) != 0 &&
                            interface_name.rfind("br-", 0) != 0 &&
                            interface_name.rfind("veth", 0) != 0 &&
                            interface_name.rfind("virbr", 0) != 0) {
                            ip = address_str;
                            break;
                        }
                    }
                }
            }
        }
        freeifaddrs(ifAddrStruct);
    }
    return ip;
}

void HttpServerModule::listen_loop() {
    server_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd_ < 0) {
        std::cerr << "[HttpServer] Socket creation failed!\n";
        return;
    }

    int opt = 1;
    setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in address;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port_);

    if (bind(server_fd_, (struct sockaddr*)&address, sizeof(address)) < 0) {
        std::cerr << "[HttpServer] Bind failed on port " << port_ << "! Trying fallback ports...\n";
        // Try other ports if 8080 is blocked
        for (int p = 8081; p < 8090; ++p) {
            address.sin_port = htons(p);
            if (bind(server_fd_, (struct sockaddr*)&address, sizeof(address)) >= 0) {
                port_ = p;
                {
                    std::lock_guard<std::mutex> lock(ip_mutex_);
                    web_address_ = "http://" + ip_address_ + ":" + std::to_string(port_);
                }
                generate_qr_code(web_address_);
                break;
            }
        }
        if (address.sin_port == htons(port_) && port_ == 8080) {
            std::cerr << "[HttpServer] Failed to bind to any fallback port. Exiting server thread.\n";
            close(server_fd_);
            server_fd_ = -1;
            return;
        }
    }

    if (listen(server_fd_, 10) < 0) {
        std::cerr << "[HttpServer] Listen failed!\n";
        close(server_fd_);
        server_fd_ = -1;
        return;
    }

    {
        std::lock_guard<std::mutex> lock(ip_mutex_);
        std::cout << "[HttpServer] Running on: " << web_address_ << "\n";
    }

    auto last_ip_check = std::chrono::steady_clock::now();

    while (running_) {
        // Periodically check for IP address changes (every 5 seconds)
        auto now_time = std::chrono::steady_clock::now();
        if (now_time - last_ip_check >= std::chrono::seconds(5)) {
            last_ip_check = now_time;
            std::string new_ip = get_local_ip();
            if (!new_ip.empty()) {
                bool ip_changed = false;
                std::string current_web_addr;
                {
                    std::lock_guard<std::mutex> lock(ip_mutex_);
                    if (new_ip != ip_address_) {
                        std::cout << "[HttpServer] IP address changed from " << ip_address_ << " to " << new_ip << "\n";
                        ip_address_ = new_ip;
                        web_address_ = "http://" + ip_address_ + ":" + std::to_string(port_);
                        current_web_addr = web_address_;
                        ip_changed = true;
                    }
                }
                if (ip_changed) {
                    generate_qr_code(current_web_addr);
                }
            }
        }
        struct pollfd pfd;
        pfd.fd = server_fd_;
        pfd.events = POLLIN;
        pfd.revents = 0;

        int ret = poll(&pfd, 1, 200); // 200ms timeout
        if (ret < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (ret == 0) continue; // Timeout

        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);
        int client_fd = accept(server_fd_, (struct sockaddr*)&client_addr, &addr_len);
        if (client_fd < 0) {
            if (errno == EMFILE || errno == ENFILE) {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
            continue;
        }

        // Set timeout on client socket (2s)
        struct timeval tv;
        tv.tv_sec = 2;
        tv.tv_usec = 0;
        setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));

        std::string request;
        char buf[4096];
        bool read_headers_ok = false;

        while (request.find("\r\n\r\n") == std::string::npos) {
            int n = recv(client_fd, buf, sizeof(buf) - 1, 0);
            if (n <= 0) break;
            buf[n] = '\0';
            request.append(buf, n);
            if (request.length() > 65536) break; // Limit request size
        }

        size_t body_pos = request.find("\r\n\r\n");
        if (body_pos != std::string::npos) {
            body_pos += 4;
            read_headers_ok = true;
        }

        if (!read_headers_ok) {
            close(client_fd);
            continue;
        }

        // Parse Request-Line (Method and URI)
        std::string method, uri;
        std::stringstream ss(request);
        ss >> method >> uri;

        // Parse Content-Length for POST body
        size_t content_len = 0;
        size_t cl_pos = request.find("Content-Length:");
        if (cl_pos != std::string::npos) {
            cl_pos += 15;
            size_t end_line = request.find("\r\n", cl_pos);
            if (end_line != std::string::npos) {
                std::string cl_str = request.substr(cl_pos, end_line - cl_pos);
                cl_str.erase(0, cl_str.find_first_not_of(" \t"));
                cl_str.erase(cl_str.find_last_not_of(" \t") + 1);
                try {
                    content_len = std::stoul(cl_str);
                } catch (...) {}
            }
        }

        std::string body = request.substr(body_pos);
        while (body.length() < content_len) {
            int to_read = content_len - body.length();
            int n = recv(client_fd, buf, std::min((size_t)sizeof(buf) - 1, (size_t)to_read), 0);
            if (n <= 0) break;
            buf[n] = '\0';
            body.append(buf, n);
        }

        // Handle Request
        std::string response_headers = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
        std::string response_body = "";

        if (method == "GET" && (uri == "/" || uri == "/index.html")) {
            response_body = HTML_CONSOLE;
            std::stringstream header;
            header << "HTTP/1.1 200 OK\r\n"
                   << "Content-Type: text/html; charset=utf-8\r\n"
                   << "Content-Length: " << response_body.length() << "\r\n"
                   << "Connection: close\r\n\r\n";
            response_headers = header.str();
        } 
        else if (method == "GET" && uri == "/api/config") {
            std::ifstream f(config_path_);
            if (f.is_open()) {
                std::stringstream buffer;
                buffer << f.rdbuf();
                response_body = buffer.str();
                std::stringstream header;
                header << "HTTP/1.1 200 OK\r\n"
                       << "Content-Type: application/json; charset=utf-8\r\n"
                       << "Content-Length: " << response_body.length() << "\r\n"
                       << "Connection: close\r\n\r\n";
                response_headers = header.str();
            } else {
                response_body = "{\"error\":\"Config file not found\"}";
                std::stringstream header;
                header << "HTTP/1.1 500 Internal Error\r\n"
                       << "Content-Type: application/json\r\n"
                       << "Content-Length: " << response_body.length() << "\r\n"
                       << "Connection: close\r\n\r\n";
                response_headers = header.str();
            }
        }
        else if (method == "POST" && uri == "/api/config") {
            try {
                auto new_json = nlohmann::json::parse(body);
                
                // Read original config into backup
                std::string backup_content = "";
                std::ifstream in_backup(config_path_);
                if (in_backup.is_open()) {
                    std::stringstream buffer;
                    buffer << in_backup.rdbuf();
                    backup_content = buffer.str();
                    in_backup.close();
                }

                // Write new config to file temporarily
                std::ofstream out_temp(config_path_);
                if (out_temp.is_open()) {
                    out_temp << new_json.dump(4);
                    out_temp.close();
                }

                // Load and Validate using the real ConfigModule parser
                ConfigModule parser;
                auto load_res = parser.load_or_create_config(config_path_);
                std::vector<std::string> validation_errors;
                
                if (load_res) {
                    validation_errors = ConfigValidator::validate(load_res.value());
                } else {
                    validation_errors.push_back("JSON parsing error inside display engine");
                }

                if (!validation_errors.empty()) {
                    // Restore backup
                    if (!backup_content.empty()) {
                        std::ofstream restore(config_path_);
                        restore << backup_content;
                        restore.close();
                    }
                    
                    nlohmann::json err_resp;
                    err_resp["errors"] = validation_errors;
                    response_body = err_resp.dump();
                    
                    std::stringstream header;
                    header << "HTTP/1.1 400 Bad Request\r\n"
                           << "Content-Type: application/json\r\n"
                           << "Content-Length: " << response_body.length() << "\r\n"
                           << "Connection: close\r\n\r\n";
                    response_headers = header.str();
                } else {
                    // Valid! Flag main loop to reload config.json
                    reload_flag_ = true;
                    response_body = "{\"status\":\"ok\"}";
                    std::stringstream header;
                    header << "HTTP/1.1 200 OK\r\n"
                           << "Content-Type: application/json\r\n"
                           << "Content-Length: " << response_body.length() << "\r\n"
                           << "Connection: close\r\n\r\n";
                    response_headers = header.str();
                }
            } catch (const std::exception& e) {
                nlohmann::json err_resp;
                err_resp["errors"] = { std::string("JSON Parse Error: ") + e.what() };
                response_body = err_resp.dump();
                std::stringstream header;
                header << "HTTP/1.1 400 Bad Request\r\n"
                       << "Content-Type: application/json\r\n"
                       << "Content-Length: " << response_body.length() << "\r\n"
                       << "Connection: close\r\n\r\n";
                response_headers = header.str();
            }
        }
        else if (method == "POST" && uri == "/api/control") {
            try {
                auto control_json = nlohmann::json::parse(body);
                if (control_json.contains("key") && control_json["key"].is_string()) {
                    std::string key_name = control_json["key"];
                    uint16_t code = key_name_to_code(key_name);
                    if (code > 0) {
                        input_module_->inject_key(code, 1);
                        response_body = "{\"status\":\"ok\"}";
                        std::stringstream header;
                        header << "HTTP/1.1 200 OK\r\n"
                               << "Content-Type: application/json\r\n"
                               << "Content-Length: " << response_body.length() << "\r\n"
                               << "Connection: close\r\n\r\n";
                        response_headers = header.str();
                    } else {
                        response_body = "{\"error\":\"Unknown key name\"}";
                        std::stringstream header;
                        header << "HTTP/1.1 400 Bad Request\r\n"
                               << "Content-Type: application/json\r\n"
                               << "Content-Length: " << response_body.length() << "\r\n"
                               << "Connection: close\r\n\r\n";
                        response_headers = header.str();
                    }
                } else {
                    response_body = "{\"error\":\"Missing key field\"}";
                    std::stringstream header;
                    header << "HTTP/1.1 400 Bad Request\r\n"
                           << "Content-Type: application/json\r\n"
                           << "Content-Length: " << response_body.length() << "\r\n"
                           << "Connection: close\r\n\r\n";
                    response_headers = header.str();
                }
            } catch (const std::exception& e) {
                response_body = std::string("{\"error\":\"") + e.what() + "\"}";
                std::stringstream header;
                header << "HTTP/1.1 400 Bad Request\r\n"
                       << "Content-Type: application/json\r\n"
                       << "Content-Length: " << response_body.length() << "\r\n"
                       << "Connection: close\r\n\r\n";
                response_headers = header.str();
            }
        }

        send(client_fd, response_headers.c_str(), response_headers.length(), 0);
        if (!response_body.empty()) {
            send(client_fd, response_body.c_str(), response_body.length(), 0);
        }
        close(client_fd);
    }

    close(server_fd_);
    server_fd_ = -1;
}

} // namespace nuc_display::modules
