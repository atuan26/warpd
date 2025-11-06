#!/bin/bash
# Quick install script for warpd with smart hint mode
# 
# Usage:
#   # Install latest version
#   curl -fsSL https://raw.githubusercontent.com/atuan26/warpd/master/install.sh | sh
#   
#   # Install specific version
#   curl -fsSL https://raw.githubusercontent.com/atuan26/warpd/master/install.sh | WARPD_VERSION=v2.0.0 sh
#   
#   # Run from source directory (if already cloned)
#   ./install.sh

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

# Check if we're already in a warpd source directory
if [ -f "Makefile" ] && [ -f "src/warpd.c" ] && [ -d "src" ]; then
    echo "Detected warpd source directory, building from current location..."
    BUILD_DIR="$(pwd)"
    CLEANUP_NEEDED=false
else
    echo ""
    echo "Downloading warpd source..."
    
    # Allow version specification via environment variable
    VERSION=${WARPD_VERSION:-"master"}
    echo "Using version/branch: $VERSION"
    
    TEMP_DIR=$(mktemp -d)
    cd "$TEMP_DIR"
    
    if [ "$VERSION" = "master" ] || [ "$VERSION" = "latest" ]; then
        git clone --depth 1 https://github.com/atuan26/warpd.git
    else
        git clone https://github.com/atuan26/warpd.git
        cd warpd
        git checkout "$VERSION" 2>/dev/null || {
            echo "Error: Version/tag '$VERSION' not found"
            echo "Available tags:"
            git tag --sort=-version:refname | head -10
            exit 1
        }
        cd ..
    fi
    
    cd warpd
    BUILD_DIR="$(pwd)"
    CLEANUP_NEEDED=true
fi

echo ""
echo "Building warpd..."
make clean 2>/dev/null || true
make

echo ""
echo "Installing warpd..."
sudo make install

if [ "$CLEANUP_NEEDED" = true ]; then
    echo ""
    echo "Cleaning up..."
    cd ~
    rm -rf "$TEMP_DIR"
else
    echo ""
    echo "Build completed in source directory"
fi

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
