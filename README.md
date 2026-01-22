libxmf2svg 
==========

Library for conversion MS EMF (Enhanced Metafile) and WMF (Windows MetaFile) to SVG.
A patched fork of [libemf2svg](https://github.com/kakwa/libemf2svg) with fixes for EMF+ image rendering issues.

## Changes from Original

This fork includes the following critical fixes:

### 1. Y Coordinate Bug Fix (vendor/libuemf/upmf.c)

**Location:** `vendor/libuemf/upmf.c`, line 6578 in `U_PMF_VARPOINTS_get` function

**Bug:** When parsing EMF+ points with `U_PPF_C` flag (int16 compressed coordinates), the Y coordinate was always 0 due to a typo:
```c
// BEFORE (BUG):
if(!U_PMF_POINT_get(&contents, &XF, &XF, blimit))break;  // &XF passed twice!
```

**Fix:**
```c
// AFTER (FIXED):
if(!U_PMF_POINT_get(&contents, &XF, &YF, blimit))break;  // Correct &YF
```

**Impact:** This fix corrects images that had height=0 or were positioned incorrectly.

### 2. EMF+ Image Rendering Disabled (src/lib/pmf2svg.c)

**Location:** `src/lib/pmf2svg.c`, lines 200-209

**Problem:** EMF+ DRAWIMAGE and DRAWIMAGEPOINTS records were rendering duplicate images at incorrect positions (often at 0,0) while the EMF STRETCHDIBITS records contained correctly positioned images.

**Fix:** Disabled EMF+ image rendering to use only EMF STRETCHDIBITS:
```c
case (U_PMR_DRAWIMAGE):
    // Skip EMF+ DRAWIMAGE - use EMF STRETCHDIBITS instead
    break;
case (U_PMR_DRAWIMAGEPOINTS):
    // Skip EMF+ DRAWIMAGEPOINTS - use EMF STRETCHDIBITS instead
    break;
```

**Impact:** Eliminates duplicate images and ensures correct image positioning in SVG output.

## Building

### Prerequisites

All platforms require:
- CMake 3.12+
- C11 compatible compiler

### Windows

1. Install [Visual Studio 2019/2022](https://visualstudio.microsoft.com/) with C++ workload
2. Install [vcpkg](https://github.com/microsoft/vcpkg)
3. Set environment variable: `set VCPKG_ROOT=C:\path\to\vcpkg`
4. Run build script:
   ```cmd
   build-windows.bat
   ```

Output: `build/Release/emf2svg.dll` and `build/Release/wmf2svg.dll`

### macOS

1. Install [Homebrew](https://brew.sh)
2. Run build script:
   ```bash
   chmod +x build-macos.sh
   ./build-macos.sh
   ```

Output: `build/libemf2svg.dylib` and `build/libwmf2svg.dylib`

### Linux

1. Install dependencies (script can do this automatically):
   ```bash
   chmod +x build-linux.sh
   ./build-linux.sh --install-deps
   ```

   Or just build (if dependencies are already installed):
   ```bash
   ./build-linux.sh
   ```

Output: `build/libemf2svg.so` and `build/libwmf2svg.so`

## Manual Build

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release -DLONLY=ON
cmake --build .
```

## Usage

The API is identical to the original libemf2svg. See the [original documentation](https://github.com/kakwa/libemf2svg).

## License

Same as original libemf2svg - see LICENSE file.

## Credits

- Original libemf2svg: https://github.com/kakwa/libemf2svg
- libuemf: https://sourceforge.net/projects/libuemf/

