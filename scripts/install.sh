#!/bin/bash

# NUC Display Dashboard Installation Script
# This script installs dependencies, sets up permissions, builds the app, and installs the service.

set -e

echo "--- 1. Installing System Dependencies ---"
sudo apt-get update
sudo apt-get install -y \
    libavcodec-dev libavformat-dev libavutil-dev libswresample-dev \
    libva-dev libdrm-dev libgbm-dev libgl1-mesa-dev libegl1-mesa-dev \
    libasound2-dev libfreetype6-dev libharfbuzz-dev \
    curl cmake g++ build-essential git

echo "--- 2. Setting Up User Permissions ---"
# Add current user to hardware access groups
sudo usermod -aG video $USER
sudo usermod -aG render $USER
sudo usermod -aG audio $USER
sudo usermod -aG plugdev $USER

echo "--- 3. Building the Application ---"
mkdir -p build
cd build
cmake ..
make -j$(nproc)
cd ..

echo "--- 4. Installing Systemd Service ---"
sudo cp nuc_display.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable nuc_display

echo "--- Installation Complete! ---"
echo "Note: You may need to logout and login again for group changes to take effect."
echo "To start the application now, run: sudo systemctl start nuc_display"
echo "To check the logs, run: journalctl -u nuc_display -f"
