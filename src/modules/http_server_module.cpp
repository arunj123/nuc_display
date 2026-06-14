#include "modules/http_server_module.hpp"
#include "modules/input_module.hpp"
#include "modules/config_module.hpp"
#include "modules/config_validator.hpp"
#include "modules/container_reader.hpp"

#include <filesystem>
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
    <title>NUC Display Dashboard Console</title>
    <link href="https://fonts.googleapis.com/css2?family=Plus+Jakarta+Sans:wght@300;400;500;600;700&display=swap" rel="stylesheet">
    <style>
        :root {
            --bg-color: #0b0914;
            --sidebar-bg: rgba(15, 12, 28, 0.7);
            --panel-bg: rgba(22, 18, 42, 0.45);
            --card-bg: rgba(255, 255, 255, 0.02);
            --accent-primary: #8a2be2;
            --accent-primary-glow: rgba(138, 43, 226, 0.3);
            --accent-secondary: #00f2fe;
            --accent-secondary-glow: rgba(0, 242, 254, 0.3);
            --text-primary: #f5f4fa;
            --text-secondary: #a3a0bd;
            --border-color: rgba(255, 255, 255, 0.06);
            --border-focus: rgba(0, 242, 254, 0.4);
            --success-color: #2ed573;
            --error-color: #ff4757;
            --transition: all 0.3s cubic-bezier(0.4, 0, 0.2, 1);
            --shadow: 0 8px 32px 0 rgba(0, 0, 0, 0.4);
        }

        * {
            box-sizing: border-box;
            margin: 0;
            padding: 0;
        }

        body {
            font-family: 'Plus Jakarta Sans', sans-serif;
            background-color: var(--bg-color);
            background-image: radial-gradient(circle at top left, #1c1435, var(--bg-color) 70%);
            color: var(--text-primary);
            min-height: 100vh;
            display: flex;
            flex-direction: column;
            overflow-x: hidden;
        }

        /* App Layout */
        .app-container {
            display: flex;
            flex: 1;
            min-height: 100vh;
        }

        /* Sidebar styling */
        aside {
            width: 280px;
            background: var(--sidebar-bg);
            backdrop-filter: blur(20px);
            border-right: 1px solid var(--border-color);
            display: flex;
            flex-direction: column;
            padding: 2rem 1.5rem;
            position: fixed;
            height: 100vh;
            z-index: 100;
            transition: var(--transition);
        }

        .logo-area {
            display: flex;
            align-items: center;
            gap: 0.75rem;
            font-weight: 700;
            font-size: 1.25rem;
            letter-spacing: 0.5px;
            margin-bottom: 2.5rem;
            background: linear-gradient(to right, var(--accent-secondary), #8a2be2);
            -webkit-background-clip: text;
            -webkit-text-fill-color: transparent;
        }

        .logo-area svg {
            stroke: var(--accent-secondary);
            filter: drop-shadow(0 0 5px var(--accent-secondary-glow));
        }

        .nav-links {
            list-style: none;
            display: flex;
            flex-direction: column;
            gap: 0.5rem;
            flex: 1;
        }

        .nav-item {
            display: flex;
            align-items: center;
            gap: 0.75rem;
            padding: 0.85rem 1rem;
            border-radius: 12px;
            cursor: pointer;
            color: var(--text-secondary);
            font-weight: 500;
            transition: var(--transition);
            border: 1px solid transparent;
        }

        .nav-item svg {
            stroke: var(--text-secondary);
            transition: var(--transition);
        }

        .nav-item:hover {
            color: var(--text-primary);
            background: rgba(255, 255, 255, 0.03);
            border-color: rgba(255, 255, 255, 0.05);
        }

        .nav-item:hover svg {
            stroke: var(--text-primary);
        }

        .nav-item.active {
            color: var(--text-primary);
            background: linear-gradient(135deg, rgba(138, 43, 226, 0.15), rgba(0, 242, 254, 0.05));
            border-color: rgba(138, 43, 226, 0.3);
            box-shadow: inset 0 0 12px rgba(138, 43, 226, 0.1), 0 4px 20px rgba(0,0,0,0.15);
        }

        .nav-item.active svg {
            stroke: var(--accent-secondary);
            filter: drop-shadow(0 0 4px var(--accent-secondary-glow));
        }

        .nav-footer {
            margin-top: auto;
            border-top: 1px solid var(--border-color);
            padding-top: 1.5rem;
        }

        .status-badge {
            display: flex;
            align-items: center;
            gap: 0.6rem;
            font-size: 0.85rem;
            padding: 0.6rem 1rem;
            background: rgba(0, 242, 254, 0.06);
            border: 1px solid rgba(0, 242, 254, 0.15);
            border-radius: 24px;
            color: var(--accent-secondary);
            font-weight: 600;
        }

        .status-dot {
            width: 8px;
            height: 8px;
            background: var(--accent-secondary);
            border-radius: 50%;
            box-shadow: 0 0 8px var(--accent-secondary);
            animation: pulse 2s infinite;
        }

        @keyframes pulse {
            0% { opacity: 0.4; }
            50% { opacity: 1; }
            100% { opacity: 0.4; }
        }

        /* Main content styling */
        main {
            margin-left: 280px;
            flex: 1;
            padding: 2rem 3rem;
            min-height: 100vh;
            display: flex;
            flex-direction: column;
            gap: 2rem;
            transition: var(--transition);
        }

        header {
            display: flex;
            justify-content: space-between;
            align-items: center;
            border-bottom: 1px solid var(--border-color);
            padding-bottom: 1.5rem;
        }

        .header-title h1 {
            font-size: 1.75rem;
            font-weight: 700;
            letter-spacing: -0.5px;
        }

        .header-title p {
            color: var(--text-secondary);
            font-size: 0.9rem;
            margin-top: 0.25rem;
        }

        .save-btn-wrapper {
            display: flex;
            gap: 1rem;
        }

        /* Tab panel */
        .tab-panel {
            display: none;
            flex-direction: column;
            gap: 2rem;
            animation: fadeIn 0.4s ease-out;
        }

        .tab-panel.active {
            display: flex;
        }

        @keyframes fadeIn {
            from { opacity: 0; transform: translateY(10px); }
            to { opacity: 1; transform: translateY(0); }
        }

        /* Cards and Panels */
        .glass-panel {
            background: var(--panel-bg);
            backdrop-filter: blur(25px);
            border: 1px solid var(--border-color);
            border-radius: 20px;
            padding: 2rem;
            box-shadow: var(--shadow);
        }

        .glass-panel h2 {
            font-size: 1.25rem;
            font-weight: 600;
            margin-bottom: 1.5rem;
            display: flex;
            align-items: center;
            gap: 0.6rem;
            border-bottom: 1px solid var(--border-color);
            padding-bottom: 0.75rem;
        }

        .glass-panel h2 svg {
            stroke: var(--text-secondary);
        }

        /* Grid elements */
        .grid-2 {
            display: grid;
            grid-template-columns: 1fr 1fr;
            gap: 2rem;
        }

        .grid-3 {
            display: grid;
            grid-template-columns: repeat(3, 1fr);
            gap: 1.5rem;
        }

        .grid-4 {
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(200px, 1fr));
            gap: 1.5rem;
        }

        .mockup-layer.selected {
            border-color: var(--accent-secondary) !important;
            box-shadow: 0 0 15px var(--accent-secondary-glow), inset 0 0 10px rgba(0, 242, 254, 0.2);
            z-index: 1000 !important;
        }
        
        .resize-handle {
            position: absolute;
            right: 0;
            bottom: 0;
            width: 12px;
            height: 12px;
            background: var(--accent-secondary);
            cursor: se-resize;
            border-top-left-radius: 4px;
            box-shadow: 0 0 5px var(--accent-secondary-glow);
            z-index: 1001;
        }

        .mockup-layer-video, .mockup-layer-camera {
            cursor: move;
        }

        @media (max-width: 1100px) {
            .grid-2, .grid-3, .grid-4 {
                grid-template-columns: 1fr;
            }
            aside {
                width: 80px;
                padding: 2rem 0.5rem;
                align-items: center;
            }
            aside .logo-area span, aside .nav-item span, aside .status-badge span {
                display: none;
            }
            aside .nav-item {
                justify-content: center;
                width: 50px;
                height: 50px;
                padding: 0;
            }
            aside .status-badge {
                justify-content: center;
                width: 50px;
                height: 50px;
                padding: 0;
                border-radius: 50%;
            }
            main {
                margin-left: 80px;
                padding: 2rem 1.5rem;
            }
        }

        /* Widgets/Stats */
        .stats-card {
            background: var(--card-bg);
            border: 1px solid var(--border-color);
            border-radius: 16px;
            padding: 1.25rem 1.5rem;
            display: flex;
            align-items: center;
            gap: 1rem;
            transition: var(--transition);
        }

        .stats-card:hover {
            border-color: rgba(0, 242, 254, 0.2);
            transform: translateY(-2px);
            background: rgba(255, 255, 255, 0.04);
        }

        .stats-icon {
            width: 48px;
            height: 48px;
            border-radius: 12px;
            background: rgba(138, 43, 226, 0.1);
            border: 1px solid rgba(138, 43, 226, 0.2);
            display: flex;
            align-items: center;
            justify-content: center;
            color: var(--accent-primary);
        }

        .stats-card:nth-child(even) .stats-icon {
            background: rgba(0, 242, 254, 0.1);
            border: 1px solid rgba(0, 242, 254, 0.2);
            color: var(--accent-secondary);
        }

        .stats-info h3 {
            font-size: 0.8rem;
            text-transform: uppercase;
            letter-spacing: 0.5px;
            color: var(--text-secondary);
            font-weight: 600;
        }

        .stats-info p {
            font-size: 1.25rem;
            font-weight: 700;
            margin-top: 0.2rem;
        }

        /* Visual Display Mockup */
        .mockup-container {
            display: flex;
            flex-direction: column;
            gap: 0.75rem;
            background: var(--card-bg);
            border: 1px solid var(--border-color);
            padding: 1.5rem;
            border-radius: 16px;
        }

        .mockup-screen {
            width: 100%;
            aspect-ratio: 16 / 9;
            background: #06050e;
            border: 2px solid #231d42;
            border-radius: 12px;
            position: relative;
            overflow: hidden;
            box-shadow: inset 0 0 30px rgba(0,0,0,0.8), 0 8px 24px rgba(0,0,0,0.5);
        }

        .mockup-grid {
            position: absolute;
            top: 0; left: 0; right: 0; bottom: 0;
            background-size: 5% 8.88%;
            background-image: 
                linear-gradient(to right, rgba(255, 255, 255, 0.02) 1px, transparent 1px),
                linear-gradient(to bottom, rgba(255, 255, 255, 0.02) 1px, transparent 1px);
        }

        .mockup-layer {
            position: absolute;
            display: flex;
            align-items: center;
            justify-content: center;
            text-align: center;
            font-size: 0.7rem;
            font-weight: 700;
            padding: 4px;
            text-transform: uppercase;
            border-radius: 4px;
            box-shadow: 0 4px 10px rgba(0,0,0,0.3);
            transition: all 0.2s ease;
            cursor: pointer;
        }

        .mockup-layer span {
            background: rgba(0, 0, 0, 0.6);
            padding: 2px 6px;
            border-radius: 4px;
            letter-spacing: 0.5px;
            pointer-events: none;
        }

        .mockup-layer-weather {
            background: rgba(255, 255, 255, 0.03);
            border: 1px dashed rgba(255, 255, 255, 0.2);
            color: #fff;
        }

        .mockup-layer-news {
            background: rgba(255, 165, 0, 0.05);
            border: 1px dashed rgba(255, 165, 0, 0.2);
            color: #ffa500;
        }

        .mockup-layer-stocks {
            background: rgba(46, 213, 115, 0.05);
            border: 1px dashed rgba(46, 213, 115, 0.2);
            color: var(--success-color);
        }

        .mockup-layer-video {
            background: rgba(138, 43, 226, 0.15);
            border: 2px solid var(--accent-primary);
            color: #e5bfff;
            text-shadow: 0 0 8px rgba(138, 43, 226, 0.6);
        }

        .mockup-layer-camera {
            background: rgba(0, 242, 254, 0.15);
            border: 2px solid var(--accent-secondary);
            color: #d1faff;
            text-shadow: 0 0 8px rgba(0, 242, 254, 0.6);
        }

        /* Form elements */
        .form-group {
            display: flex;
            flex-direction: column;
            gap: 0.5rem;
            margin-bottom: 1.25rem;
        }

        .form-row {
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(120px, 1fr));
            gap: 1rem;
        }

        label {
            font-size: 0.85rem;
            color: var(--text-secondary);
            font-weight: 600;
            letter-spacing: 0.3px;
        }

        input[type="text"], input[type="number"], select, textarea {
            background: rgba(255, 255, 255, 0.03);
            border: 1px solid var(--border-color);
            border-radius: 10px;
            padding: 0.75rem 1rem;
            color: var(--text-primary);
            font-family: inherit;
            font-size: 0.95rem;
            outline: none;
            transition: var(--transition);
            width: 100%;
        }

        input:focus, select:focus, textarea:focus {
            border-color: var(--accent-secondary);
            background: rgba(255, 255, 255, 0.06);
            box-shadow: 0 0 12px rgba(0, 242, 254, 0.15);
        }

        /* Sliders */
        .slider-control-group {
            background: rgba(255, 255, 255, 0.01);
            border: 1px solid rgba(255, 255, 255, 0.03);
            padding: 1rem;
            border-radius: 12px;
            margin-top: 0.5rem;
        }

        .slider-header {
            display: flex;
            justify-content: space-between;
            font-size: 0.8rem;
            color: var(--text-secondary);
            margin-bottom: 0.4rem;
        }

        .slider-row {
            display: flex;
            align-items: center;
            gap: 1rem;
        }

        .slider-row input[type="range"] {
            flex: 1;
            accent-color: var(--accent-secondary);
            cursor: pointer;
        }

        .slider-row span {
            font-family: monospace;
            font-size: 0.9rem;
            width: 45px;
            text-align: right;
            font-weight: 600;
        }

        /* Toggle Switches */
        .toggle-container {
            display: flex;
            align-items: center;
            justify-content: space-between;
            background: rgba(255, 255, 255, 0.01);
            border: 1px solid var(--border-color);
            padding: 1rem 1.25rem;
            border-radius: 12px;
            margin-bottom: 1rem;
        }

        .toggle-container label {
            color: var(--text-primary);
            font-size: 0.95rem;
        }

        .switch {
            position: relative;
            display: inline-block;
            width: 48px;
            height: 26px;
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
            background-color: rgba(255,255,255,0.06);
            transition: .3s;
            border-radius: 26px;
            border: 1px solid var(--border-color);
        }

        .slider:before {
            position: absolute;
            content: "";
            height: 18px;
            width: 18px;
            left: 3px;
            bottom: 3px;
            background-color: #f5f4fa;
            transition: .3s;
            border-radius: 50%;
        }

        input:checked + .slider {
            background-color: var(--accent-secondary);
            border-color: var(--accent-secondary);
            box-shadow: 0 0 10px rgba(0, 242, 254, 0.3);
        }

        input:checked + .slider:before {
            transform: translateX(22px);
        }

        /* Buttons */
        .btn {
            background: linear-gradient(135deg, var(--accent-primary), #6002c4);
            color: white;
            border: none;
            border-radius: 10px;
            padding: 0.8rem 1.5rem;
            font-weight: 600;
            cursor: pointer;
            transition: var(--transition);
            display: inline-flex;
            align-items: center;
            justify-content: center;
            gap: 0.5rem;
            font-size: 0.95rem;
            border: 1px solid rgba(255,255,255,0.05);
        }

        .btn:hover {
            transform: translateY(-2px);
            box-shadow: 0 6px 20px var(--accent-primary-glow);
        }

        .btn:active {
            transform: translateY(0);
        }

        .btn-cyan {
            background: linear-gradient(135deg, var(--accent-secondary), #00c6ff);
            color: #0b0914;
            border: none;
        }

        .btn-cyan:hover {
            box-shadow: 0 6px 20px var(--accent-secondary-glow);
        }

        .btn-secondary {
            background: rgba(255, 255, 255, 0.04);
            border: 1px solid var(--border-color);
            color: var(--text-primary);
        }

        .btn-secondary:hover {
            background: rgba(255, 255, 255, 0.08);
            border-color: rgba(255, 255, 255, 0.15);
        }

        .btn-danger {
            background: linear-gradient(135deg, #ff4757, #ff2e44);
            color: white;
        }

        .btn-danger:hover {
            box-shadow: 0 6px 20px rgba(255, 71, 87, 0.3);
        }

        .btn-small {
            padding: 0.4rem 0.8rem;
            font-size: 0.8rem;
            border-radius: 8px;
        }

        /* Accordion panels for Videos */
        .accordion-item {
            background: var(--card-bg);
            border: 1px solid var(--border-color);
            border-radius: 14px;
            margin-bottom: 1rem;
            overflow: hidden;
            transition: var(--transition);
        }

        .accordion-header {
            padding: 1.25rem 1.5rem;
            display: flex;
            justify-content: space-between;
            align-items: center;
            cursor: pointer;
            background: rgba(255, 255, 255, 0.01);
            user-select: none;
            transition: var(--transition);
        }

        .accordion-header:hover {
            background: rgba(255, 255, 255, 0.03);
        }

        .accordion-title-block {
            display: flex;
            align-items: center;
            gap: 0.75rem;
        }

        .accordion-title-block h3 {
            font-size: 1rem;
            font-weight: 600;
        }

        .accordion-title-block .badge {
            font-size: 0.75rem;
            padding: 0.25rem 0.6rem;
            border-radius: 12px;
            font-weight: 600;
        }

        .badge-active {
            background: rgba(46, 213, 115, 0.1);
            border: 1px solid rgba(46, 213, 115, 0.2);
            color: var(--success-color);
        }

        .badge-inactive {
            background: rgba(255, 71, 87, 0.1);
            border: 1px solid rgba(255, 71, 87, 0.2);
            color: var(--error-color);
        }

        .accordion-arrow {
            transition: var(--transition);
        }

        .accordion-item.open .accordion-arrow {
            transform: rotate(180deg);
        }

        .accordion-content {
            display: none;
            padding: 1.5rem;
            border-top: 1px solid var(--border-color);
            background: rgba(0, 0, 0, 0.1);
        }

        .accordion-item.open .accordion-content {
            display: block;
        }

        /* Dynamic lists */
        .list-items {
            display: flex;
            flex-direction: column;
            gap: 0.75rem;
            max-height: 300px;
            overflow-y: auto;
            padding-right: 0.5rem;
            margin-top: 0.5rem;
        }

        .list-item {
            display: flex;
            gap: 0.75rem;
            align-items: center;
            background: rgba(255, 255, 255, 0.015);
            border: 1px solid var(--border-color);
            padding: 0.6rem;
            border-radius: 10px;
            animation: slideIn 0.2s ease-out;
        }

        @keyframes slideIn {
            from { opacity: 0; transform: translateY(5px); }
            to { opacity: 1; transform: translateY(0); }
        }

        /* Custom scrollbars */
        ::-webkit-scrollbar {
            width: 8px;
            height: 8px;
        }
        ::-webkit-scrollbar-track {
            background: rgba(255, 255, 255, 0.01);
        }
        ::-webkit-scrollbar-thumb {
            background: rgba(255, 255, 255, 0.1);
            border-radius: 4px;
        }
        ::-webkit-scrollbar-thumb:hover {
            background: rgba(255, 255, 255, 0.2);
        }

        /* Toast notifications */
        #toastContainer {
            position: fixed;
            top: 2rem;
            right: 2rem;
            display: flex;
            flex-direction: column;
            gap: 0.75rem;
            z-index: 1000;
        }

        .toast {
            min-width: 300px;
            max-width: 450px;
            background: rgba(18, 15, 36, 0.95);
            backdrop-filter: blur(10px);
            border-left: 4px solid var(--accent-secondary);
            border-radius: 8px;
            padding: 1rem 1.25rem;
            box-shadow: 0 10px 30px rgba(0,0,0,0.5);
            display: flex;
            align-items: flex-start;
            gap: 0.75rem;
            animation: toastIn 0.3s cubic-bezier(0.175, 0.885, 0.32, 1.275);
            transition: var(--transition);
        }

        @keyframes toastIn {
            from { transform: translateX(100%) translateY(-10px); opacity: 0; }
            to { transform: translateX(0) translateY(0); opacity: 1; }
        }

        .toast.hide {
            transform: translateX(120%);
            opacity: 0;
        }

        .toast-success { border-left-color: var(--success-color); }
        .toast-error { border-left-color: var(--error-color); }
        .toast-info { border-left-color: var(--accent-secondary); }

        .toast-content {
            flex: 1;
        }

        .toast-title {
            font-weight: 700;
            font-size: 0.9rem;
            margin-bottom: 0.2rem;
        }

        .toast-msg {
            font-size: 0.85rem;
            color: var(--text-secondary);
            line-height: 1.4;
        }

        .toast-close {
            background: none;
            border: none;
            color: var(--text-secondary);
            cursor: pointer;
            font-size: 1.1rem;
            line-height: 1;
            padding: 0;
            margin-top: -2px;
        }

        .toast-close:hover {
            color: var(--text-primary);
        }

        /* Virtual Remote Mobile Design */
        .remote-phone {
            max-width: 360px;
            margin: 0 auto;
            background: #0e0d16;
            border: 4px solid #231d42;
            border-radius: 40px;
            padding: 2.5rem 1.5rem;
            box-shadow: 0 20px 50px rgba(0, 0, 0, 0.6), inset 0 0 15px rgba(255,255,255,0.02);
            position: relative;
        }

        .remote-phone:before {
            content: '';
            position: absolute;
            top: 12px; left: 50%;
            transform: translateX(-50%);
            width: 60px; height: 16px;
            background: #231d42;
            border-radius: 8px;
        }

        .remote-screen-title {
            text-align: center;
            font-size: 0.75rem;
            letter-spacing: 2px;
            text-transform: uppercase;
            color: var(--accent-secondary);
            margin-bottom: 1.5rem;
            font-weight: 700;
            text-shadow: 0 0 8px var(--accent-secondary-glow);
        }

        .remote-dpad {
            position: relative;
            width: 180px;
            height: 180px;
            margin: 1.5rem auto;
            background: #171526;
            border-radius: 50%;
            border: 2px solid var(--border-color);
            box-shadow: 0 8px 16px rgba(0,0,0,0.3);
        }

        .dpad-btn {
            position: absolute;
            background: rgba(255, 255, 255, 0.02);
            border: none;
            cursor: pointer;
            display: flex;
            align-items: center;
            justify-content: center;
            color: var(--text-primary);
            transition: var(--transition);
        }

        .dpad-btn:hover {
            background: rgba(255, 255, 255, 0.06);
            color: var(--accent-secondary);
        }

        .dpad-btn:active {
            transform: scale(0.92);
        }

        .dpad-up {
            top: 8px; left: 65px; width: 50px; height: 45px;
            border-radius: 12px 12px 0 0;
        }
        .dpad-down {
            bottom: 8px; left: 65px; width: 50px; height: 45px;
            border-radius: 0 0 12px 12px;
        }
        .dpad-left {
            left: 8px; top: 65px; width: 45px; height: 50px;
            border-radius: 12px 0 0 12px;
        }
        .dpad-right {
            right: 8px; top: 65px; width: 45px; height: 50px;
            border-radius: 0 12px 12px 0;
        }
        .dpad-center {
            top: 60px; left: 60px; width: 60px; height: 60px;
            background: #1f1b36;
            border-radius: 50%;
            border: 1px solid var(--border-color);
            color: var(--accent-secondary);
        }
        .dpad-center:hover {
            background: #252042;
            box-shadow: 0 0 10px var(--accent-secondary-glow);
        }

        .remote-row {
            display: grid;
            grid-template-columns: repeat(3, 1fr);
            gap: 0.75rem;
            margin-bottom: 0.75rem;
        }

        .remote-title-divider {
            grid-column: span 3;
            font-size: 0.65rem;
            text-transform: uppercase;
            letter-spacing: 1px;
            color: var(--text-secondary);
            margin: 0.75rem 0 0.25rem 0;
            border-bottom: 1px solid rgba(255,255,255,0.05);
            padding-bottom: 2px;
            text-align: center;
        }

        .remote-btn {
            background: rgba(255, 255, 255, 0.03);
            border: 1px solid var(--border-color);
            border-radius: 12px;
            padding: 0.85rem 0.25rem;
            color: var(--text-primary);
            font-weight: 500;
            cursor: pointer;
            transition: var(--transition);
            display: flex;
            flex-direction: column;
            align-items: center;
            gap: 0.3rem;
            font-size: 0.75rem;
        }

        .remote-btn svg {
            stroke: var(--text-secondary);
            transition: var(--transition);
        }

        .remote-btn:hover {
            background: rgba(255, 255, 255, 0.08);
            border-color: var(--accent-secondary);
            transform: translateY(-2px);
        }

        .remote-btn:hover svg {
            stroke: var(--accent-secondary);
            transform: scale(1.08);
        }

        .remote-btn:active {
            transform: translateY(0);
        }

        /* Layout reorder lists */
        .layout-list-item {
            display: flex;
            align-items: center;
            justify-content: space-between;
            background: rgba(255, 255, 255, 0.02);
            border: 1px solid var(--border-color);
            border-radius: 10px;
            padding: 0.75rem 1.25rem;
            margin-bottom: 0.5rem;
        }

        .layout-list-info {
            display: flex;
            align-items: center;
            gap: 0.75rem;
        }

        .layout-drag-handle {
            color: var(--text-secondary);
            cursor: grab;
        }

        .layout-type-badge {
            font-size: 0.75rem;
            text-transform: uppercase;
            padding: 0.2rem 0.5rem;
            border-radius: 6px;
            font-weight: 700;
        }

        .badge-weather { background: rgba(0, 198, 255, 0.1); color: #00c6ff; border: 1px solid rgba(0,198,255,0.2); }
        .badge-stocks { background: rgba(46, 213, 115, 0.1); color: var(--success-color); border: 1px solid rgba(46,213,115,0.2); }
        .badge-news { background: rgba(255, 165, 0, 0.1); color: #ffa500; border: 1px solid rgba(255,165,0,0.2); }
        .badge-video { background: rgba(138, 43, 226, 0.1); color: #c38eff; border: 1px solid rgba(138,43,226,0.2); }
        .badge-camera { background: rgba(0, 242, 254, 0.1); color: var(--accent-secondary); border: 1px solid rgba(0,242,254,0.2); }

        .layout-actions {
            display: flex;
            gap: 0.4rem;
        }

        .layout-btn {
            background: rgba(255, 255, 255, 0.03);
            border: 1px solid var(--border-color);
            color: var(--text-primary);
            cursor: pointer;
            width: 28px;
            height: 28px;
            border-radius: 6px;
            display: flex;
            align-items: center;
            justify-content: center;
            transition: var(--transition);
        }

        .layout-btn:hover {
            background: rgba(255, 255, 255, 0.08);
            border-color: var(--accent-secondary);
            color: var(--accent-secondary);
        }

        .layout-btn:disabled {
            opacity: 0.2;
            cursor: not-allowed;
            color: var(--text-secondary);
            border-color: var(--border-color);
        }
    </style>
</head>
<body>
    <div id="toastContainer"></div>

    <!-- Visual Crop Modal -->
    <div id="cropModal" style="display: none; position: fixed; top: 0; left: 0; width: 100%; height: 100%; background: rgba(11, 9, 20, 0.85); backdrop-filter: blur(10px); z-index: 2000; align-items: center; justify-content: center;">
        <div class="glass-panel" style="width: 90%; max-width: 700px; padding: 2rem; position: relative; border-color: rgba(138, 43, 226, 0.3); background: rgba(22, 18, 42, 0.95);">
            <h2 id="cropModalTitle" style="margin-bottom: 1rem; font-size: 1.25rem;">Visual Crop Editor</h2>
            <p style="font-size: 0.85rem; color: var(--text-secondary); margin-bottom: 1.5rem;">Drag and resize the cropping boundary box over the 16:9 source aspect frame.</p>
            
            <div id="cropWorkspace" style="width: 100%; aspect-ratio: 16 / 9; background: #06050e; border: 2px solid #231d42; border-radius: 8px; position: relative; overflow: hidden; margin-bottom: 1.5rem;">
                <div style="position: absolute; top: 0; left: 0; right: 0; bottom: 0; opacity: 0.15; background: linear-gradient(135deg, var(--accent-primary), var(--accent-secondary)); display: flex; align-items: center; justify-content: center; font-size: 2.5rem; font-weight: 800; letter-spacing: 2px; color: white;">
                    SOURCE MEDIA FRAME
                </div>
                <div id="cropBox" style="position: absolute; border: 2px dashed var(--accent-secondary); background: rgba(0, 242, 254, 0.08); box-shadow: 0 0 15px rgba(0, 242, 254, 0.2); cursor: move;">
                    <div id="cropBoxLabel" style="position: absolute; top: 4px; left: 6px; font-size: 0.65rem; font-weight: 700; text-transform: uppercase; background: rgba(0,0,0,0.6); padding: 2px 4px; border-radius: 3px; color: var(--accent-secondary); pointer-events: none;">Crop Region</div>
                    <div class="resize-handle" id="cropResizeHandle"></div>
                </div>
            </div>
            
            <div style="display: flex; justify-content: space-between; align-items: center;">
                <div style="font-family: monospace; font-size: 0.85rem; color: var(--text-secondary);">
                    X: <span id="cropVal-x">0.00</span> | Y: <span id="cropVal-y">0.00</span> | W: <span id="cropVal-w">1.00</span> | H: <span id="cropVal-h">1.00</span>
                </div>
                <div style="display: flex; gap: 1rem;">
                    <button class="btn btn-secondary" onclick="closeCropEditor(false)">Cancel</button>
                    <button class="btn" onclick="closeCropEditor(true)">Apply Crop Coordinates</button>
                </div>
            </div>
        </div>
    </div>

    <!-- Media Browse Modal -->
    <div id="browseModal" style="display: none; position: fixed; top: 0; left: 0; width: 100%; height: 100%; background: rgba(11, 9, 20, 0.85); backdrop-filter: blur(10px); z-index: 2000; align-items: center; justify-content: center;">
        <div class="glass-panel" style="width: 90%; max-width: 600px; padding: 2rem; position: relative; border-color: rgba(0, 242, 254, 0.3); background: rgba(22, 18, 42, 0.95); display: flex; flex-direction: column; max-height: 85vh;">
            <h2 style="margin-bottom: 1rem; font-size: 1.25rem;">Browse Media Assets</h2>
            <p style="font-size: 0.85rem; color: var(--text-secondary); margin-bottom: 1.5rem;">Select an uploaded media asset to set as the path for your playlist item.</p>
            
            <div style="flex: 1; overflow-y: auto; border: 1px solid var(--border-color); border-radius: 8px; margin-bottom: 1.5rem; background: rgba(6, 5, 14, 0.5);">
                <table style="width: 100%; border-collapse: collapse; text-align: left;">
                    <thead>
                        <tr style="border-bottom: 2px solid var(--border-color); color: var(--text-secondary); font-size: 0.85rem;">
                            <th style="padding: 0.75rem 1rem;">File Name</th>
                            <th style="padding: 0.75rem 1rem; width: 100px;">Size</th>
                            <th style="padding: 0.75rem 1rem; width: 100px; text-align: right;">Action</th>
                        </tr>
                    </thead>
                    <tbody id="browse-modal-list">
                        <!-- Populated dynamically -->
                    </tbody>
                </table>
            </div>
            
            <div style="display: flex; justify-content: flex-end; gap: 1rem;">
                <button class="btn btn-secondary" onclick="closeBrowseModal()">Cancel</button>
            </div>
        </div>
    </div>

    <div class="app-container">
        <!-- Sidebar Navigation -->
        <aside>
            <div class="logo-area">
                <svg width="26" height="26" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5" stroke-linecap="round" stroke-linejoin="round">
                    <rect x="2" y="2" width="20" height="8" rx="2" ry="2"></rect>
                    <rect x="2" y="14" width="20" height="8" rx="2" ry="2"></rect>
                    <line x1="6" y1="6" x2="6.01" y2="6"></line>
                    <line x1="6" y1="18" x2="6.01" y2="18"></line>
                </svg>
                <span>NUC ENGINE</span>
            </div>
            
            <ul class="nav-links">
                <li class="nav-item active" onclick="switchTab('tab-dashboard')">
                    <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
                        <rect x="3" y="3" width="7" height="7"></rect>
                        <rect x="14" y="3" width="7" height="7"></rect>
                        <rect x="14" y="14" width="7" height="7"></rect>
                        <rect x="3" y="14" width="7" height="7"></rect>
                    </svg>
                    <span>Dashboard</span>
                </li>
                <li class="nav-item" onclick="switchTab('tab-media')">
                    <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
                        <rect x="3" y="3" width="18" height="18" rx="2" ry="2"></rect>
                        <line x1="9" y1="3" x2="9" y2="21"></line>
                        <line x1="9" y1="9" x2="21" y2="9"></line>
                        <line x1="9" y1="15" x2="21" y2="15"></line>
                    </svg>
                    <span>Media Manager</span>
                </li>
                <li class="nav-item" onclick="switchTab('tab-location')">
                    <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
                        <path d="M21 10c0 7-9 13-9 13s-9-6-9-13a9 9 0 0 1 18 0z"></path>
                        <circle cx="12" cy="10" r="3"></circle>
                    </svg>
                    <span>Location & Power</span>
                </li>
                <li class="nav-item" onclick="switchTab('tab-videos')">
                    <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
                        <polygon points="23 7 16 12 23 17 23 7"></polygon>
                        <rect x="1" y="5" width="15" height="14" rx="2" ry="2"></rect>
                    </svg>
                    <span>Video Regions</span>
                </li>
                <li class="nav-item" onclick="switchTab('tab-cameras')">
                    <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
                        <path d="M23 19a2 2 0 0 1-2 2H3a2 2 0 0 1-2-2V8a2 2 0 0 1 2-2h4l2-3h6l2 3h4a2 2 0 0 1 2 2z"></path>
                        <circle cx="12" cy="13" r="4"></circle>
                    </svg>
                    <span>Camera Inputs</span>
                </li>
                <li class="nav-item" onclick="switchTab('tab-stocks')">
                    <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
                        <line x1="18" y1="20" x2="18" y2="10"></line>
                        <line x1="12" y1="20" x2="12" y2="4"></line>
                        <line x1="6" y1="20" x2="6" y2="14"></line>
                    </svg>
                    <span>Stocks & Feeds</span>
                </li>
                <li class="nav-item" onclick="switchTab('tab-layout')">
                    <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
                        <polygon points="12 2 2 7 12 12 22 7 12 2"></polygon>
                        <polyline points="2 17 12 22 22 17"></polyline>
                        <polyline points="2 12 12 17 22 12"></polyline>
                    </svg>
                    <span>Layout & Keys</span>
                </li>
                <li class="nav-item" onclick="switchTab('tab-remote')">
                    <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
                        <rect x="5" y="2" width="14" height="20" rx="2" ry="2"></rect>
                        <circle cx="12" cy="18" r="2"></circle>
                        <line x1="12" y1="6" x2="12" y2="10"></line>
                    </svg>
                    <span>Virtual Remote</span>
                </li>
            </ul>

            <div class="nav-footer">
                <div class="status-badge">
                    <div class="status-dot"></div>
                    <span>DISPLAY ONLINE</span>
                </div>
            </div>
        </aside>

        <!-- Main Area -->
        <main>
            <header>
                <div class="header-title">
                    <h1 id="panelTitle">Dashboard Overview</h1>
                    <p id="panelSubtitle">General state and display preview mockup</p>
                </div>
                <div class="save-btn-wrapper" id="headerSaveBtn">
                    <button class="btn btn-cyan" onclick="saveConfig()">
                        <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
                            <path d="M19 21H5a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2h11l5 5v11a2 2 0 0 1-2 2z"></path>
                            <polyline points="17 21 17 13 7 13 7 21"></polyline>
                            <polyline points="7 3 7 8 15 8"></polyline>
                        </svg>
                        Save Settings
                    </button>
                </div>
            </header>

            <!-- Dashboard Tab -->
            <div id="tab-dashboard" class="tab-panel active">
                <div class="grid-4" id="statsGrid">
                    <div class="stats-card">
                        <div class="stats-icon">
                            <svg width="22" height="22" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M21 10c0 7-9 13-9 13s-9-6-9-13a9 9 0 0 1 18 0z"></path><circle cx="12" cy="10" r="3"></circle></svg>
                        </div>
                        <div class="stats-info">
                            <h3>Location</h3>
                            <p id="statsLocation">N/A</p>
                        </div>
                    </div>
                    <div class="stats-card">
                        <div class="stats-icon">
                            <svg width="22" height="22" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><polygon points="23 7 16 12 23 17 23 7"></polygon><rect x="1" y="5" width="15" height="14" rx="2" ry="2"></rect></svg>
                        </div>
                        <div class="stats-info">
                            <h3>Video Regions</h3>
                            <p id="statsVideos">0 Active</p>
                        </div>
                    </div>
                    <div class="stats-card">
                        <div class="stats-icon">
                            <svg width="22" height="22" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M23 19a2 2 0 0 1-2 2H3a2 2 0 0 1-2-2V8a2 2 0 0 1 2-2h4l2-3h6l2 3h4a2 2 0 0 1 2 2z"></path><circle cx="12" cy="13" r="4"></circle></svg>
                        </div>
                        <div class="stats-info">
                            <h3>Cameras</h3>
                            <p id="statsCameras">0 Connected</p>
                        </div>
                    </div>
                    <div class="stats-card">
                        <div class="stats-icon">
                            <svg width="22" height="22" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><line x1="18" y1="20" x2="18" y2="10"></line><line x1="12" y1="20" x2="12" y2="4"></line><line x1="6" y1="20" x2="6" y2="14"></line></svg>
                        </div>
                        <div class="stats-info">
                            <h3>Stocks</h3>
                            <p id="statsStocks">0 Tracked</p>
                        </div>
                    </div>
                    <div class="stats-card" onclick="switchTab('tab-media')" style="cursor:pointer;">
                        <div class="stats-icon">
                            <svg width="22" height="22" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><rect x="3" y="4" width="18" height="18" rx="2" ry="2"></rect><line x1="16" y1="2" x2="16" y2="6"></line><line x1="8" y1="2" x2="8" y2="6"></line><line x1="3" y1="10" x2="21" y2="10"></line></svg>
                        </div>
                        <div class="stats-info">
                            <h3>Media Files</h3>
                            <p id="statsMedia">0 Stored</p>
                        </div>
                    </div>
                </div>

                <div class="mockup-container">
                    <div style="display: flex; justify-content: space-between; align-items: center; margin-bottom: 0.25rem;">
                        <label style="color:var(--text-primary); font-size:1.05rem; font-weight:600;">NUC Screen Layout Preview</label>
                        <div id="interactionModeSelector" style="display: none; gap: 0.5rem; align-items: center; background: rgba(255,255,255,0.03); padding: 4px 8px; border-radius: 8px; border: 1px solid var(--border-color);">
                            <span style="font-size: 0.75rem; color: var(--text-secondary); font-weight: 600; margin-right: 4px;">Edit Mode:</span>
                            <button id="modeBtnLayout" class="btn btn-small" onclick="setInteractionMode('layout')" style="padding: 2px 8px; font-size: 0.75rem;">Layout</button>
                            <button id="modeBtnCrop" class="btn btn-secondary btn-small" onclick="setInteractionMode('crop')" style="padding: 2px 8px; font-size: 0.75rem;">Crop</button>
                        </div>
                    </div>
                    <span style="font-size:0.8rem; color:var(--text-secondary); display:block; margin-bottom:1rem;">Interactive simulation showing screen zones in priority z-order</span>
                    
                    <div class="mockup-screen">
                        <div class="mockup-grid"></div>
                        <div id="mockupLayersContainer"></div>
                    </div>
                </div>
            </div>

            <!-- Media Manager Tab -->
            <div id="tab-media" class="tab-panel">
                <div class="glass-panel">
                    <h2>
                        <svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" style="stroke:var(--accent-secondary);"><path d="M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4"></path><polyline points="17 8 12 3 7 8"></polyline><line x1="12" y1="3" x2="12" y2="15"></line></svg>
                        Upload Media Resources
                    </h2>
                    
                    <div id="upload-zone" style="border: 2px dashed rgba(0, 242, 254, 0.3); border-radius: 14px; padding: 2.5rem; text-align: center; background: rgba(0, 242, 254, 0.01); cursor: pointer; transition: var(--transition);">
                        <svg width="36" height="36" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" style="stroke:var(--accent-secondary); margin-bottom: 0.75rem; filter:drop-shadow(0 0 5px var(--accent-secondary-glow));"><path d="M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4"></path><polyline points="17 8 12 3 7 8"></polyline><line x1="12" y1="3" x2="12" y2="15"></line></svg>
                        <p style="font-weight:600; font-size:1rem; margin-bottom:0.25rem;">Drag & drop video/audio media here, or click to select</p>
                        <p style="font-size:0.8rem; color:var(--text-secondary);">Uploaded resources are placed directly inside target device assets directory</p>
                        <input type="file" id="media-file-input" style="display: none;">
                    </div>
                    
                    <div id="upload-progress-container" style="display: none; margin-top: 1.5rem; background:rgba(255,255,255,0.01); border:1px solid var(--border-color); padding:1rem; border-radius:10px;">
                        <div style="display: flex; justify-content: space-between; font-size: 0.85rem; margin-bottom: 0.5rem;">
                            <span id="upload-filename" style="font-weight: 600; text-overflow:ellipsis; overflow:hidden; white-space:nowrap; max-width:70%;">resource.mp4</span>
                            <span id="upload-percentage" style="font-family:monospace; font-weight:700; color:var(--accent-secondary);">0%</span>
                        </div>
                        <div style="width: 100%; height: 6px; background: rgba(255,255,255,0.05); border-radius: 3px; overflow: hidden;">
                            <div id="upload-progress-bar" style="width: 0%; height: 100%; background: linear-gradient(to right, var(--accent-secondary), var(--accent-primary)); transition: width 0.1s ease;"></div>
                        </div>
                    </div>
                </div>

                <div class="glass-panel">
                    <h2>
                        <svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" style="stroke:var(--accent-primary);"><path d="M22 19a2 2 0 0 1-2 2H4a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2h5l2 3h9a2 2 0 0 1 2 2z"></path></svg>
                        Stored Media Assets Table
                    </h2>
                    <div style="overflow-x:auto;">
                        <table style="width: 100%; border-collapse: collapse; text-align: left; font-size:0.9rem;">
                            <thead>
                                <tr style="border-bottom: 1px solid var(--border-color); color: var(--text-secondary); font-size: 0.8rem; text-transform:uppercase; letter-spacing:0.5px;">
                                    <th style="padding: 1rem;">Asset Filename</th>
                                    <th style="padding: 1rem;">File Size</th>
                                    <th style="padding: 1rem; text-align: right;">Operations</th>
                                </tr>
                            </thead>
                            <tbody id="media-files-list">
                                <!-- Populated dynamically -->
                            </tbody>
                        </table>
                    </div>
                </div>
            </div>

            <!-- Location Tab -->
            <div id="tab-location" class="tab-panel">
                <div class="glass-panel">
                    <h2>
                        <svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M21 10c0 7-9 13-9 13s-9-6-9-13a9 9 0 0 1 18 0z"></path><circle cx="12" cy="10" r="3"></circle></svg>
                        Geographic Settings
                    </h2>
                    <div class="form-group">
                        <label for="locName">City / Target Location Name</label>
                        <div style="display:flex; gap:0.75rem;">
                            <input type="text" id="locName" placeholder="e.g. Nürnberg, DE">
                            <button class="btn btn-secondary" onclick="geocodeAddress()" style="white-space:nowrap;">
                                Geocode Query
                            </button>
                        </div>
                    </div>
                    <div class="form-row">
                        <div class="form-group">
                            <label for="locLat">Latitude (Decimal Degrees)</label>
                            <input type="number" step="any" id="locLat">
                        </div>
                        <div class="form-group">
                            <label for="locLon">Longitude (Decimal Degrees)</label>
                            <input type="number" step="any" id="locLon">
                        </div>
                    </div>
                </div>

                <div class="glass-panel">
                    <h2>
                        <svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><rect x="3" y="4" width="18" height="18" rx="2" ry="2"></rect><line x1="16" y1="2" x2="16" y2="6"></line><line x1="8" y1="2" x2="8" y2="6"></line><line x1="3" y1="10" x2="21" y2="10"></line></svg>
                        Power Save (Schedule)
                    </h2>
                    <div class="toggle-container">
                        <label for="psEnabled" style="font-weight:600;">Enable Power Save Schedule</label>
                        <label class="switch">
                            <input type="checkbox" id="psEnabled" onchange="togglePowerSaveFields()">
                            <span class="slider"></span>
                        </label>
                    </div>
                    <div class="form-row" id="psTimes">
                        <div class="form-group">
                            <label for="psStart">Start Time (HH:MM)</label>
                            <input type="text" id="psStart" placeholder="23:00">
                        </div>
                        <div class="form-group">
                            <label for="psEnd">End Time (HH:MM)</label>
                            <input type="text" id="psEnd" placeholder="07:00">
                        </div>
                    </div>
                </div>
            </div>

            <!-- Video Regions Tab -->
            <div id="tab-videos" class="tab-panel">
                <div class="glass-panel">
                    <div style="display:flex; justify-content:space-between; align-items:center; margin-bottom:1.5rem;">
                        <h2>
                            <svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><polygon points="23 7 16 12 23 17 23 7"></polygon><rect x="1" y="5" width="15" height="14" rx="2" ry="2"></rect></svg>
                            Video Region Decoders
                        </h2>
                        <button class="btn btn-secondary btn-small" onclick="addVideoDecoder()">+ Add Video Decoder</button>
                    </div>
                    
                    <div id="videoAccordionContainer"></div>
                </div>
            </div>

            <!-- Camera Inputs Tab -->
            <div id="tab-cameras" class="tab-panel">
                <div class="glass-panel">
                    <div style="display:flex; justify-content:space-between; align-items:center; margin-bottom:1.5rem;">
                        <h2>
                            <svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M23 19a2 2 0 0 1-2 2H3a2 2 0 0 1-2-2V8a2 2 0 0 1 2-2h4l2-3h6l2 3h4a2 2 0 0 1 2 2z"></path><circle cx="12" cy="13" r="4"></circle></svg>
                            Hardware Video Capture (V4L2 Cameras)
                        </h2>
                        <button class="btn btn-secondary btn-small" onclick="addCameraInput()">+ Add Camera Device</button>
                    </div>

                    <div id="camerasListContainer"></div>
                </div>
            </div>

            <!-- Stocks & News Tab -->
            <div id="tab-stocks" class="tab-panel">
                <div class="glass-panel">
                    <div style="display:flex; justify-content:space-between; align-items:center; margin-bottom:1.5rem;">
                        <h2>
                            <svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><line x1="18" y1="20" x2="18" y2="10"></line><line x1="12" y1="20" x2="12" y2="4"></line><line x1="6" y1="20" x2="6" y2="14"></line></svg>
                            Stock Exchange Symbols
                        </h2>
                        <button class="btn btn-secondary btn-small" onclick="addStockItem()">+ Add Stock Symbol</button>
                    </div>
                    <div id="stocksList" class="list-items"></div>
                </div>

                <div class="glass-panel">
                    <div style="display:flex; justify-content:space-between; align-items:center; margin-bottom:1.5rem;">
                        <h2>
                            <svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M4 11a9 9 0 0 1 9 9"></path><path d="M4 4a16 16 0 0 1 16 16"></path><circle cx="5" cy="19" r="1"></circle></svg>
                            RSS News Feeds
                        </h2>
                        <button class="btn btn-secondary btn-small" onclick="addNewsFeedItem()">+ Add Feed URL</button>
                    </div>
                    
                    <div class="toggle-container">
                        <label for="newsEnabled" style="font-weight:600;">Enable Headlines Module</label>
                        <label class="switch">
                            <input type="checkbox" id="newsEnabled">
                            <span class="slider"></span>
                        </label>
                    </div>

                    <div id="newsList" class="list-items"></div>
                </div>
            </div>

            <!-- Layout & Keys Tab -->
            <div id="tab-layout" class="tab-panel">
                <div class="glass-panel">
                    <h2>
                        <svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><polygon points="12 2 2 7 12 12 22 7 12 2"></polygon><polyline points="2 17 12 22 22 17"></polyline><polyline points="2 12 12 17 22 12"></polyline></svg>
                        Display Layers Draw Priority
                    </h2>
                    <span style="font-size:0.8rem; color:var(--text-secondary); display:block; margin-bottom:1.25rem;">
                        Layers are drawn from top to bottom (items lower in list draw on top of items higher in list).
                    </span>
                    <div id="layoutLayersContainer"></div>
                </div>

                <div class="glass-panel">
                    <h2>
                        <svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><rect x="2" y="4" width="20" height="16" rx="2" ry="2"></rect><line x1="6" y1="20" x2="18" y2="20"></line></svg>
                        Global & Stock Key Bindings
                    </h2>
                    <div class="form-row">
                        <div class="form-group">
                            <label for="keyHideVideos">Hide/Show All Videos</label>
                            <select id="keyHideVideos" class="key-selector"></select>
                        </div>
                        <div class="form-group">
                            <label for="keyNextStock">Next Stock Symbol</label>
                            <select id="keyNextStock" class="key-selector"></select>
                        </div>
                    </div>
                    <div class="form-row">
                        <div class="form-group">
                            <label for="keyPrevStock">Previous Stock Symbol</label>
                            <select id="keyPrevStock" class="key-selector"></select>
                        </div>
                        <div class="form-group">
                            <label for="keyNextChart">Next Financial Chart</label>
                            <select id="keyNextChart" class="key-selector"></select>
                        </div>
                        <div class="form-group">
                            <label for="keyPrevChart">Previous Financial Chart</label>
                            <select id="keyPrevChart" class="key-selector"></select>
                        </div>
                    </div>
                </div>
            </div>

            <!-- Virtual Remote Tab -->
            <div id="tab-remote" class="tab-panel">
                <div class="glass-panel">
                    <h2>
                        <svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><rect x="5" y="2" width="14" height="20" rx="2" ry="2"></rect><circle cx="12" cy="18" r="2"></circle><line x1="12" y1="6" x2="12" y2="10"></line></svg>
                        Tactile Remote Terminal
                    </h2>
                    
                    <div class="remote-phone">
                        <div class="remote-screen-title">NUC Core remote</div>
                        
                        <div class="remote-dpad">
                            <button class="dpad-btn dpad-up" onclick="sendControl('up')" title="Skip Video Forward">
                                <svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5" stroke-linecap="round" stroke-linejoin="round"><polyline points="18 15 12 9 6 15"></polyline></svg>
                            </button>
                            <button class="dpad-btn dpad-down" onclick="sendControl('down')" title="Skip Video Backward">
                                <svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5" stroke-linecap="round" stroke-linejoin="round"><polyline points="6 9 12 15 18 9"></polyline></svg>
                            </button>
                            <button class="dpad-btn dpad-left" onclick="sendControl('left')" title="Previous Video">
                                <svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5" stroke-linecap="round" stroke-linejoin="round"><polyline points="15 18 9 12 15 6"></polyline></svg>
                            </button>
                            <button class="dpad-btn dpad-right" onclick="sendControl('right')" title="Next Video">
                                <svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5" stroke-linecap="round" stroke-linejoin="round"><polyline points="9 18 15 12 9 6"></polyline></svg>
                            </button>
                            <button class="dpad-btn dpad-center" onclick="sendControl('p')" title="Play/Pause Video">
                                <svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5" stroke-linecap="round" stroke-linejoin="round"><polygon points="5 3 19 12 5 21 5 3"></polygon></svg>
                            </button>
                        </div>
                        
                        <div class="remote-row">
                            <div class="remote-title-divider">Stock Navigation</div>
                            <button class="remote-btn" onclick="sendControl('comma')">
                                <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><polyline points="11 17 6 12 11 7"></polyline><polyline points="18 17 13 12 18 7"></polyline></svg>
                                <span>Prev stock</span>
                            </button>
                            <button class="remote-btn" onclick="sendControl('dot')">
                                <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><polyline points="13 17 18 12 13 7"></polyline><polyline points="6 17 11 12 6 7"></polyline></svg>
                                <span>Next stock</span>
                            </button>
                            <button class="remote-btn" onclick="sendControl('v')">
                                <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M1 12s4-8 11-8 11 8 11 8-4 8-11 8-11-8-11-8z"></path><circle cx="12" cy="12" r="3"></circle></svg>
                                <span>Hide layer</span>
                            </button>
                        </div>
                        
                        <div class="remote-row">
                            <div class="remote-title-divider">Chart Intervals</div>
                            <button class="remote-btn" onclick="sendControl('minus')">
                                <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><line x1="5" y1="12" x2="19" y2="12"></line></svg>
                                <span>Prev chart</span>
                            </button>
                            <button class="remote-btn" onclick="sendControl('equal')">
                                <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><line x1="12" y1="5" x2="12" y2="19"></line><line x1="5" y1="12" x2="19" y2="12"></line></svg>
                                <span>Next chart</span>
                            </button>
                            <div class="remote-btn" style="opacity: 0.15; cursor: default;"></div>
                        </div>
                    </div>
                </div>
            </div>
        </main>
    </div>

    <script>
        let fullConfig = null;
        let selectedLayer = null;
        let savedConfigString = '';
        let activeBrowseTarget = null;
        let interactionMode = 'layout';

        function setInteractionMode(mode) {
            interactionMode = mode;
            updateLayoutPreview();
        }

        function copyToClipboard(text) {
            if (navigator.clipboard && navigator.clipboard.writeText) {
                navigator.clipboard.writeText(text)
                    .then(() => {
                        showToast('Path Copied', 'Copied relative media path to clipboard.', 'info');
                    })
                    .catch(err => {
                        fallbackCopy(text);
                    });
            } else {
                fallbackCopy(text);
            }
        }

        function fallbackCopy(text) {
            try {
                const textarea = document.createElement('textarea');
                textarea.value = text;
                textarea.style.position = 'fixed';
                textarea.style.top = '-9999px';
                document.body.appendChild(textarea);
                textarea.focus();
                textarea.select();
                const success = document.execCommand('copy');
                document.body.removeChild(textarea);
                if (success) {
                    showToast('Path Copied', 'Copied relative media path to clipboard.', 'info');
                } else {
                    throw new Error('execCommand copy returned false');
                }
            } catch (err) {
                console.error('Fallback copy failed:', err);
                showToast('Copy Failed', 'Failed to copy path. Please select and copy manually.', 'error');
            }
        }

        function getCurrentUIConfig() {
            if (!fullConfig) return null;
            const cfg = JSON.parse(JSON.stringify(fullConfig));
            
            if (cfg.location) {
                const locNameEl = document.getElementById('locName');
                const locLatEl = document.getElementById('locLat');
                const locLonEl = document.getElementById('locLon');
                if (locNameEl) cfg.location.name = locNameEl.value;
                if (locLatEl) cfg.location.lat = parseFloat(locLatEl.value) || 0;
                if (locLonEl) cfg.location.lon = parseFloat(locLonEl.value) || 0;
            }

            if (cfg.power_save) {
                const psEnabledEl = document.getElementById('psEnabled');
                const psStartEl = document.getElementById('psStart');
                const psEndEl = document.getElementById('psEnd');
                if (psEnabledEl) cfg.power_save.enabled = psEnabledEl.checked;
                if (psStartEl) cfg.power_save.start_time = psStartEl.value;
                if (psEndEl) cfg.power_save.end_time = psEndEl.value;
            }

            const newsEnabledEl = document.getElementById('newsEnabled');
            if (newsEnabledEl) {
                if (!cfg.news) cfg.news = { enabled: true, sources: [] };
                cfg.news.enabled = newsEnabledEl.checked;
            }

            if (cfg.global_keys) {
                const keyHideVideosEl = document.getElementById('keyHideVideos');
                if (keyHideVideosEl) {
                    cfg.global_keys.hide_videos = keyHideVideosEl.value || null;
                }
            }

            if (cfg.stock_keys) {
                const keyNextStockEl = document.getElementById('keyNextStock');
                const keyPrevStockEl = document.getElementById('keyPrevStock');
                const keyNextChartEl = document.getElementById('keyNextChart');
                const keyPrevChartEl = document.getElementById('keyPrevChart');
                if (keyNextStockEl) cfg.stock_keys.next_stock = keyNextStockEl.value || null;
                if (keyPrevStockEl) cfg.stock_keys.prev_stock = keyPrevStockEl.value || null;
                if (keyNextChartEl) cfg.stock_keys.next_chart = keyNextChartEl.value || null;
                if (keyPrevChartEl) cfg.stock_keys.prev_chart = keyPrevChartEl.value || null;
            }

            if (cfg.stocks) {
                cfg.stocks = cfg.stocks.filter(s => s.symbol.trim() !== '');
            }
            if (cfg.news && cfg.news.sources) {
                cfg.news.sources = cfg.news.sources.filter(src => src.trim() !== '');
            }
            if (cfg.videos) {
                cfg.videos.forEach(v => {
                    delete v.lock_aspect;
                    if (v.playlists) {
                        v.playlists = v.playlists.filter(p => p.trim() !== '');
                    }
                });
            }
            if (cfg.cameras) {
                cfg.cameras.forEach(c => {
                    delete c.lock_aspect;
                });
            }
            return cfg;
        }

        function isConfigDirty() {
            if (!fullConfig || !savedConfigString) return false;
            return JSON.stringify(getCurrentUIConfig()) !== savedConfigString;
        }

        async function openBrowseModal(vIdx, pIdx) {
            activeBrowseTarget = { vIdx, pIdx };
            const tbody = document.getElementById('browse-modal-list');
            if (!tbody) return;

            tbody.innerHTML = `
                <tr>
                    <td colspan="3" style="text-align: center; padding: 2rem; color: var(--text-secondary);">
                        Loading assets...
                    </td>
                </tr>
            `;

            const modal = document.getElementById('browseModal');
            if (modal) {
                modal.style.display = 'flex';
            }

            try {
                const res = await fetch('/api/media');
                if (!res.ok) throw new Error('API request failed');
                const files = await res.json();
                
                tbody.innerHTML = '';
                if (files.length === 0) {
                    tbody.innerHTML = `
                        <tr>
                            <td colspan="3" style="text-align: center; padding: 2rem; color: var(--text-secondary);">
                                No media files uploaded yet. Upload files in the Media Manager tab first.
                            </td>
                        </tr>
                    `;
                    return;
                }

                files.forEach(file => {
                    const tr = document.createElement('tr');
                    tr.style.borderBottom = '1px solid var(--border-color)';
                    
                    const sizeMB = (file.size / (1024 * 1024)).toFixed(2);
                    const relativePath = `assets/media/${file.name}`;
                    
                    tr.innerHTML = `
                        <td style="padding: 0.75rem 1rem; font-weight: 500; word-break: break-all;">${file.name}</td>
                        <td style="padding: 0.75rem 1rem; color: var(--text-secondary);">${sizeMB} MB</td>
                        <td style="padding: 0.75rem 1rem; text-align: right;">
                            <button class="btn btn-cyan btn-small" onclick="selectMediaFile('${relativePath.replace(/'/g, "\\'")}')">Select</button>
                        </td>
                    `;
                    tbody.appendChild(tr);
                });
            } catch (err) {
                console.error(err);
                tbody.innerHTML = `
                    <tr>
                        <td colspan="3" style="text-align: center; padding: 2rem; color: var(--accent-primary);">
                            Failed to load media. Check server connection.
                        </td>
                    </tr>
                `;
            }
        }

        function selectMediaFile(path) {
            if (activeBrowseTarget) {
                const { vIdx, pIdx } = activeBrowseTarget;
                updateVideoPlaylistPath(vIdx, pIdx, path);
                renderVideosAccordion();
                document.getElementById(`video-accordion-${vIdx}`).classList.add('open');
            }
            closeBrowseModal();
        }

        function closeBrowseModal() {
            const modal = document.getElementById('browseModal');
            if (modal) {
                modal.style.display = 'none';
            }
            activeBrowseTarget = null;
        }

        function updateLockAspect(type, idx, checked) {
            fullConfig[type][idx].lock_aspect = checked;
            localStorage.setItem(`lock_aspect_${type}_${idx}`, checked ? 'true' : 'false');
        }

        async function resizeToVideoSize(vIdx, pIdx) {
            const path = fullConfig.videos[vIdx].playlists[pIdx];
            if (!path || path.trim() === '') {
                showToast('Validation Alert', 'Please enter or select a video path first.', 'error');
                return;
            }

            try {
                const res = await fetch(`/api/media/dimensions?file=${encodeURIComponent(path)}`);
                if (!res.ok) {
                    const data = await res.json();
                    throw new Error(data.error || 'Failed to fetch video details.');
                }
                const dimensions = await res.json();
                const { width, height } = dimensions;
                if (!width || !height) throw new Error('Invalid dimensions received.');

                let targetW = width / 1920.0;
                let targetH = height / 1080.0;
                if (targetW > 1.0 || targetH > 1.0) {
                    const factor = Math.max(targetW, targetH);
                    targetW /= factor;
                    targetH /= factor;
                }
                
                const video = fullConfig.videos[vIdx];
                video.w = parseFloat(targetW.toFixed(2));
                video.h = parseFloat(targetH.toFixed(2));
                if (video.x + video.w > 1.0) video.x = parseFloat((1.0 - video.w).toFixed(2));
                if (video.y + video.h > 1.0) video.y = parseFloat((1.0 - video.h).toFixed(2));
                
                video.src_x = 0.0;
                video.src_y = 0.0;
                video.src_w = 1.0;
                video.src_h = 1.0;

                renderVideosAccordion();
                updateLayoutPreview();
                if (selectedLayer && selectedLayer.type === 'video' && selectedLayer.index === vIdx) {
                    updateUIFieldsForLayer('video', vIdx);
                }
                showToast('Layout Adjusted', `Resized video region to match ${width}x${height} aspect ratio (w=${video.w}, h=${video.h}).`, 'success');
            } catch (err) {
                console.error(err);
                showToast('Fetch Size Failed', err.message || 'Could not query video size from server.', 'error');
            }
        }

        async function triggerPlayStop(index) {
            const video = fullConfig.videos[index];
            if (!video) {
                showToast('Error', 'Video decoder slot not found.', 'error');
                return;
            }
            if (!video.enabled) {
                showToast('Validation Alert', 'Cannot trigger: Video region is disabled.', 'error');
                return;
            }
            const cleanPlaylists = (video.playlists || []).filter(p => p.trim() !== '');
            if (cleanPlaylists.length === 0) {
                showToast('Validation Alert', 'Cannot play: Playlist has no entries.', 'error');
                return;
            }

            if (isConfigDirty()) {
                if (confirm('You have unsaved changes. Would you like to save settings before toggling video playback?')) {
                    const success = await saveConfig();
                    if (!success) {
                        showToast('Trigger Blocked', 'Settings could not be saved. Playback trigger cancelled.', 'error');
                        return;
                    }
                } else {
                    return;
                }
            }

            try {
                const res = await fetch('/api/video/trigger', {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify({ video_index: index })
                });
                if (res.ok) {
                    showToast('Playback Triggered', `Sent play/stop command for Video Player Slot #${index}.`, 'success');
                } else {
                    const data = await res.json();
                    showToast('Trigger Failed', data.error || 'Server rejected playback toggle.', 'error');
                }
            } catch (err) {
                showToast('Connection Error', 'Failed to communicate playback trigger to backend.', 'error');
            }
        }

        // Key Name List mapped dynamically
        const VALID_KEYS = [
            "", "a", "b", "c", "d", "e", "f", "g", "h", "i", "j", "k", "l", "m", "n", "o", "p", "q", "r", "s", "t", "u", "v", "w", "x", "y", "z",
            "0", "1", "2", "3", "4", "5", "6", "7", "8", "9",
            "up", "down", "left", "right", "space", "enter", "tab", "esc", "backspace", "home", "end", "pageup", "pagedown",
            "f1", "f2", "f3", "f4", "f5", "f6", "f7", "f8", "f9", "f10", "f11", "f12",
            "minus", "equal", "comma", "dot", "slash"
        ];

        // Global key dropdown population helper
        function populateKeySelectors() {
            const selectors = document.querySelectorAll('.key-selector');
            selectors.forEach(sel => {
                sel.innerHTML = '';
                VALID_KEYS.forEach(key => {
                    const opt = document.createElement('option');
                    opt.value = key;
                    opt.textContent = key === "" ? "None (Auto)" : key.toUpperCase();
                    sel.appendChild(opt);
                });
            });
        }

        // Switch panel tabs
        function switchTab(tabId) {
            document.querySelectorAll('.nav-item').forEach(item => item.classList.remove('active'));
            document.querySelectorAll('.tab-panel').forEach(panel => panel.classList.remove('active'));

            const activeNav = Array.from(document.querySelectorAll('.nav-item')).find(item => item.getAttribute('onclick').includes(tabId));
            if (activeNav) activeNav.classList.add('active');

            const targetPanel = document.getElementById(tabId);
            if (targetPanel) targetPanel.classList.add('active');

            // Header titles update
            const titles = {
                'tab-dashboard': ['Dashboard Overview', 'General state and display preview mockup'],
                'tab-media': ['Media Management', 'Upload and organize media resources on the NUC device'],
                'tab-location': ['Location & Scheduling', 'Setup city coordinates and power save intervals'],
                'tab-videos': ['Video Region Decoders', 'Assign video sources, coordinates, and triggers for each viewport'],
                'tab-cameras': ['V4L2 Cameras', 'Configure camera hardware devices and layout grids'],
                'tab-stocks': ['Financials & News RSS', 'Add stocks tickers and headlines feed targets'],
                'tab-layout': ['Layout stack & Key Bindings', 'Set key mapping actions and layers render ordering'],
                'tab-remote': ['Tactile Remote Terminal', 'Send virtual hardware keystrokes directly to the screen']
            };

            const headerInfo = titles[tabId];
            document.getElementById('panelTitle').textContent = headerInfo[0];
            document.getElementById('panelSubtitle').textContent = headerInfo[1];
            
            // Layout Preview redraw when switching back to dashboard
            if (tabId === 'tab-dashboard') {
                updateLayoutPreview();
            } else if (tabId === 'tab-media') {
                loadMediaFiles();
            }
        }

        // Accordion functionality
        function toggleAccordion(element) {
            const item = element.parentElement;
            item.classList.toggle('open');
        }

        // Toast trigger
        function showToast(title, msg, type = 'info') {
            const container = document.getElementById('toastContainer');
            const toast = document.createElement('div');
            toast.className = `toast toast-${type}`;
            
            toast.innerHTML = `
                <div class="toast-content">
                    <div class="toast-title">${title}</div>
                    <div class="toast-msg">${msg}</div>
                </div>
                <button class="toast-close" onclick="this.parentElement.remove()">&times;</button>
            `;
            
            container.appendChild(toast);
            setTimeout(() => {
                toast.classList.add('hide');
                setTimeout(() => toast.remove(), 300);
            }, 5000);
        }

        // Fetch current config
        async function fetchConfig() {
            try {
                populateKeySelectors();
                const res = await fetch('/api/config');
                fullConfig = await res.json();
                
                populateFormFields();
                updateLayoutPreview();
                savedConfigString = JSON.stringify(getCurrentUIConfig());
            } catch (err) {
                showToast('Failed Connection', 'Could not read settings from NUC display engine.', 'error');
            }
        }

        function populateFormFields() {
            if (!fullConfig) return;

            if (fullConfig.videos) {
                fullConfig.videos.forEach((v, idx) => {
                    v.lock_aspect = localStorage.getItem(`lock_aspect_videos_${idx}`) === 'true';
                });
            }
            if (fullConfig.cameras) {
                fullConfig.cameras.forEach((c, idx) => {
                    c.lock_aspect = localStorage.getItem(`lock_aspect_cameras_${idx}`) === 'true';
                });
            }

            // Stats
            document.getElementById('statsLocation').textContent = fullConfig.location.name || 'N/A';
            document.getElementById('statsVideos').textContent = `${(fullConfig.videos || []).filter(v => v.enabled).length} Enabled`;
            document.getElementById('statsCameras').textContent = `${(fullConfig.cameras || []).filter(c => c.enabled).length} Connected`;
            document.getElementById('statsStocks').textContent = `${(fullConfig.stocks || []).length} Tracked`;

            // Location
            document.getElementById('locName').value = fullConfig.location.name || '';
            document.getElementById('locLat').value = fullConfig.location.lat || 0;
            document.getElementById('locLon').value = fullConfig.location.lon || 0;

            // Power save
            document.getElementById('psEnabled').checked = fullConfig.power_save.enabled || false;
            document.getElementById('psStart').value = fullConfig.power_save.start_time || '23:00';
            document.getElementById('psEnd').value = fullConfig.power_save.end_time || '07:00';
            togglePowerSaveFields();

            // News enabled
            document.getElementById('newsEnabled').checked = (fullConfig.news && fullConfig.news.enabled !== undefined) ? fullConfig.news.enabled : true;

            // Populate list sections
            renderStocksList();
            renderNewsList();
            renderVideosAccordion();
            renderCamerasList();
            renderLayoutLayersList();
            loadMediaFiles();

            // Populate Key selectors
            document.getElementById('keyHideVideos').value = fullConfig.global_keys.hide_videos || '';
            document.getElementById('keyNextStock').value = (fullConfig.stock_keys && fullConfig.stock_keys.next_stock) || '';
            document.getElementById('keyPrevStock').value = (fullConfig.stock_keys && fullConfig.stock_keys.prev_stock) || '';
            document.getElementById('keyNextChart').value = (fullConfig.stock_keys && fullConfig.stock_keys.next_chart) || '';
            document.getElementById('keyPrevChart').value = (fullConfig.stock_keys && fullConfig.stock_keys.prev_chart) || '';
        }

        // Toggle Power save input fields opacity
        function togglePowerSaveFields() {
            const enabled = document.getElementById('psEnabled').checked;
            const psTimes = document.getElementById('psTimes');
            psTimes.style.opacity = enabled ? '1' : '0.35';
            psTimes.querySelectorAll('input').forEach(i => i.disabled = !enabled);
        }

        // --- STOCKS LIST COMPONENT ---
        function renderStocksList() {
            const container = document.getElementById('stocksList');
            container.innerHTML = '';
            if (fullConfig.stocks) {
                fullConfig.stocks.forEach((stock, index) => {
                    const row = document.createElement('div');
                    row.className = 'list-item';
                    row.innerHTML = `
                        <input type="text" placeholder="Symbol" value="${stock.symbol}" oninput="updateStock(${index}, 'symbol', this.value)" style="flex: 1.5; font-weight:600;">
                        <input type="text" placeholder="Name" value="${stock.name}" oninput="updateStock(${index}, 'name', this.value)" style="flex: 2;">
                        <select onchange="updateStock(${index}, 'currency_symbol', this.value)" style="flex: 1;">
                            <option value="$" ${stock.currency_symbol==='$'?'selected':''}>$ (USD)</option>
                            <option value="€" ${stock.currency_symbol==='€'?'selected':''}>€ (EUR)</option>
                            <option value="£" ${stock.currency_symbol==='£'?'selected':''}>£ (GBP)</option>
                            <option value="₹" ${stock.currency_symbol==='₹'?'selected':''}>₹ (INR)</option>
                            <option value="¥" ${stock.currency_symbol==='¥'?'selected':''}>¥ (JPY/CNY)</option>
                            <option value="₩" ${stock.currency_symbol==='₩'?'selected':''}>₩ (KRW)</option>
                        </select>
                        <button class="btn btn-danger btn-small" onclick="deleteStock(${index})" style="padding: 0.6rem 0.8rem;">Remove</button>
                    `;
                    container.appendChild(row);
                });
            }
        }

        function updateStock(idx, field, val) {
            fullConfig.stocks[idx][field] = val;
        }

        function addStockItem() {
            if (!fullConfig.stocks) fullConfig.stocks = [];
            fullConfig.stocks.push({ symbol: '', name: '', currency_symbol: '$' });
            renderStocksList();
        }

        function deleteStock(idx) {
            fullConfig.stocks.splice(idx, 1);
            renderStocksList();
        }

        // --- RSS NEWS SOURCES ---
        function renderNewsList() {
            const container = document.getElementById('newsList');
            container.innerHTML = '';
            if (fullConfig.news && fullConfig.news.sources) {
                fullConfig.news.sources.forEach((source, index) => {
                    const row = document.createElement('div');
                    row.className = 'list-item';
                    row.innerHTML = `
                        <input type="text" placeholder="RSS Feed XML URL" value="${source}" oninput="updateNewsSource(${index}, this.value)" style="flex: 1;">
                        <button class="btn btn-danger btn-small" onclick="deleteNewsSource(${index})" style="padding: 0.6rem 0.8rem;">Remove</button>
                    `;
                    container.appendChild(row);
                });
            }
        }

        function updateNewsSource(idx, val) {
            fullConfig.news.sources[idx] = val;
        }

        function addNewsFeedItem() {
            if (!fullConfig.news) fullConfig.news = { enabled: true, sources: [] };
            if (!fullConfig.news.sources) fullConfig.news.sources = [];
            fullConfig.news.sources.push('');
            renderNewsList();
        }

        function deleteNewsSource(idx) {
            fullConfig.news.sources.splice(idx, 1);
            renderNewsList();
        }

        // --- VIDEOS DECODERS ---
        function renderVideosAccordion() {
            const container = document.getElementById('videoAccordionContainer');
            container.innerHTML = '';
            if (fullConfig.videos) {
                fullConfig.videos.forEach((v, index) => {
                    const el = document.createElement('div');
                    el.className = 'accordion-item';
                    el.id = `video-accordion-${index}`;
                    
                    const pathsListHTML = (v.playlists || []).map((path, pIdx) => `
                        <div class="list-item" style="margin-bottom:0.4rem;">
                            <input type="text" placeholder="e.g. tests/sample.mp4" value="${path}" oninput="updateVideoPlaylistPath(${index}, ${pIdx}, this.value)" style="flex:1;">
                            <button class="btn btn-secondary btn-small" onclick="openBrowseModal(${index}, ${pIdx})">Browse</button>
                            <button class="btn btn-secondary btn-small" onclick="resizeToVideoSize(${index}, ${pIdx})">Match Size</button>
                            <button class="btn btn-danger btn-small" onclick="deleteVideoPlaylistPath(${index}, ${pIdx})">&times;</button>
                        </div>
                    `).join('');

                    // Build dropdown selections for video keys
                    const keysSelectsHTML = ['next', 'prev', 'skip_forward', 'skip_backward'].map(k => {
                        const boundKey = (v.keys && v.keys[k]) || '';
                        let opts = VALID_KEYS.map(key => `
                            <option value="${key}" ${boundKey===key?'selected':''}>${key===''?'None (Auto)':key.toUpperCase()}</option>
                        `).join('');
                        return `
                            <div class="form-group">
                                <label style="text-transform: capitalize;">${k.replace('_', ' ')} Key</label>
                                <select onchange="updateVideoKey(${index}, '${k}', this.value)">${opts}</select>
                            </div>
                        `;
                    }).join('');

                    // Trigger trigger select
                    let triggerOpts = VALID_KEYS.map(key => `
                        <option value="${key===''?'auto':key}" ${(v.start_trigger === key || (key==='' && v.start_trigger==='auto'))?'selected':''}>${key===''?'Auto Play':key.toUpperCase()}</option>
                    `).join('');

                    el.innerHTML = `
                        <div class="accordion-header" onclick="toggleAccordion(this)">
                            <div class="accordion-title-block">
                                <span class="badge ${v.enabled?'badge-active':'badge-inactive'}">${v.enabled?'ENABLED':'DISABLED'}</span>
                                <h3>Video Player Slot #${index}</h3>
                            </div>
                            <svg class="accordion-arrow" width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><polyline points="6 9 12 15 18 9"></polyline></svg>
                        </div>
                        <div class="accordion-content">
                            <div class="toggle-container" style="background:rgba(255,255,255,0.015); margin-bottom:1.5rem;">
                                <label for="vEnabled-${index}" style="font-weight:600;">Enable Video Player Region</label>
                                <label class="switch">
                                    <input type="checkbox" id="vEnabled-${index}" ${v.enabled?'checked':''} onchange="updateVideoBool(${index}, 'enabled', this.checked)">
                                    <span class="slider"></span>
                                </label>
                            </div>

                            <div class="grid-2">
                                <div>
                                    <div class="toggle-container" style="background:rgba(255,255,255,0.015);">
                                        <label for="vAudio-${index}">Enable Audio Output</label>
                                        <label class="switch">
                                            <input type="checkbox" id="vAudio-${index}" ${v.audio_enabled?'checked':''} onchange="updateVideoBool(${index}, 'audio_enabled', this.checked)">
                                            <span class="slider"></span>
                                        </label>
                                    </div>
                                    
                                    <div class="form-group">
                                        <label>Audio Hardware Device Name</label>
                                        <input type="text" value="${v.audio_device || 'default'}" oninput="updateVideoString(${index}, 'audio_device', this.value)">
                                    </div>

                                    <div class="form-group">
                                        <label>Playlist Load/Start Trigger</label>
                                        <div style="display: flex; gap: 0.5rem; align-items: center;">
                                            <select onchange="updateVideoString(${index}, 'start_trigger', this.value)" style="flex: 1;">${triggerOpts}</select>
                                            <button class="btn btn-cyan btn-small" onclick="triggerPlayStop(${index})" style="white-space: nowrap; height: 38px;">Play / Stop Toggle</button>
                                        </div>
                                    </div>

                                    <div class="form-group" style="margin-top:1.5rem;">
                                        <div style="display:flex; justify-content:space-between; align-items:center; margin-bottom:0.5rem;">
                                            <label>Playlist Media Paths</label>
                                            <button class="btn btn-secondary btn-small" onclick="addVideoPlaylistPath(${index})">+ Add Path</button>
                                        </div>
                                        <div id="videoPathsList-${index}">${pathsListHTML}</div>
                                    </div>
                                </div>

                                <div>
                                    <h4 style="font-size:0.85rem; text-transform:uppercase; letter-spacing:0.5px; color:var(--text-secondary); margin-bottom:1rem;">Target Layout Rect Coordinates</h4>
                                    
                                    ${renderCoordSliders(index, 'videos', v)}
                                    
                                    <div class="toggle-container" style="background:rgba(255,255,255,0.015); margin-top:0.5rem; margin-bottom:0.5rem; padding: 0.5rem 1rem; border-radius: 8px;">
                                        <label for="vLockAspect-${index}" style="font-size:0.85rem; color:var(--text-secondary);">Lock Aspect Ratio</label>
                                        <label class="switch" style="transform: scale(0.8);">
                                            <input type="checkbox" id="vLockAspect-${index}" ${v.lock_aspect ? 'checked' : ''} onchange="updateLockAspect('videos', ${index}, this.checked)">
                                            <span class="slider"></span>
                                        </label>
                                    </div>
                                    
                                    <h4 style="font-size:0.85rem; text-transform:uppercase; letter-spacing:0.5px; color:var(--text-secondary); margin: 1.5rem 0 1rem 0;">Source Media Crop Region</h4>
                                    
                                    ${renderCropCoordSliders(index, 'videos', v)}

                                    <button class="btn btn-secondary btn-small" onclick="openCropEditor('videos', ${index})" style="margin-top: 1rem; width: 100%; gap: 0.4rem;">
                                        <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M6.13 1L6 16a2 2 0 0 0 2 2h15"></path><path d="M1 6.13L16 6a2 2 0 0 1 2 2v15"></path></svg>
                                        Open Visual Crop Editor
                                    </button>
                                </div>
                            </div>

                            <div style="margin-top:1.5rem; border-top: 1px solid var(--border-color); padding-top:1.5rem;">
                                <h4 style="font-size:0.85rem; text-transform:uppercase; letter-spacing:0.5px; color:var(--text-secondary); margin-bottom:1rem;">Keyboard Control Overrides</h4>
                                <div class="grid-4">${keysSelectsHTML}</div>
                            </div>

                            <div style="margin-top:2rem; display:flex; justify-content:flex-end;">
                                <button class="btn btn-danger btn-small" onclick="deleteVideoDecoder(${index})">Delete Video Decoder</button>
                            </div>
                        </div>
                    `;
                    container.appendChild(el);
                });
            }
        }

        function renderCoordSliders(idx, type, obj) {
            return ['x', 'y', 'w', 'h'].map(c => `
                <div class="slider-control-group">
                    <div class="slider-header">
                        <span style="text-transform:uppercase; font-weight:700;">${c} Coordinate</span>
                        <span>Range: 0.0 - 1.0</span>
                    </div>
                    <div class="slider-row">
                        <input type="range" min="0" max="1" step="0.01" value="${obj[c] || 0}" oninput="updateCoordSlider('${type}', ${idx}, '${c}', parseFloat(this.value))">
                        <span id="${type}-${idx}-val-${c}">${(obj[c] || 0).toFixed(2)}</span>
                    </div>
                </div>
            `).join('');
        }

        function renderCropCoordSliders(idx, type, obj) {
            return ['src_x', 'src_y', 'src_w', 'src_h'].map(c => `
                <div class="slider-control-group">
                    <div class="slider-header">
                        <span style="text-transform:uppercase; font-weight:700;">Crop ${c.replace('src_', '')}</span>
                    </div>
                    <div class="slider-row">
                        <input type="range" min="0" max="1" step="0.01" value="${obj[c] !== undefined ? obj[c] : 1}" oninput="updateCoordSlider('${type}', ${idx}, '${c}', parseFloat(this.value))">
                        <span id="${type}-${idx}-val-${c}">${(obj[c] !== undefined ? obj[c] : 1).toFixed(2)}</span>
                    </div>
                </div>
            `).join('');
        }

        function updateVideoBool(idx, field, checked) {
            fullConfig.videos[idx][field] = checked;
            // Update accordion status indicator immediately
            const accordion = document.getElementById(`video-accordion-${idx}`);
            if (accordion) {
                const badge = accordion.querySelector('.accordion-header .badge');
                if (field === 'enabled') {
                    if (checked) {
                        badge.className = 'badge badge-active';
                        badge.textContent = 'ENABLED';
                    } else {
                        badge.className = 'badge badge-inactive';
                        badge.textContent = 'DISABLED';
                    }
                }
            }
            updateLayoutPreview();
        }

        function updateVideoString(idx, field, val) {
            fullConfig.videos[idx][field] = val;
        }

        function updateVideoKey(idx, keyType, val) {
            if (!fullConfig.videos[idx].keys) fullConfig.videos[idx].keys = {};
            if (val === '') {
                delete fullConfig.videos[idx].keys[keyType];
            } else {
                fullConfig.videos[idx].keys[keyType] = val;
            }
        }

        function updateCoordSlider(type, idx, coord, val) {
            fullConfig[type][idx][coord] = val;
            document.getElementById(`${type}-${idx}-val-${coord}`).textContent = val.toFixed(2);
            updateLayoutPreview();
        }

        function updateVideoPlaylistPath(vIdx, pIdx, val) {
            fullConfig.videos[vIdx].playlists[pIdx] = val;
        }

        function addVideoPlaylistPath(vIdx) {
            if (!fullConfig.videos[vIdx].playlists) fullConfig.videos[vIdx].playlists = [];
            fullConfig.videos[vIdx].playlists.push('');
            renderVideosAccordion();
            // keep the accordion open
            document.getElementById(`video-accordion-${vIdx}`).classList.add('open');
        }

        function deleteVideoPlaylistPath(vIdx, pIdx) {
            fullConfig.videos[vIdx].playlists.splice(pIdx, 1);
            renderVideosAccordion();
            document.getElementById(`video-accordion-${vIdx}`).classList.add('open');
        }

        function addVideoDecoder() {
            if (!fullConfig.videos) fullConfig.videos = [];
            fullConfig.videos.push({
                enabled: true,
                audio_enabled: false,
                audio_device: 'default',
                playlists: [],
                x: 0.0, y: 0.0, w: 0.5, h: 0.5,
                src_x: 0.0, src_y: 0.0, src_w: 1.0, src_h: 1.0,
                start_trigger: 'auto',
                keys: {}
            });
            // Auto add to layout list
            const newIdx = fullConfig.videos.length - 1;
            fullConfig.layout.push({ type: 'video', video_index: newIdx });

            renderVideosAccordion();
            renderLayoutLayersList();
            updateLayoutPreview();
            
            // Open the newly added decoder panel
            const accordionItems = document.querySelectorAll('#videoAccordionContainer .accordion-item');
            if (accordionItems.length > 0) {
                accordionItems[accordionItems.length - 1].classList.add('open');
            }
        }

        function deleteVideoDecoder(idx) {
            fullConfig.videos.splice(idx, 1);
            // Remove matching layout layer and update indices
            fullConfig.layout = fullConfig.layout.filter(layer => {
                if (layer.type === 'video') {
                    if (layer.video_index === idx) return false; // Delete layout entry
                    if (layer.video_index > idx) layer.video_index--; // Adjust index down
                }
                return true;
            });
            renderVideosAccordion();
            renderLayoutLayersList();
            updateLayoutPreview();
        }

        // --- CAMERA CAPTURES ---
        function renderCamerasList() {
            const container = document.getElementById('camerasListContainer');
            container.innerHTML = '';
            if (fullConfig.cameras) {
                fullConfig.cameras.forEach((cam, index) => {
                    const row = document.createElement('div');
                    row.className = 'glass-panel';
                    row.style.background = 'rgba(255,255,255,0.01)';
                    row.style.marginBottom = '1.5rem';
                    
                    row.innerHTML = `
                        <div style="display:flex; justify-content:space-between; align-items:center; border-bottom:1px solid var(--border-color); padding-bottom:0.75rem; margin-bottom:1.25rem;">
                            <div style="display:flex; align-items:center; gap:0.5rem;">
                                <span class="badge ${cam.enabled?'badge-active':'badge-inactive'}">${cam.enabled?'ACTIVE':'INACTIVE'}</span>
                                <h3 style="font-size:0.95rem; font-weight:600;">Camera Stream Slot #${index}</h3>
                            </div>
                            <button class="btn btn-danger btn-small" onclick="deleteCameraInput(${index})">Remove Camera</button>
                        </div>

                        <div class="toggle-container" style="background:rgba(255,255,255,0.015);">
                            <label for="cEnabled-${index}" style="font-weight:600;">Enable Camera Streaming</label>
                            <label class="switch">
                                <input type="checkbox" id="cEnabled-${index}" ${cam.enabled?'checked':''} onchange="updateCameraBool(${index}, 'enabled', this.checked)">
                                <span class="slider"></span>
                            </label>
                        </div>

                        <div class="grid-2">
                            <div>
                                <div class="form-group">
                                    <label>Linux V4L2 Device Path</label>
                                    <input type="text" placeholder="e.g. /dev/video0" value="${cam.device || ''}" oninput="updateCameraString(${index}, 'device', this.value)">
                                </div>

                                <div class="form-row">
                                    <div class="form-group">
                                        <label>Capture Width</label>
                                        <input type="number" value="${cam.width || 640}" oninput="updateCameraInt(${index}, 'width', this.value)">
                                    </div>
                                    <div class="form-group">
                                        <label>Capture Height</label>
                                        <input type="number" value="${cam.height || 480}" oninput="updateCameraInt(${index}, 'height', this.value)">
                                    </div>
                                </div>

                                <div class="form-row">
                                    <div class="form-group">
                                        <label>Frame rate (FPS)</label>
                                        <input type="number" value="${cam.fps || 30}" oninput="updateCameraInt(${index}, 'fps', this.value)">
                                    </div>
                                    <div class="form-group">
                                        <label>Pixel Stream Format</label>
                                        <select onchange="updateCameraString(${index}, 'pixel_format', this.value)">
                                            <option value="MJPG" ${cam.pixel_format==='MJPG'?'selected':''}>MJPEG Compressed (MJPG)</option>
                                            <option value="YUYV" ${cam.pixel_format==='YUYV'?'selected':''}>YUYV 4:2:2 Raw (YUYV)</option>
                                            <option value="NV12" ${cam.pixel_format==='NV12'?'selected':''}>NV12 Planar YUV (NV12)</option>
                                        </select>
                                    </div>
                                </div>
                            </div>

                            <div>
                                <h4 style="font-size:0.85rem; text-transform:uppercase; letter-spacing:0.5px; color:var(--text-secondary); margin-bottom:1rem;">Screen Destination Coordinates</h4>
                                ${renderCoordSliders(index, 'cameras', cam)}
                                
                                <div class="toggle-container" style="background:rgba(255,255,255,0.015); margin-top:0.5rem; margin-bottom:0.5rem; padding: 0.5rem 1rem; border-radius: 8px;">
                                    <label for="cLockAspect-${index}" style="font-size:0.85rem; color:var(--text-secondary);">Lock Aspect Ratio</label>
                                    <label class="switch" style="transform: scale(0.8);">
                                        <input type="checkbox" id="cLockAspect-${index}" ${cam.lock_aspect ? 'checked' : ''} onchange="updateLockAspect('cameras', ${index}, this.checked)">
                                        <span class="slider"></span>
                                    </label>
                                </div>
                                
                                <h4 style="font-size:0.85rem; text-transform:uppercase; letter-spacing:0.5px; color:var(--text-secondary); margin:1.5rem 0 1rem 0;">Source Sensor Crop Rect</h4>
                                ${renderCropCoordSliders(index, 'cameras', cam)}

                                <button class="btn btn-secondary btn-small" onclick="openCropEditor('cameras', ${index})" style="margin-top: 1rem; width: 100%; gap: 0.4rem;">
                                    <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M6.13 1L6 16a2 2 0 0 0 2 2h15"></path><path d="M1 6.13L16 6a2 2 0 0 1 2 2v15"></path></svg>
                                    Open Visual Crop Editor
                                </button>
                            </div>
                        </div>
                    `;
                    container.appendChild(row);
                });
            }
        }

        function updateCameraBool(idx, field, checked) {
            fullConfig.cameras[idx][field] = checked;
            renderCamerasList();
            updateLayoutPreview();
        }

        function updateCameraString(idx, field, val) {
            fullConfig.cameras[idx][field] = val;
        }

        function updateCameraInt(idx, field, val) {
            fullConfig.cameras[idx][field] = parseInt(val) || 0;
        }

        function addCameraInput() {
            if (!fullConfig.cameras) fullConfig.cameras = [];
            fullConfig.cameras.push({
                enabled: true,
                device: '/dev/video0',
                width: 640, height: 480, fps: 30,
                pixel_format: 'MJPG',
                x: 0.1, y: 0.1, w: 0.4, h: 0.4,
                src_x: 0.0, src_y: 0.0, src_w: 1.0, src_h: 1.0
            });
            // Auto add to layout list
            const newIdx = fullConfig.cameras.length - 1;
            fullConfig.layout.push({ type: 'camera', camera_index: newIdx });

            renderCamerasList();
            renderLayoutLayersList();
            updateLayoutPreview();
        }

        function deleteCameraInput(idx) {
            fullConfig.cameras.splice(idx, 1);
            // Remove matching layout layer and update indices
            fullConfig.layout = fullConfig.layout.filter(layer => {
                if (layer.type === 'camera') {
                    if (layer.camera_index === idx) return false;
                    if (layer.camera_index > idx) layer.camera_index--;
                }
                return true;
            });
            renderCamerasList();
            renderLayoutLayersList();
            updateLayoutPreview();
        }

        // --- LAYOUT LAYERS LIST REORDER ---
        function renderLayoutLayersList() {
            const container = document.getElementById('layoutLayersContainer');
            container.innerHTML = '';
            if (fullConfig.layout) {
                fullConfig.layout.forEach((layer, index) => {
                    const row = document.createElement('div');
                    row.className = 'layout-list-item';
                    
                    let label = '';
                    let badgeClass = '';
                    if (layer.type === 'weather') {
                        label = 'Weather Conditions Block';
                        badgeClass = 'badge-weather';
                    } else if (layer.type === 'stocks') {
                        label = 'Financial Stocks Tickers Grid';
                        badgeClass = 'badge-stocks';
                    } else if (layer.type === 'news') {
                        label = 'Scrolling Headlines Banner';
                        badgeClass = 'badge-news';
                    } else if (layer.type === 'video') {
                        label = `Video Player Slot #${layer.video_index}`;
                        badgeClass = 'badge-video';
                    } else if (layer.type === 'camera') {
                        label = `Camera Hardware Input #${layer.camera_index}`;
                        badgeClass = 'badge-camera';
                    }

                    row.innerHTML = `
                        <div class="layout-list-info">
                            <span class="layout-type-badge ${badgeClass}">${layer.type}</span>
                            <span style="font-weight:600; font-size:0.9rem;">${label}</span>
                        </div>
                        <div class="layout-actions">
                            <button class="layout-btn" onclick="moveLayer(${index}, -1)" ${index===0?'disabled':''} title="Move Layer Down (Backwards)">
                                <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5" stroke-linecap="round" stroke-linejoin="round"><polyline points="18 15 12 9 6 15"></polyline></svg>
                            </button>
                            <button class="layout-btn" onclick="moveLayer(${index}, 1)" ${index===fullConfig.layout.length-1?'disabled':''} title="Move Layer Up (Forwards)">
                                <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5" stroke-linecap="round" stroke-linejoin="round"><polyline points="6 9 12 15 18 9"></polyline></svg>
                            </button>
                        </div>
                    `;
                    container.appendChild(row);
                });
            }
        }

        function moveLayer(index, dir) {
            const temp = fullConfig.layout[index];
            fullConfig.layout[index] = fullConfig.layout[index + dir];
            fullConfig.layout[index + dir] = temp;
            renderLayoutLayersList();
            updateLayoutPreview();
        }

        // --- DRAW INTERACTIVE DISPLAY PREVIEW ---
        function updateLayoutPreview() {
            if (!fullConfig) return;
            const container = document.getElementById('mockupLayersContainer');
            container.innerHTML = '';

            // Mode selector visibility
            const interactionSel = document.getElementById('interactionModeSelector');
            if (selectedLayer && (selectedLayer.type === 'video' || selectedLayer.type === 'camera')) {
                if (interactionSel) interactionSel.style.display = 'flex';
                const btnLayout = document.getElementById('modeBtnLayout');
                const btnCrop = document.getElementById('modeBtnCrop');
                if (btnLayout && btnCrop) {
                    if (interactionMode === 'layout') {
                        btnLayout.className = 'btn btn-small';
                        btnCrop.className = 'btn btn-secondary btn-small';
                    } else {
                        btnLayout.className = 'btn btn-secondary btn-small';
                        btnCrop.className = 'btn btn-small';
                    }
                }
            } else {
                interactionMode = 'layout';
                if (interactionSel) interactionSel.style.display = 'none';
            }

            // Draw order: first = behind, last = on top.
            fullConfig.layout.forEach((layer) => {
                let div = document.createElement('div');
                div.className = 'mockup-layer';

                if (layer.type === 'weather') {
                    div.classList.add('mockup-layer-weather');
                    div.style.left = '3%';
                    div.style.top = '3%';
                    div.style.width = '37%';
                    div.style.height = '75%';
                    div.innerHTML = `<span>Weather</span>`;
                    container.appendChild(div);
                } 
                else if (layer.type === 'news') {
                    const newsEnabled = document.getElementById('newsEnabled').checked;
                    if (newsEnabled) {
                        div.classList.add('mockup-layer-news');
                        div.style.left = '3%';
                        div.style.top = '80%';
                        div.style.width = '37%';
                        div.style.height = '17%';
                        div.innerHTML = `<span>News</span>`;
                        container.appendChild(div);
                    }
                } 
                else if (layer.type === 'stocks') {
                    div.classList.add('mockup-layer-stocks');
                    div.style.left = '42%';
                    div.style.top = '3%';
                    div.style.width = '55%';
                    div.style.height = '94%';
                    div.innerHTML = `<span>Stocks</span>`;
                    container.appendChild(div);
                } 
                else if (layer.type === 'video') {
                    const v = fullConfig.videos[layer.video_index];
                    if (v && v.enabled) {
                        div.classList.add('mockup-layer-video');
                        div.style.left = `${v.x * 100}%`;
                        div.style.top = `${v.y * 100}%`;
                        div.style.width = `${v.w * 100}%`;
                        div.style.height = `${v.h * 100}%`;
                        div.innerHTML = `<span>Video ${layer.video_index}</span>`;
                        
                        if (selectedLayer && selectedLayer.type === 'video' && selectedLayer.index === layer.video_index) {
                            div.classList.add('selected');
                            if (interactionMode === 'layout') {
                                const handle = document.createElement('div');
                                handle.className = 'resize-handle';
                                div.appendChild(handle);
                            } else if (interactionMode === 'crop') {
                                const cropBox = document.createElement('div');
                                cropBox.className = 'inner-crop-box';
                                cropBox.style.position = 'absolute';
                                cropBox.style.left = `${(v.src_x !== undefined ? v.src_x : 0) * 100}%`;
                                cropBox.style.top = `${(v.src_y !== undefined ? v.src_y : 0) * 100}%`;
                                cropBox.style.width = `${(v.src_w !== undefined ? v.src_w : 1) * 100}%`;
                                cropBox.style.height = `${(v.src_h !== undefined ? v.src_h : 1) * 100}%`;
                                cropBox.style.border = '2px dashed #ffa500';
                                cropBox.style.background = 'rgba(255, 165, 0, 0.15)';
                                cropBox.style.boxShadow = '0 0 8px rgba(255, 165, 0, 0.4)';
                                cropBox.style.cursor = 'move';
                                cropBox.style.zIndex = '1002';
                                cropBox.innerHTML = `<span style="position: absolute; top: 2px; left: 4px; font-size: 0.6rem; color: #ffa500; font-weight: bold; background: rgba(0,0,0,0.6); padding: 1px 3px; border-radius: 2px; pointer-events: none;">Crop</span>`;
                                
                                const cropHandle = document.createElement('div');
                                cropHandle.className = 'crop-resize-handle';
                                cropHandle.style.position = 'absolute';
                                cropHandle.style.right = '0';
                                cropHandle.style.bottom = '0';
                                cropHandle.style.width = '10px';
                                cropHandle.style.height = '10px';
                                cropHandle.style.background = '#ffa500';
                                cropHandle.style.cursor = 'se-resize';
                                cropHandle.style.boxShadow = '0 0 4px rgba(255, 165, 0, 0.6)';
                                cropBox.appendChild(cropHandle);
                                div.appendChild(cropBox);
                            }
                        }
                        
                        div.addEventListener('click', (e) => {
                            if (!e.defaultPrevented) {
                                switchTab('tab-videos');
                                const acc = document.getElementById(`video-accordion-${layer.video_index}`);
                                if (acc) acc.classList.add('open');
                            }
                        });

                        setupLayerInteractions(div, container, layer, v);
                        container.appendChild(div);
                    }
                } 
                else if (layer.type === 'camera') {
                    const c = fullConfig.cameras[layer.camera_index];
                    if (c && c.enabled) {
                        div.classList.add('mockup-layer-camera');
                        div.style.left = `${c.x * 100}%`;
                        div.style.top = `${c.y * 100}%`;
                        div.style.width = `${c.w * 100}%`;
                        div.style.height = `${c.h * 100}%`;
                        div.innerHTML = `<span>Camera ${layer.camera_index}</span>`;
                        
                        if (selectedLayer && selectedLayer.type === 'camera' && selectedLayer.index === layer.camera_index) {
                            div.classList.add('selected');
                            if (interactionMode === 'layout') {
                                const handle = document.createElement('div');
                                handle.className = 'resize-handle';
                                div.appendChild(handle);
                            } else if (interactionMode === 'crop') {
                                const cropBox = document.createElement('div');
                                cropBox.className = 'inner-crop-box';
                                cropBox.style.position = 'absolute';
                                cropBox.style.left = `${(c.src_x !== undefined ? c.src_x : 0) * 100}%`;
                                cropBox.style.top = `${(c.src_y !== undefined ? c.src_y : 0) * 100}%`;
                                cropBox.style.width = `${(c.src_w !== undefined ? c.src_w : 1) * 100}%`;
                                cropBox.style.height = `${(c.src_h !== undefined ? c.src_h : 1) * 100}%`;
                                cropBox.style.border = '2px dashed #ffa500';
                                cropBox.style.background = 'rgba(255, 165, 0, 0.15)';
                                cropBox.style.boxShadow = '0 0 8px rgba(255, 165, 0, 0.4)';
                                cropBox.style.cursor = 'move';
                                cropBox.style.zIndex = '1002';
                                cropBox.innerHTML = `<span style="position: absolute; top: 2px; left: 4px; font-size: 0.6rem; color: #ffa500; font-weight: bold; background: rgba(0,0,0,0.6); padding: 1px 3px; border-radius: 2px; pointer-events: none;">Crop</span>`;
                                
                                const cropHandle = document.createElement('div');
                                cropHandle.className = 'crop-resize-handle';
                                cropHandle.style.position = 'absolute';
                                cropHandle.style.right = '0';
                                cropHandle.style.bottom = '0';
                                cropHandle.style.width = '10px';
                                cropHandle.style.height = '10px';
                                cropHandle.style.background = '#ffa500';
                                cropHandle.style.cursor = 'se-resize';
                                cropHandle.style.boxShadow = '0 0 4px rgba(255, 165, 0, 0.6)';
                                cropBox.appendChild(cropHandle);
                                div.appendChild(cropBox);
                            }
                        }
                        
                        div.addEventListener('click', (e) => {
                            if (!e.defaultPrevented) {
                                switchTab('tab-cameras');
                            }
                        });

                        setupLayerInteractions(div, container, layer, c);
                        container.appendChild(div);
                    }
                }
            });
        }

        function setupLayerInteractions(div, container, layer, item) {
            const isSelected = selectedLayer && selectedLayer.type === layer.type && selectedLayer.index === (layer.type === 'video' ? layer.video_index : layer.camera_index);
            
            // If in crop mode and this layer is selected, set up crop box interactions
            if (isSelected && interactionMode === 'crop') {
                const cropBox = div.querySelector('.inner-crop-box');
                const cropHandle = div.querySelector('.crop-resize-handle');
                if (cropBox && cropHandle) {
                    cropBox.addEventListener('mousedown', (e) => {
                        e.stopPropagation();
                        e.preventDefault();
                        
                        let isResizing = (e.target === cropHandle);
                        let isDragging = !isResizing;
                        
                        const rect = div.getBoundingClientRect();
                        const startX = e.clientX;
                        const startY = e.clientY;
                        
                        const currentX = item.src_x !== undefined ? item.src_x : 0;
                        const currentY = item.src_y !== undefined ? item.src_y : 0;
                        const currentW = item.src_w !== undefined ? item.src_w : 1;
                        const currentH = item.src_h !== undefined ? item.src_h : 1;
                        
                        const startLeft = currentX * rect.width;
                        const startTop = currentY * rect.height;
                        const startWidth = currentW * rect.width;
                        const startHeight = currentH * rect.height;
                        
                        let moved = false;
                        
                        const onMouseMove = (moveEv) => {
                            moved = true;
                            const dx = moveEv.clientX - startX;
                            const dy = moveEv.clientY - startY;
                            const currentRect = div.getBoundingClientRect();
                            
                            if (isDragging) {
                                let newLeft = startLeft + dx;
                                let newTop = startTop + dy;
                                
                                newLeft = Math.max(0, Math.min(currentRect.width - startWidth, newLeft));
                                newTop = Math.max(0, Math.min(currentRect.height - startHeight, newTop));
                                
                                item.src_x = parseFloat((newLeft / currentRect.width).toFixed(2));
                                item.src_y = parseFloat((newTop / currentRect.height).toFixed(2));
                            } else if (isResizing) {
                                let newWidth = startWidth + dx;
                                let newHeight = startHeight + dy;
                                
                                const lock = moveEv.shiftKey || item.lock_aspect;
                                if (lock) {
                                    const aspectRatio = startWidth / startHeight;
                                    if (Math.abs(dx) > Math.abs(dy)) {
                                        newHeight = newWidth / aspectRatio;
                                        
                                        const minH = currentRect.height * 0.05;
                                        const maxH = currentRect.height - startTop;
                                        if (newHeight < minH) {
                                            newHeight = minH;
                                            newWidth = newHeight * aspectRatio;
                                        } else if (newHeight > maxH) {
                                            newHeight = maxH;
                                            newWidth = newHeight * aspectRatio;
                                        }
                                        
                                        const minW = currentRect.width * 0.05;
                                        const maxW = currentRect.width - startLeft;
                                        if (newWidth < minW) {
                                            newWidth = minW;
                                            newHeight = newWidth / aspectRatio;
                                        } else if (newWidth > maxW) {
                                            newWidth = maxW;
                                            newHeight = newWidth / aspectRatio;
                                        }
                                    } else {
                                        newWidth = newHeight * aspectRatio;
                                        
                                        const minW = currentRect.width * 0.05;
                                        const maxW = currentRect.width - startLeft;
                                        if (newWidth < minW) {
                                            newWidth = minW;
                                            newHeight = newWidth / aspectRatio;
                                        } else if (newWidth > maxW) {
                                            newWidth = maxW;
                                            newHeight = newWidth / aspectRatio;
                                        }
                                        
                                        const minH = currentRect.height * 0.05;
                                        const maxH = currentRect.height - startTop;
                                        if (newHeight < minH) {
                                            newHeight = minH;
                                            newWidth = newHeight * aspectRatio;
                                        } else if (newHeight > maxH) {
                                            newHeight = maxH;
                                            newWidth = newHeight * aspectRatio;
                                        }
                                    }
                                } else {
                                    newWidth = Math.max(currentRect.width * 0.05, Math.min(currentRect.width - startLeft, newWidth));
                                    newHeight = Math.max(currentRect.height * 0.05, Math.min(currentRect.height - startTop, newHeight));
                                }
                                
                                item.src_w = parseFloat((newWidth / currentRect.width).toFixed(2));
                                item.src_h = parseFloat((newHeight / currentRect.height).toFixed(2));
                            }
                            
                            cropBox.style.left = `${item.src_x * 100}%`;
                            cropBox.style.top = `${item.src_y * 100}%`;
                            cropBox.style.width = `${item.src_w * 100}%`;
                            cropBox.style.height = `${item.src_h * 100}%`;
                            
                            updateUIFieldsForCrop(layer.type, selectedLayer.index);
                        };
                        
                        const onMouseUp = () => {
                            document.removeEventListener('mousemove', onMouseMove);
                            document.removeEventListener('mouseup', onMouseUp);
                        };
                        
                        document.addEventListener('mousemove', onMouseMove);
                        document.addEventListener('mouseup', onMouseUp);
                    });
                }
            }

            // Layout Mode interaction (dragging the layer or layout resize-handle)
            div.addEventListener('mousedown', (e) => {
                // Ignore layout moves if clicking inner-crop-box or crop-resize-handle
                if (e.target.closest('.inner-crop-box')) return;
                
                selectedLayer = { type: layer.type, index: layer.type === 'video' ? layer.video_index : layer.camera_index };
                
                const wasSelected = isSelected;
                if (!wasSelected) {
                    document.querySelectorAll('.mockup-layer').forEach(l => l.classList.remove('selected'));
                    document.querySelectorAll('.resize-handle').forEach(h => h.remove());
                    document.querySelectorAll('.inner-crop-box').forEach(b => b.remove());
                    
                    div.classList.add('selected');
                    if (interactionMode === 'layout') {
                        const handle = document.createElement('div');
                        handle.className = 'resize-handle';
                        div.appendChild(handle);
                    }
                    updateLayoutPreview();
                    return;
                }
                
                let isResizing = (e.target.classList.contains('resize-handle'));
                let isDragging = !isResizing;
                
                const rect = container.getBoundingClientRect();
                const startX = e.clientX;
                const startY = e.clientY;
                
                const startLeft = item.x * rect.width;
                const startTop = item.y * rect.height;
                const startWidth = item.w * rect.width;
                const startHeight = item.h * rect.height;
                
                let moved = false;
                
                const onMouseMove = (moveEv) => {
                    moved = true;
                    const dx = moveEv.clientX - startX;
                    const dy = moveEv.clientY - startY;
                    const currentRect = container.getBoundingClientRect();
                    
                    if (isDragging) {
                        let newLeft = startLeft + dx;
                        let newTop = startTop + dy;
                        
                        newLeft = Math.max(0, Math.min(currentRect.width - startWidth, newLeft));
                        newTop = Math.max(0, Math.min(currentRect.height - startHeight, newTop));
                        
                        item.x = parseFloat((newLeft / currentRect.width).toFixed(2));
                        item.y = parseFloat((newTop / currentRect.height).toFixed(2));
                    } else if (isResizing) {
                        let newWidth = startWidth + dx;
                        let newHeight = startHeight + dy;
                        
                        const lock = moveEv.shiftKey || item.lock_aspect;
                        if (lock) {
                            const aspectRatio = startWidth / startHeight;
                            if (Math.abs(dx) > Math.abs(dy)) {
                                newHeight = newWidth / aspectRatio;
                                
                                const minH = currentRect.height * 0.05;
                                const maxH = currentRect.height - startTop;
                                if (newHeight < minH) {
                                    newHeight = minH;
                                    newWidth = newHeight * aspectRatio;
                                } else if (newHeight > maxH) {
                                    newHeight = maxH;
                                    newWidth = newHeight * aspectRatio;
                                }
                                
                                const minW = currentRect.width * 0.05;
                                const maxW = currentRect.width - startLeft;
                                if (newWidth < minW) {
                                    newWidth = minW;
                                    newHeight = newWidth / aspectRatio;
                                } else if (newWidth > maxW) {
                                    newWidth = maxW;
                                    newHeight = newWidth / aspectRatio;
                                }
                            } else {
                                newWidth = newHeight * aspectRatio;
                                
                                const minW = currentRect.width * 0.05;
                                const maxW = currentRect.width - startLeft;
                                if (newWidth < minW) {
                                    newWidth = minW;
                                    newHeight = newWidth / aspectRatio;
                                } else if (newWidth > maxW) {
                                    newWidth = maxW;
                                    newHeight = newWidth / aspectRatio;
                                }
                                
                                const minH = currentRect.height * 0.05;
                                const maxH = currentRect.height - startTop;
                                if (newHeight < minH) {
                                    newHeight = minH;
                                    newWidth = newHeight * aspectRatio;
                                } else if (newHeight > maxH) {
                                    newHeight = maxH;
                                    newWidth = newHeight * aspectRatio;
                                }
                            }
                        } else {
                            newWidth = Math.max(currentRect.width * 0.05, Math.min(currentRect.width - startLeft, newWidth));
                            newHeight = Math.max(currentRect.height * 0.05, Math.min(currentRect.height - startTop, newHeight));
                        }
                        
                        item.w = parseFloat((newWidth / currentRect.width).toFixed(2));
                        item.h = parseFloat((newHeight / currentRect.height).toFixed(2));
                    }
                    
                    div.style.left = `${item.x * 100}%`;
                    div.style.top = `${item.y * 100}%`;
                    div.style.width = `${item.w * 100}%`;
                    div.style.height = `${item.h * 100}%`;
                    
                    updateUIFieldsForLayer(layer.type, selectedLayer.index);
                };
                
                const onMouseUp = (upEv) => {
                    document.removeEventListener('mousemove', onMouseMove);
                    document.removeEventListener('mouseup', onMouseUp);
                    
                    if (moved) {
                        upEv.preventDefault();
                        div.addEventListener('click', function captureClick(clickEv) {
                            clickEv.stopPropagation();
                            clickEv.preventDefault();
                            div.removeEventListener('click', captureClick, true);
                        }, true);
                    }
                };
                
                document.addEventListener('mousemove', onMouseMove);
                document.addEventListener('mouseup', onMouseUp);
            });
        }

        // Geocoding via Open-Meteo
        async function geocodeAddress() {
            const name = document.getElementById('locName').value;
            if (!name) {
                showToast('Validation Error', 'Please enter a target city name first.', 'error');
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
                    
                    showToast('Location Found', `Updated to coordinates for ${fullName}.`, 'success');
                } else {
                    showToast('Geocoding Failed', 'No matches found for that location query.', 'error');
                }
            } catch (err) {
                showToast('Geocoding Network Error', 'Failed to communicate with Open-Meteo.', 'error');
            }
        }

        // Save Config to Server
        async function saveConfig() {
            if (!fullConfig) return false;

            // Gather Location
            fullConfig.location.name = document.getElementById('locName').value;
            fullConfig.location.lat = parseFloat(document.getElementById('locLat').value);
            fullConfig.location.lon = parseFloat(document.getElementById('locLon').value);

            // Gather Power save
            fullConfig.power_save.enabled = document.getElementById('psEnabled').checked;
            fullConfig.power_save.start_time = document.getElementById('psStart').value;
            fullConfig.power_save.end_time = document.getElementById('psEnd').value;

            // Gather news enabled
            if (!fullConfig.news) fullConfig.news = { enabled: true, sources: [] };
            fullConfig.news.enabled = document.getElementById('newsEnabled').checked;

            // Gather Global Keys
            fullConfig.global_keys.hide_videos = document.getElementById('keyHideVideos').value || null;

            // Gather Stock Keys
            if (!fullConfig.stock_keys) fullConfig.stock_keys = {};
            fullConfig.stock_keys.next_stock = document.getElementById('keyNextStock').value || null;
            fullConfig.stock_keys.prev_stock = document.getElementById('keyPrevStock').value || null;
            fullConfig.stock_keys.next_chart = document.getElementById('keyNextChart').value || null;
            fullConfig.stock_keys.prev_chart = document.getElementById('keyPrevChart').value || null;

            // Clean collections to avoid blank items
            if (fullConfig.stocks) {
                fullConfig.stocks = fullConfig.stocks.filter(s => s.symbol.trim() !== '');
            }
            if (fullConfig.news && fullConfig.news.sources) {
                fullConfig.news.sources = fullConfig.news.sources.filter(src => src.trim() !== '');
            }
            if (fullConfig.videos) {
                fullConfig.videos.forEach(v => {
                    if (v.playlists) {
                        v.playlists = v.playlists.filter(p => p.trim() !== '');
                    }
                });
            }

            // Client-side key binding duplicate validations
            const keysToValidate = [];
            if (fullConfig.global_keys.hide_videos) keysToValidate.push({ name: 'Hide/Show Videos', key: fullConfig.global_keys.hide_videos });
            if (fullConfig.stock_keys.next_stock) keysToValidate.push({ name: 'Next Stock', key: fullConfig.stock_keys.next_stock });
            if (fullConfig.stock_keys.prev_stock) keysToValidate.push({ name: 'Prev Stock', key: fullConfig.stock_keys.prev_stock });
            if (fullConfig.stock_keys.next_chart) keysToValidate.push({ name: 'Next Chart', key: fullConfig.stock_keys.next_chart });
            if (fullConfig.stock_keys.prev_chart) keysToValidate.push({ name: 'Prev Chart', key: fullConfig.stock_keys.prev_chart });
            
            if (fullConfig.videos) {
                fullConfig.videos.forEach((v, index) => {
                    if (v.enabled) {
                        if (v.start_trigger && v.start_trigger !== 'auto') keysToValidate.push({ name: `Video ${index} Trigger`, key: v.start_trigger });
                        if (v.keys) {
                            if (v.keys.next) keysToValidate.push({ name: `Video ${index} Next`, key: v.keys.next });
                            if (v.keys.prev) keysToValidate.push({ name: `Video ${index} Prev`, key: v.keys.prev });
                            if (v.keys.skip_forward) keysToValidate.push({ name: `Video ${index} Skip Fwd`, key: v.keys.skip_forward });
                            if (v.keys.skip_backward) keysToValidate.push({ name: `Video ${index} Skip Bwd`, key: v.keys.skip_backward });
                        }
                    }
                });
            }

            const duplicates = {};
            keysToValidate.forEach(item => {
                if (item.key) {
                    if (!duplicates[item.key]) duplicates[item.key] = [];
                    duplicates[item.key].push(item.name);
                }
            });

            let dupErrors = [];
            for (const key in duplicates) {
                if (duplicates[key].length > 1) {
                    dupErrors.push(`Key "${key.toUpperCase()}" mapped to multiple: ${duplicates[key].join(', ')}`);
                }
            }

            if (dupErrors.length > 0) {
                showToast('Duplicate Binding Alert', dupErrors.join('<br>'), 'error');
                return false;
            }

            try {
                const res = await fetch('/api/config', {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify(getCurrentUIConfig())
                });
                
                if (res.ok) {
                    showToast('Settings Saved', 'Configuration successfully updated and reloaded.', 'success');
                    await fetchConfig(); // Reload from disk to verify
                    return true;
                } else {
                    const data = await res.json();
                    if (data.errors && data.errors.length > 0) {
                        showToast('Validation Failed', 'The display server rejected configurations:<br>' + data.errors.map(e => `&bull; ${e}`).join('<br>'), 'error');
                    } else {
                        showToast('Server Error', 'Failed to write configurations to backend.', 'error');
                    }
                    return false;
                }
            } catch (err) {
                showToast('Connection Error', 'Network failed saving settings.', 'error');
                return false;
            }
        }

        // Send Key Command to Virtual Remote API
        async function sendControl(keyName) {
            try {
                const res = await fetch('/api/control', {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify({ key: keyName })
                });
                if (res.ok) {
                    showToast('Key Injected', `Successfully sent key press for "${keyName.toUpperCase()}".`, 'info');
                } else {
                    showToast('Keystroke Refused', `Engine rejected key: "${keyName}"`, 'error');
                }
            } catch (err) {
                console.error('Failed to send control command:', err);
            }
        }

        function updateUIFieldsForLayer(type, idx) {
            const item = type === 'video' ? fullConfig.videos[idx] : fullConfig.cameras[idx];
            const typeStr = type === 'video' ? 'videos' : 'cameras';
            ['x', 'y', 'w', 'h'].forEach(c => {
                const slider = document.querySelector(`input[oninput*="updateCoordSlider('${typeStr}', ${idx}, '${c}'"]`);
                if (slider) {
                    slider.value = item[c];
                }
                const label = document.getElementById(`${typeStr}-${idx}-val-${c}`);
                if (label) {
                    label.textContent = item[c].toFixed(2);
                }
            });
        }

        function updateUIFieldsForCrop(type, idx) {
            const item = type === 'video' ? fullConfig.videos[idx] : fullConfig.cameras[idx];
            const typeStr = type === 'video' ? 'videos' : 'cameras';
            ['src_x', 'src_y', 'src_w', 'src_h'].forEach(c => {
                const slider = document.querySelector(`input[oninput*="updateCoordSlider('${typeStr}', ${idx}, '${c}'"]`);
                if (slider) {
                    slider.value = item[c] !== undefined ? item[c] : (c === 'src_w' || c === 'src_h' ? 1.0 : 0.0);
                }
                const label = document.getElementById(`${typeStr}-${idx}-val-${c}`);
                if (label) {
                    label.textContent = (item[c] !== undefined ? item[c] : (c === 'src_w' || c === 'src_h' ? 1.0 : 0.0)).toFixed(2);
                }
            });
        }

        async function loadMediaFiles() {
            try {
                const res = await fetch('/api/media');
                if (!res.ok) throw new Error('API return code failed');
                const files = await res.json();
                
                const statsMedia = document.getElementById('statsMedia');
                if (statsMedia) {
                    statsMedia.textContent = `${files.length} Stored`;
                }
                
                const list = document.getElementById('media-files-list');
                if (list) {
                    list.innerHTML = '';
                    if (files.length === 0) {
                        list.innerHTML = `
                            <tr>
                                <td colspan="3" style="text-align: center; padding: 2rem; color: var(--text-secondary);">
                                    No media files uploaded yet. Drag & drop files above to populate.
                                </td>
                            </tr>
                        `;
                        return;
                    }
                    
                    files.forEach(file => {
                        const tr = document.createElement('tr');
                        tr.style.borderBottom = '1px solid var(--border-color)';
                        
                        const sizeMB = (file.size / (1024 * 1024)).toFixed(2);
                        const relativePath = `assets/media/${file.name}`;
                        
                        tr.innerHTML = `
                            <td style="padding: 1rem; font-weight: 500;">${file.name}</td>
                            <td style="padding: 1rem; color: var(--text-secondary);">${sizeMB} MB</td>
                            <td style="padding: 1rem; text-align: right;">
                                <div style="display: flex; gap: 0.5rem; justify-content: flex-end;">
                                    <button class="btn btn-secondary btn-small" onclick="copyToClipboard('${relativePath}')">Copy Path</button>
                                    <a href="/api/media/download?file=${encodeURIComponent(file.name)}" class="btn btn-cyan btn-small" style="text-decoration: none; color: #0b0914;">Download</a>
                                    <button class="btn btn-danger btn-small" onclick="deleteMediaFile('${file.name}')">Delete</button>
                                </div>
                            </td>
                        `;
                        list.appendChild(tr);
                    });
                }
            } catch (err) {
                console.error(err);
                showToast('Failed to load media', 'Could not query uploaded media from backend.', 'error');
            }
        }
        
        async function deleteMediaFile(filename) {
            if (!confirm(`Are you sure you want to permanently delete "${filename}"?`)) return;
            try {
                const res = await fetch('/api/media/delete', {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify({ file: filename })
                });
                if (res.ok) {
                    showToast('File Deleted', `Successfully removed "${filename}" from media storage.`, 'success');
                    loadMediaFiles();
                } else {
                    const data = await res.json();
                    showToast('Delete Failed', data.error || 'Server error deleting file.', 'error');
                }
            } catch (err) {
                showToast('Delete Connection Error', 'Failed to reach backend delete endpoint.', 'error');
            }
        }
        
        function initMediaUploader() {
            const zone = document.getElementById('upload-zone');
            const input = document.getElementById('media-file-input');
            const progressContainer = document.getElementById('upload-progress-container');
            const progressFilename = document.getElementById('upload-filename');
            const percentageText = document.getElementById('upload-percentage');
            const progressBar = document.getElementById('upload-progress-bar');
            
            if (!zone || !input) return;
            
            zone.onclick = () => input.click();
            
            zone.ondragover = (e) => {
                e.preventDefault();
                zone.style.borderColor = 'var(--accent-secondary)';
                zone.style.background = 'rgba(0, 242, 254, 0.04)';
            };
            
            zone.ondragleave = () => {
                zone.style.borderColor = 'rgba(138, 43, 226, 0.4)';
                zone.style.background = 'rgba(138, 43, 226, 0.02)';
            };
            
            zone.ondrop = (e) => {
                e.preventDefault();
                zone.style.borderColor = 'rgba(138, 43, 226, 0.4)';
                zone.style.background = 'rgba(138, 43, 226, 0.02)';
                if (e.dataTransfer.files.length > 0) {
                    uploadFile(e.dataTransfer.files[0]);
                }
            };
            
            input.onchange = () => {
                if (input.files.length > 0) {
                    uploadFile(input.files[0]);
                    input.value = ''; // Reset
                }
            };
            
            function uploadFile(file) {
                progressContainer.style.display = 'block';
                progressFilename.textContent = file.name;
                percentageText.textContent = '0%';
                progressBar.style.width = '0%';
                
                const xhr = new XMLHttpRequest();
                xhr.open('POST', '/api/upload', true);
                xhr.setRequestHeader('X-Filename', encodeURIComponent(file.name));
                
                xhr.upload.onprogress = (e) => {
                    if (e.lengthComputable) {
                        const pct = Math.round((e.loaded / e.total) * 100);
                        percentageText.textContent = `${pct}%`;
                        progressBar.style.width = `${pct}%`;
                    }
                };
                
                xhr.onload = () => {
                    progressContainer.style.display = 'none';
                    if (xhr.status === 200) {
                        showToast('Upload Complete', `Successfully uploaded "${file.name}".`, 'success');
                        loadMediaFiles();
                    } else {
                        let errorMsg = 'Upload failed';
                        try {
                            const resp = JSON.parse(xhr.responseText);
                            errorMsg = resp.error || errorMsg;
                        } catch (e) {}
                        showToast('Upload Failed', errorMsg, 'error');
                    }
                };
                
                xhr.onerror = () => {
                    progressContainer.style.display = 'none';
                    showToast('Upload Network Error', 'Failed to communicate with media server.', 'error');
                };
                
                xhr.send(file);
            }
        }

        let activeCropTarget = null; // { type: 'videos'|'cameras', index: i }
        let currentCropCoords = { x: 0, y: 0, w: 1, h: 1 };
        
        function openCropEditor(type, idx) {
            activeCropTarget = { type, index: idx };
            const item = fullConfig[type][idx];
            
            currentCropCoords.x = item.src_x !== undefined ? item.src_x : 0;
            currentCropCoords.y = item.src_y !== undefined ? item.src_y : 0;
            currentCropCoords.w = item.src_w !== undefined ? item.src_w : 1;
            currentCropCoords.h = item.src_h !== undefined ? item.src_h : 1;
            
            const titleEl = document.getElementById('cropModalTitle');
            if (titleEl) {
                titleEl.textContent = `Visual Crop Editor - ${type === 'videos' ? 'Video Player' : 'Camera Stream'} Slot #${idx}`;
            }
            
            const modal = document.getElementById('cropModal');
            if (modal) {
                modal.style.display = 'flex';
            }
            
            updateCropBoxVisuals();
            initCropBoxInteractions();
        }
        
        function updateCropBoxVisuals() {
            const cropBox = document.getElementById('cropBox');
            if (cropBox) {
                cropBox.style.left = `${currentCropCoords.x * 100}%`;
                cropBox.style.top = `${currentCropCoords.y * 100}%`;
                cropBox.style.width = `${currentCropCoords.w * 100}%`;
                cropBox.style.height = `${currentCropCoords.h * 100}%`;
            }
            
            const valX = document.getElementById('cropVal-x');
            const valY = document.getElementById('cropVal-y');
            const valW = document.getElementById('cropVal-w');
            const valH = document.getElementById('cropVal-h');
            if (valX) valX.textContent = currentCropCoords.x.toFixed(2);
            if (valY) valY.textContent = currentCropCoords.y.toFixed(2);
            if (valW) valW.textContent = currentCropCoords.w.toFixed(2);
            if (valH) valH.textContent = currentCropCoords.h.toFixed(2);
        }
        
        function initCropBoxInteractions() {
            const workspace = document.getElementById('cropWorkspace');
            const cropBox = document.getElementById('cropBox');
            const handle = document.getElementById('cropResizeHandle');
            
            if (!workspace || !cropBox || !handle) return;
            
            let isDragging = false;
            let isResizing = false;
            let startX, startY, startLeft, startTop, startWidth, startHeight;
            
            cropBox.onmousedown = (e) => {
                e.preventDefault();
                const rect = workspace.getBoundingClientRect();
                startX = e.clientX;
                startY = e.clientY;
                
                startLeft = currentCropCoords.x * rect.width;
                startTop = currentCropCoords.y * rect.height;
                startWidth = currentCropCoords.w * rect.width;
                startHeight = currentCropCoords.h * rect.height;
                
                if (e.target === handle) {
                    isResizing = true;
                } else {
                    isDragging = true;
                }
                
                const onMouseMove = (moveEv) => {
                    const dx = moveEv.clientX - startX;
                    const dy = moveEv.clientY - startY;
                    const currentRect = workspace.getBoundingClientRect();
                    
                    if (isDragging) {
                        let newLeft = startLeft + dx;
                        let newTop = startTop + dy;
                        
                        newLeft = Math.max(0, Math.min(currentRect.width - startWidth, newLeft));
                        newTop = Math.max(0, Math.min(currentRect.height - startHeight, newTop));
                        
                        currentCropCoords.x = parseFloat((newLeft / currentRect.width).toFixed(2));
                        currentCropCoords.y = parseFloat((newTop / currentRect.height).toFixed(2));
                    } else if (isResizing) {
                        let newWidth = startWidth + dx;
                        let newHeight = startHeight + dy;
                        
                        newWidth = Math.max(currentRect.width * 0.05, Math.min(currentRect.width - startLeft, newWidth));
                        newHeight = Math.max(currentRect.height * 0.05, Math.min(currentRect.height - startTop, newHeight));
                        
                        currentCropCoords.w = parseFloat((newWidth / currentRect.width).toFixed(2));
                        currentCropCoords.h = parseFloat((newHeight / currentRect.height).toFixed(2));
                    }
                    
                    updateCropBoxVisuals();
                };
                
                const onMouseUp = () => {
                    isDragging = false;
                    isResizing = false;
                    document.removeEventListener('mousemove', onMouseMove);
                    document.removeEventListener('mouseup', onMouseUp);
                };
                
                document.addEventListener('mousemove', onMouseMove);
                document.addEventListener('mouseup', onMouseUp);
            };
        }
        
        function closeCropEditor(apply) {
            if (apply && activeCropTarget) {
                const { type, index } = activeCropTarget;
                const item = fullConfig[type][index];
                item.src_x = currentCropCoords.x;
                item.src_y = currentCropCoords.y;
                item.src_w = currentCropCoords.w;
                item.src_h = currentCropCoords.h;
                
                // Sync to sliders
                ['src_x', 'src_y', 'src_w', 'src_h'].forEach(c => {
                    const slider = document.querySelector(`input[oninput*="updateCoordSlider('${type}', ${index}, '${c}'"]`);
                    if (slider) {
                        slider.value = item[c];
                    }
                    const label = document.getElementById(`${type}-${index}-val-${c}`);
                    if (label) {
                        label.textContent = item[c].toFixed(2);
                    }
                });
                
                showToast('Crop Coordinates Applied', 'Successfully mapped to input fields.', 'success');
            }
            
            const modal = document.getElementById('cropModal');
            if (modal) {
                modal.style.display = 'none';
            }
            activeCropTarget = null;
        }

        // Initializer
        fetchConfig();
        initMediaUploader();
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

std::optional<int> HttpServerModule::pop_video_trigger() {
    std::lock_guard<std::mutex> lock(trigger_mutex_);
    if (pending_triggers_.empty()) {
        return std::nullopt;
    }
    int idx = pending_triggers_.front();
    pending_triggers_.erase(pending_triggers_.begin());
    return idx;
}

void HttpServerModule::push_video_trigger(int index) {
    std::lock_guard<std::mutex> lock(trigger_mutex_);
    pending_triggers_.push_back(index);
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

        // Set timeout on client socket (30s to allow file uploads)
        struct timeval tv;
        tv.tv_sec = 30;
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

        // Parse X-Filename for POST upload
        std::string x_filename = "";
        size_t xf_pos = request.find("X-Filename:");
        if (xf_pos != std::string::npos) {
            xf_pos += 11;
            size_t end_line = request.find("\r\n", xf_pos);
            if (end_line != std::string::npos) {
                x_filename = request.substr(xf_pos, end_line - xf_pos);
                x_filename.erase(0, x_filename.find_first_not_of(" \t"));
                x_filename.erase(x_filename.find_last_not_of(" \t") + 1);
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
        else if (method == "GET" && uri == "/api/media") {
            try {
                std::filesystem::create_directories("assets/media");
                nlohmann::json files_arr = nlohmann::json::array();
                
                for (const auto& entry : std::filesystem::directory_iterator("assets/media")) {
                    if (entry.is_regular_file()) {
                        nlohmann::json f;
                        f["name"] = entry.path().filename().string();
                        f["size"] = entry.file_size();
                        files_arr.push_back(f);
                    }
                }
                
                response_body = files_arr.dump();
                std::stringstream header;
                header << "HTTP/1.1 200 OK\r\n"
                       << "Content-Type: application/json; charset=utf-8\r\n"
                       << "Content-Length: " << response_body.length() << "\r\n"
                       << "Connection: close\r\n\r\n";
                response_headers = header.str();
            } catch (const std::exception& e) {
                response_body = std::string("{\"error\":\"") + e.what() + "\"}";
                std::stringstream header;
                header << "HTTP/1.1 500 Internal Error\r\n"
                       << "Content-Type: application/json\r\n"
                       << "Content-Length: " << response_body.length() << "\r\n"
                       << "Connection: close\r\n\r\n";
                response_headers = header.str();
            }
        }
        else if (method == "POST" && uri == "/api/upload") {
            try {
                if (x_filename.empty()) {
                    response_body = "{\"error\":\"Missing X-Filename header\"}";
                    std::stringstream header;
                    header << "HTTP/1.1 400 Bad Request\r\n"
                           << "Content-Type: application/json\r\n"
                           << "Content-Length: " << response_body.length() << "\r\n"
                           << "Connection: close\r\n\r\n";
                    response_headers = header.str();
                } else {
                    std::filesystem::create_directories("assets/media");
                    std::string clean_filename = std::filesystem::path(x_filename).filename().string();
                    std::string save_path = "assets/media/" + clean_filename;
                    
                    std::ofstream out_file(save_path, std::ios::binary);
                    if (out_file.is_open()) {
                        out_file.write(body.data(), body.size());
                        out_file.close();
                        response_body = "{\"status\":\"ok\"}";
                        std::stringstream header;
                        header << "HTTP/1.1 200 OK\r\n"
                               << "Content-Type: application/json\r\n"
                               << "Content-Length: " << response_body.length() << "\r\n"
                               << "Connection: close\r\n\r\n";
                        response_headers = header.str();
                    } else {
                        response_body = "{\"error\":\"Failed to open output file\"}";
                        std::stringstream header;
                        header << "HTTP/1.1 500 Internal Error\r\n"
                               << "Content-Type: application/json\r\n"
                               << "Content-Length: " << response_body.length() << "\r\n"
                               << "Connection: close\r\n\r\n";
                        response_headers = header.str();
                    }
                }
            } catch (const std::exception& e) {
                response_body = std::string("{\"error\":\"") + e.what() + "\"}";
                std::stringstream header;
                header << "HTTP/1.1 500 Internal Error\r\n"
                       << "Content-Type: application/json\r\n"
                       << "Content-Length: " << response_body.length() << "\r\n"
                       << "Connection: close\r\n\r\n";
                response_headers = header.str();
            }
        }
        else if (method == "GET" && uri.rfind("/api/media/dimensions", 0) == 0) {
            try {
                size_t param_pos = uri.find("?file=");
                if (param_pos != std::string::npos) {
                    std::string filename = uri.substr(param_pos + 6);
                    std::string decoded = "";
                    for (size_t i = 0; i < filename.length(); ++i) {
                        if (filename[i] == '%' && i + 2 < filename.length()) {
                            std::string hex = filename.substr(i + 1, 2);
                            try {
                                char chr = (char)std::stoul(hex, nullptr, 16);
                                decoded += chr;
                                i += 2;
                            } catch (...) {
                                decoded += filename[i];
                            }
                        } else {
                            decoded += filename[i];
                        }
                    }
                    filename = decoded;
                    
                    std::string file_path = filename;
                    if (file_path.rfind("assets/media/", 0) == std::string::npos) {
                        file_path = "assets/media/" + std::filesystem::path(file_path).filename().string();
                    }
                    
                    ContainerReader reader;
                    auto open_res = reader.open(file_path);
                    if (open_res) {
                        int v_stream = reader.find_video_stream();
                        if (v_stream >= 0) {
                            auto params = reader.get_codec_params(v_stream);
                            if (params) {
                                int w = params->width;
                                int h = params->height;
                                response_body = "{\"width\":" + std::to_string(w) + ",\"height\":" + std::to_string(h) + "}";
                                std::stringstream header;
                                header << "HTTP/1.1 200 OK\r\n"
                                       << "Content-Type: application/json\r\n"
                                       << "Content-Length: " << response_body.length() << "\r\n"
                                       << "Connection: close\r\n\r\n";
                                response_headers = header.str();
                            } else {
                                throw std::runtime_error("No codec parameters found");
                            }
                        } else {
                            throw std::runtime_error("No video stream found in file");
                        }
                    } else {
                        throw std::runtime_error("Failed to open media file");
                    }
                } else {
                    response_body = "{\"error\":\"Missing file parameter\"}";
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
        else if (method == "GET" && uri.rfind("/api/media/download", 0) == 0) {
            try {
                size_t param_pos = uri.find("?file=");
                if (param_pos != std::string::npos) {
                    std::string filename = uri.substr(param_pos + 6);
                    std::string decoded = "";
                    for (size_t i = 0; i < filename.length(); ++i) {
                        if (filename[i] == '%' && i + 2 < filename.length()) {
                            std::string hex = filename.substr(i + 1, 2);
                            try {
                                char chr = (char)std::stoul(hex, nullptr, 16);
                                decoded += chr;
                                i += 2;
                            } catch (...) {
                                decoded += filename[i];
                            }
                        } else {
                            decoded += filename[i];
                        }
                    }
                    filename = decoded;
                    filename = std::filesystem::path(filename).filename().string();
                    std::string file_path = "assets/media/" + filename;
                    
                    std::ifstream f(file_path, std::ios::binary);
                    if (f.is_open()) {
                        std::stringstream buffer;
                        buffer << f.rdbuf();
                        response_body = buffer.str();
                        
                        std::stringstream header;
                        header << "HTTP/1.1 200 OK\r\n"
                               << "Content-Type: application/octet-stream\r\n"
                               << "Content-Disposition: attachment; filename=\"" << filename << "\"\r\n"
                               << "Content-Length: " << response_body.length() << "\r\n"
                               << "Connection: close\r\n\r\n";
                        response_headers = header.str();
                    } else {
                        response_body = "{\"error\":\"File not found\"}";
                        std::stringstream header;
                        header << "HTTP/1.1 404 Not Found\r\n"
                               << "Content-Type: application/json\r\n"
                               << "Content-Length: " << response_body.length() << "\r\n"
                               << "Connection: close\r\n\r\n";
                        response_headers = header.str();
                    }
                } else {
                    response_body = "{\"error\":\"Missing file parameter\"}";
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
                header << "HTTP/1.1 500 Internal Error\r\n"
                       << "Content-Type: application/json\r\n"
                       << "Content-Length: " << response_body.length() << "\r\n"
                       << "Connection: close\r\n\r\n";
                response_headers = header.str();
            }
        }
        else if (method == "POST" && uri == "/api/media/delete") {
            try {
                auto del_json = nlohmann::json::parse(body);
                if (del_json.contains("file") && del_json["file"].is_string()) {
                    std::string filename = del_json["file"];
                    filename = std::filesystem::path(filename).filename().string();
                    std::string file_path = "assets/media/" + filename;
                    
                    if (std::filesystem::exists(file_path)) {
                        std::filesystem::remove(file_path);
                        response_body = "{\"status\":\"ok\"}";
                        std::stringstream header;
                        header << "HTTP/1.1 200 OK\r\n"
                               << "Content-Type: application/json\r\n"
                               << "Content-Length: " << response_body.length() << "\r\n"
                               << "Connection: close\r\n\r\n";
                        response_headers = header.str();
                    } else {
                        response_body = "{\"error\":\"File not found\"}";
                        std::stringstream header;
                        header << "HTTP/1.1 404 Not Found\r\n"
                               << "Content-Type: application/json\r\n"
                               << "Content-Length: " << response_body.length() << "\r\n"
                               << "Connection: close\r\n\r\n";
                        response_headers = header.str();
                    }
                } else {
                    response_body = "{\"error\":\"Missing file field\"}";
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
                header << "HTTP/1.1 500 Internal Error\r\n"
                       << "Content-Type: application/json\r\n"
                       << "Content-Length: " << response_body.length() << "\r\n"
                       << "Connection: close\r\n\r\n";
                response_headers = header.str();
            }
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
        else if (method == "POST" && uri == "/api/video/trigger") {
            try {
                auto trigger_json = nlohmann::json::parse(body);
                if (trigger_json.contains("video_index") && trigger_json["video_index"].is_number_integer()) {
                    int video_idx = trigger_json["video_index"];
                    
                    int max_videos = 0;
                    std::ifstream f(config_path_);
                    if (!f.is_open()) {
                        std::filesystem::path p = config_path_;
                        for (int i = 0; i < 4; ++i) {
                            p = "../" + p.string();
                            std::ifstream f2(p);
                            if (f2.is_open()) {
                                f = std::move(f2);
                                break;
                            }
                        }
                    }
                    if (f.is_open()) {
                        nlohmann::json j;
                        f >> j;
                        if (j.contains("videos") && j["videos"].is_array()) {
                            max_videos = j["videos"].size();
                        }
                    }
                    
                    if (video_idx >= 0 && video_idx < max_videos) {
                        push_video_trigger(video_idx);
                        response_body = "{\"status\":\"ok\"}";
                        std::stringstream header;
                        header << "HTTP/1.1 200 OK\r\n"
                               << "Content-Type: application/json\r\n"
                               << "Content-Length: " << response_body.length() << "\r\n"
                               << "Connection: close\r\n\r\n";
                        response_headers = header.str();
                    } else {
                        response_body = "{\"error\":\"Video index out of bounds\"}";
                        std::stringstream header;
                        header << "HTTP/1.1 400 Bad Request\r\n"
                               << "Content-Type: application/json\r\n"
                               << "Content-Length: " << response_body.length() << "\r\n"
                               << "Connection: close\r\n\r\n";
                        response_headers = header.str();
                    }
                } else {
                    response_body = "{\"error\":\"Missing or invalid video_index field\"}";
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
