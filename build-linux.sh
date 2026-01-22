#!/bin/bash
# libemf2svg-patched Linux Build Script
# Requires: CMake, GCC/Clang, development libraries

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/build"
BUILD_TYPE="Release"

# Detect package manager and install dependencies
install_dependencies() {
    if command -v apt-get &> /dev/null; then
        echo "Detected Debian/Ubuntu, installing dependencies..."
        sudo apt-get update
        sudo apt-get install -y cmake build-essential \
            libpng-dev libfreetype6-dev libfontconfig1-dev libxml2-dev
    elif command -v dnf &> /dev/null; then
        echo "Detected Fedora/RHEL, installing dependencies..."
        sudo dnf install -y cmake gcc gcc-c++ make \
            libpng-devel freetype-devel fontconfig-devel libxml2-devel
    elif command -v pacman &> /dev/null; then
        echo "Detected Arch Linux, installing dependencies..."
        sudo pacman -Sy --noconfirm cmake base-devel \
            libpng freetype2 fontconfig libxml2
    elif command -v zypper &> /dev/null; then
        echo "Detected openSUSE, installing dependencies..."
        sudo zypper install -y cmake gcc gcc-c++ make \
            libpng-devel freetype-devel fontconfig-devel libxml2-devel
    else
        echo "ERROR: Unknown Linux distribution."
        echo "Please install the following dependencies manually:"
        echo "  - cmake"
        echo "  - gcc/g++ or clang"
        echo "  - libpng development files"
        echo "  - freetype development files"
        echo "  - fontconfig development files"
        echo "  - libxml2 development files"
        exit 1
    fi
}

# Check for CMake
if ! command -v cmake &> /dev/null; then
    echo "CMake not found, attempting to install dependencies..."
    install_dependencies
fi

# Check if dependencies should be installed
if [ "$1" = "--install-deps" ]; then
    install_dependencies
fi

# Create build directory
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

# Configure with CMake
echo ""
echo "Configuring with CMake..."
cmake .. \
    -DCMAKE_BUILD_TYPE=$BUILD_TYPE \
    -DLONLY=ON

# Build
echo ""
echo "Building..."
cmake --build . --config $BUILD_TYPE -j$(nproc)

# Show output
echo ""
echo "Build successful!"
echo "Output files are in: $BUILD_DIR"
ls -la "$BUILD_DIR"/*.so* 2>/dev/null || true

echo ""
echo "To use, copy libemf2svg.so and libwmf2svg.so to your application directory"
echo "or install to system: sudo cmake --install ."
