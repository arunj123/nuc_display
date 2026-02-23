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
sudo usermod -aG video,render,audio,plugdev,input $USER

echo "--- 3. Building the Application ---"
mkdir -p build
cd build
cmake ..
make -j$(nproc)
cd ..

echo "--- 4. Installing Systemd Service ---"
# Discover system values
# Use SUDO_USER if running under sudo, otherwise current user
USER_VAL=${SUDO_USER:-$(whoami)}
WORKDIR_VAL=$(pwd)
# If we are in the scripts directory, go up one level
if [[ $WORKDIR_VAL == */scripts ]]; then
    WORKDIR_VAL=$(dirname "$WORKDIR_VAL")
fi
UID_VAL=$(id -u $USER_VAL)
# Try to find the first ALSA card
ALSA_CARD_VAL=$(aplay -l | grep "^card" | head -n 1 | cut -d" " -f2 | cut -d":" -f1 || echo "0")

echo "Detected Configuration:"
echo "  User: $USER_VAL"
echo "  WorkDir: $WORKDIR_VAL"
echo "  UID: $UID_VAL"
echo "  ALSA Card: $ALSA_CARD_VAL"

# Generate resolved service file
sed -e "s|@USER@|$USER_VAL|g" \
    -e "s|@WORKDIR@|$WORKDIR_VAL|g" \
    -e "s|@UID@|$UID_VAL|g" \
    -e "s|@ALSA_CARD@|$ALSA_CARD_VAL|g" \
    nuc_display.service > nuc_display.service.resolved

sudo cp nuc_display.service.resolved /etc/systemd/system/nuc_display.service
sudo systemctl daemon-reload
sudo systemctl enable nuc_display

echo "--- Installation Complete! ---"
echo "Note: You may need to logout and login again for group changes to take effect."
echo "To start the application now, run: sudo systemctl start nuc_display"
echo "To check the logs, run: journalctl -u nuc_display -f"
