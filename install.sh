#!/bin/bash
# Quick install script for warpd with smart hint mode
# Usage: curl -fsSL https://raw.githubusercontent.com/atuan26/warpd/master/install.sh | sh

set -e

echo "=== WARPD ==="
echo ""

# Check if running as root
if [ "$EUID" -eq 0 ]; then
    echo "Error: Please do not run this script as root"
    echo "The script will ask for sudo password when needed"
    exit 1
fi

# Detect OS and install dependencies
echo "Detecting system and installing dependencies..."
if command -v apt &> /dev/null; then
    echo "Detected Debian/Ubuntu system"
    sudo apt update
    sudo apt install -y \
        git make gcc \
        libxi-dev libxinerama-dev libxft-dev \
        libxfixes-dev libxtst-dev libx11-dev \
        libcairo2-dev libxkbcommon-dev libwayland-dev \
        libatspi2.0-dev libdbus-1-dev libglib2.0-dev
elif command -v pacman &> /dev/null; then
    echo "Detected Arch Linux system"
    sudo pacman -S --needed --noconfirm \
        git make gcc \
        libxi libxinerama libxft libxfixes libxtst libx11 \
        cairo libxkbcommon wayland \
        at-spi2-core dbus glib2
elif command -v dnf &> /dev/null; then
    echo "Detected Fedora/RHEL system"
    sudo dnf install -y \
        git make gcc \
        libXi-devel libXinerama-devel libXft-devel \
        libXfixes-devel libXtst-devel libX11-devel \
        cairo-devel libxkbcommon-devel wayland-devel \
        at-spi2-core-devel dbus-devel glib2-devel
else
    echo "Error: Unsupported distribution"
    echo "Please install dependencies manually and run: make && sudo make install"
    exit 1
fi

echo ""
echo "Downloading warpd source..."
TEMP_DIR=$(mktemp -d)
cd "$TEMP_DIR"

git clone --depth 1 https://github.com/atuan26/warpd.git
cd warpd

echo ""
echo "Building warpd..."
make

echo ""
echo "Installing warpd..."
sudo make install

echo ""
echo "Cleaning up..."
cd ~
rm -rf "$TEMP_DIR"

echo ""
echo "=== Installation Complete! ==="
echo ""
echo "To start warpd, run: warpd"
echo ""
echo "Smart Hint Mode usage:"
echo "  1. Press Alt+Meta+c to activate normal mode"
echo "  2. Press 'f' to activate smart hint mode"
echo "  3. Type the letter labels to navigate to elements"
echo ""
echo "For more information, see: man warpd"
echo ""
