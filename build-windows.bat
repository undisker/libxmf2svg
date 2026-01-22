@echo off
REM libemf2svg-patched Windows Build Script
REM Requires: Visual Studio 2019/2022, CMake, vcpkg

setlocal EnableDelayedExpansion

set SCRIPT_DIR=%~dp0
set BUILD_DIR=%SCRIPT_DIR%build
set BUILD_TYPE=Release

REM Check for vcpkg
if "%VCPKG_ROOT%"=="" (
    echo ERROR: VCPKG_ROOT environment variable is not set.
    echo Please install vcpkg and set VCPKG_ROOT to its location.
    echo Example: set VCPKG_ROOT=C:\vcpkg
    exit /b 1
)

if not exist "%VCPKG_ROOT%\vcpkg.exe" (
    echo ERROR: vcpkg.exe not found in %VCPKG_ROOT%
    exit /b 1
)

REM Install dependencies via vcpkg
echo Installing dependencies via vcpkg...
"%VCPKG_ROOT%\vcpkg.exe" install libxml2:x64-windows libpng:x64-windows libiconv:x64-windows freetype:x64-windows fontconfig:x64-windows

if %ERRORLEVEL% NEQ 0 (
    echo ERROR: Failed to install vcpkg dependencies
    exit /b 1
)

REM Create build directory
if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"
cd "%BUILD_DIR%"

REM Configure with CMake
echo.
echo Configuring with CMake...
cmake .. -G "Visual Studio 17 2022" -A x64 ^
    -DCMAKE_BUILD_TYPE=%BUILD_TYPE% ^
    -DCMAKE_TOOLCHAIN_FILE="%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake" ^
    -DVCPKG_TARGET_TRIPLET=x64-windows ^
    -DLONLY=ON

if %ERRORLEVEL% NEQ 0 (
    echo ERROR: CMake configuration failed
    exit /b 1
)

REM Build
echo.
echo Building...
cmake --build . --config %BUILD_TYPE%

if %ERRORLEVEL% NEQ 0 (
    echo ERROR: Build failed
    exit /b 1
)

REM Copy output DLLs
echo.
echo Build successful!
echo Output files are in: %BUILD_DIR%\%BUILD_TYPE%\
dir "%BUILD_DIR%\%BUILD_TYPE%\*.dll" 2>nul
dir "%BUILD_DIR%\%BUILD_TYPE%\*.lib" 2>nul

echo.
echo To use, copy emf2svg.dll and wmf2svg.dll to your application directory.

endlocal
