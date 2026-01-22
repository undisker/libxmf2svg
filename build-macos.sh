#!/bin/bash
# libemf2svg-patched macOS Build Script
# Requires: Xcode Command Line Tools, CMake, Homebrew

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/build"
BUILD_TYPE="Release"

# Check for Homebrew
if ! command -v brew &> /dev/null; then
    echo "ERROR: Homebrew is not installed."
    echo "Please install Homebrew: https://brew.sh"
    exit 1
fi

# Check for CMake
if ! command -v cmake &> /dev/null; then
    echo "CMake not found, installing via Homebrew..."
    brew install cmake
fi

# Install dependencies via Homebrew
echo "Installing dependencies via Homebrew..."
brew install libpng freetype fontconfig libxml2 libiconv

# Create build directory
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

# Detect architecture
ARCH=$(uname -m)
if [ "$ARCH" = "arm64" ]; then
    echo "Building for Apple Silicon (arm64)..."
    CMAKE_ARCH="-DCMAKE_OSX_ARCHITECTURES=arm64"
else
    echo "Building for Intel (x86_64)..."
    CMAKE_ARCH="-DCMAKE_OSX_ARCHITECTURES=x86_64"
fi

# Configure with CMake
echo ""
echo "Configuring with CMake..."
cmake .. \
    -DCMAKE_BUILD_TYPE=$BUILD_TYPE \
    -DLONLY=ON \
    $CMAKE_ARCH \
    -DPNG_PNG_INCLUDE_DIR="$(brew --prefix libpng)/include" \
    -DPNG_LIBRARY="$(brew --prefix libpng)/lib/libpng.dylib" \
    -DFREETYPE_INCLUDE_DIRS="$(brew --prefix freetype)/include/freetype2" \
    -DFREETYPE_LIBRARIES="$(brew --prefix freetype)/lib/libfreetype.dylib" \
    -DFONTCONFIG_INCLUDE_DIR="$(brew --prefix fontconfig)/include" \
    -DFONTCONFIG_LIBRARY="$(brew --prefix fontconfig)/lib/libfontconfig.dylib" \
    -DLIBXML2_INCLUDE_DIR="$(brew --prefix libxml2)/include/libxml2" \
    -DLIBXML2_LIBRARIES="$(brew --prefix libxml2)/lib/libxml2.dylib"

# Build
echo ""
echo "Building..."
cmake --build . --config $BUILD_TYPE -j$(sysctl -n hw.ncpu)

# Show output
echo ""
echo "Build successful!"
echo "Output files are in: $BUILD_DIR"
ls -la "$BUILD_DIR"/*.dylib 2>/dev/null || true

echo ""
echo "To use, copy libemf2svg.dylib and libwmf2svg.dylib to your application directory."
