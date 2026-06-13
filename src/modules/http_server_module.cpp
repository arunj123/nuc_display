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
            grid-template-columns: repeat(4, 1fr);
            gap: 1.5rem;
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
                </div>

                <div class="mockup-container">
                    <label style="color:var(--text-primary); font-size:1.05rem; font-weight:600; margin-bottom:0.25rem;">NUC Screen Layout Preview</label>
                    <span style="font-size:0.8rem; color:var(--text-secondary); display:block; margin-bottom:1rem;">Interactive simulation showing screen zones in priority z-order</span>
                    
                    <div class="mockup-screen">
                        <div class="mockup-grid"></div>
                        <div id="mockupLayersContainer"></div>
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
            } catch (err) {
                showToast('Failed Connection', 'Could not read settings from NUC display engine.', 'error');
            }
        }

        // Populate fields
        function populateFormFields() {
            if (!fullConfig) return;

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
                                        <select onchange="updateVideoString(${index}, 'start_trigger', this.value)">${triggerOpts}</select>
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
                                    
                                    <h4 style="font-size:0.85rem; text-transform:uppercase; letter-spacing:0.5px; color:var(--text-secondary); margin: 1.5rem 0 1rem 0;">Source Media Crop Region</h4>
                                    
                                    ${renderCropCoordSliders(index, 'videos', v)}
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
                                
                                <h4 style="font-size:0.85rem; text-transform:uppercase; letter-spacing:0.5px; color:var(--text-secondary); margin:1.5rem 0 1rem 0;">Source Sensor Crop Rect</h4>
                                ${renderCropCoordSliders(index, 'cameras', cam)}
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

            // Draw order: first = behind, last = on top.
            // Absolute positioning DOM order renders later elements on top of earlier ones.
            // So rendering in order of index matches OpenGL overlays perfectly.
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
                        div.onclick = () => { switchTab('tab-videos'); document.getElementById(`video-accordion-${layer.video_index}`).classList.add('open'); };
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
                        div.onclick = () => switchTab('tab-cameras');
                        container.appendChild(div);
                    }
                }
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
            if (!fullConfig) return;

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
                return;
            }

            try {
                const res = await fetch('/api/config', {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify(fullConfig)
                });
                
                if (res.ok) {
                    showToast('Settings Saved', 'Configuration successfully updated and reloaded.', 'success');
                    fetchConfig(); // Reload from disk to verify
                } else {
                    const data = await res.json();
                    if (data.errors && data.errors.length > 0) {
                        showToast('Validation Failed', 'The display server rejected configurations:<br>' + data.errors.map(e => `&bull; ${e}`).join('<br>'), 'error');
                    } else {
                        showToast('Server Error', 'Failed to write configurations to backend.', 'error');
                    }
                }
            } catch (err) {
                showToast('Connection Error', 'Network failed saving settings.', 'error');
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

        // Initializer
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
