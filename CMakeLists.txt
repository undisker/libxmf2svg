cmake_minimum_required(VERSION 3.12)
include(CheckFunctionExists)
include(ExternalProject)

# libemf2svg-patched - Patched EMF to SVG conversion library
# This fork contains fixes for EMF+ image rendering issues:
# 1. Fixed Y coordinate bug in U_PMF_VARPOINTS_get (upmf.c)
# 2. Disabled EMF+ DRAWIMAGE/DRAWIMAGEPOINTS to prevent duplicate images

project(emf2svg)

set(emf2svg_VERSION_MAJOR 1)
set(emf2svg_VERSION_MINOR 8)
set(emf2svg_VERSION_PATCH 2)
set(emf2svg_VERSION ${emf2svg_VERSION_MAJOR}.${emf2svg_VERSION_MINOR}.${emf2svg_VERSION_PATCH})

if(VCPKG_TARGET_TRIPLET)
  set(PLATFORM_TOOLCHAIN ${CMAKE_CURRENT_SOURCE_DIR}/cmake/${VCPKG_TARGET_TRIPLET}.cmake)
  message(STATUS "Configured with platform toolchain = '${PLATFORM_TOOLCHAIN}'")
  if(EXISTS ${PLATFORM_TOOLCHAIN})
    set(PLATFORM_TOOLCHAIN_OPTION -DCMAKE_TOOLCHAIN_FILE=${PLATFORM_TOOLCHAIN})
    include(${PLATFORM_TOOLCHAIN})
  endif(EXISTS ${PLATFORM_TOOLCHAIN})
endif(VCPKG_TARGET_TRIPLET)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_CURRENT_SOURCE_DIR}/cmake/")

option(LONLY     "build library only"           ON)
option(GCOV      "compile with gcov support"    OFF)
option(UNITTEST  "compile unit tests"           OFF)
option(INDEX     "print record indexes"         OFF)
option(STATIC    "compile statically"           OFF)
option(FORCELE   "force little endian architecture"   OFF)

if(STATIC)
    set(SHARED "")
    set(CMAKE_FIND_LIBRARY_SUFFIXES ".a")
    set(BUILD_SHARED_LIBRARIES OFF)
    set(CMAKE_EXE_LINKER_FLAGS "-static")
else(STATIC)
    set(SHARED "SHARED")
endif(STATIC)

find_package(PNG REQUIRED)
find_package(Freetype REQUIRED)
find_package(Fontconfig REQUIRED)
find_package(LibXml2 REQUIRED)

set(DEPS ${CMAKE_CURRENT_SOURCE_DIR}/deps)
set(EXTERNAL_INCLUDE_DIR ${DEPS}/include)
set(EXTERNAL_LIB_DIR ${DEPS}/lib)

# Configure fmem for memory-backed streams
# On Linux, use native open_memstream; on other platforms, build external project
if(UNIX AND NOT APPLE)
  # Linux: Use native implementation from inc/fmem.h using open_memstream
  message(STATUS "Using native open_memstream for fmem (Linux)")
  set(EXTERNAL_FMEM "")
else()
  # macOS/Windows/other: Build fmem as external project
  message(STATUS "Building fmem as external project")
  set(FMEM_NAME fm${EXTERNAL_LIB_DIR_SUFFIX})

  ExternalProject_Add(${FMEM_NAME}
     PREFIX ${DEPS}
     SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/vendor/fmem
     CMAKE_ARGS -DBUILD_TESTING=FALSE
                -DCMAKE_INSTALL_PREFIX=${DEPS}
                -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
                -DCMAKE_POLICY_VERSION_MINIMUM=3.5
                ${PLATFORM_TOOLCHAIN_OPTION}
                ${CMAKE_OSX_ARCHITECTURES_OPTION}
  )
  set(EXTERNAL_FMEM "fmem")
endif()

set(DEPS_UEMF ${CMAKE_CURRENT_SOURCE_DIR}/vendor/libuemf)

# set version as a definition
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DE2S_VERSION='\"${emf2svg_VERSION}\"'")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DE2S_VERSION=${emf2svg_VERSION}")

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  if(MSVC)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Od -Zi")
  else(MSVC)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O0 -g")
  endif(MSVC)
endif()

if(UNIX)
  link_libraries(m)
  add_compile_options(-fPIC)
endif(UNIX)

if(NOT FORCELE)
  include(TestBigEndian)
  TEST_BIG_ENDIAN(BIGENDIAN)
  IF(${BIGENDIAN})
      add_definitions(-DWORDS_BIGENDIAN)
  ENDIF(${BIGENDIAN})
endif(NOT FORCELE)

# Build external dependencies if we are on OSX
if(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
  set(EXTERNAL_ICONV "iconv")
  add_definitions(-DDARWIN)
endif(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")

# Find/build external dependencies if it is Microsoft Visual Studio build
if(MSVC)
  find_package(Iconv REQUIRED)
  set(EXTERNAL_ICONV ${Iconv_LIBRARY})
endif(MSVC)

if(MSVC)
  include_directories(
    ./inc
    ${PNG_INCLUDE_DIRS}
    ${FREETYPE_INCLUDE_DIRS}
    ${FONTCONFIG_INCLUDE_DIRS}
    ${EXTERNAL_INCLUDE_DIR}
    ${DEPS_UEMF}/include
  )
  link_directories(
    ${CMAKE_BINARY_DIR}
    ${EXTERNAL_LIB_DIR}
  )
else(MSVC)
  include_directories(
    ./inc
    ${PNG_INCLUDE_DIRS}
    ${FREETYPE_INCLUDE_DIRS}
    ${FONTCONFIG_INCLUDE_DIRS}
    ${EXTERNAL_INCLUDE_DIR}
    ${DEPS_UEMF}/include
    /usr/local/include
    /usr/include
    /sw/include
  )
  link_directories(
    ${CMAKE_BINARY_DIR}
    ${EXTERNAL_LIB_DIR}
  )
endif(MSVC)

# Vendored libuemf sources (with patched upmf.c)
set(UEMF_SOURCES
  ${DEPS_UEMF}/uemf_utf.c
  ${DEPS_UEMF}/uemf_endian.c
  ${DEPS_UEMF}/uemf.c
  ${DEPS_UEMF}/upmf.c
)

# WMF sources for wmf2svg library
set(UWMF_SOURCES
  ${DEPS_UEMF}/uemf_utf.c
  ${DEPS_UEMF}/uemf_endian.c
  ${DEPS_UEMF}/uemf.c
  ${DEPS_UEMF}/uemf_safe.c
  ${DEPS_UEMF}/uwmf_endian.c
  ${DEPS_UEMF}/uwmf.c
)

add_library(emf2svg
  ${SHARED}
  src/lib/pmf2svg.c
  src/lib/pmf2svg_print.c
  ${UEMF_SOURCES}
  src/lib/emf2svg_utils.c
  src/lib/emf2svg_img_utils.c
  src/lib/emf2svg_clip_utils.c
  src/lib/emf2svg_rec_control.c
  src/lib/emf2svg_rec_object_creation.c
  src/lib/emf2svg_rec_path.c
  src/lib/emf2svg_rec_clipping.c
  src/lib/emf2svg_rec_drawing.c
  src/lib/emf2svg_rec_bitmap.c
  src/lib/emf2svg_rec_object_manipulation.c
  src/lib/emf2svg_rec_comment.c
  src/lib/emf2svg_rec_transform.c
  src/lib/emf2svg_rec_state_record.c
  src/lib/emf2svg_print.c
  src/lib/emf2svg.c
)

# Add libuemf include directory with high priority
target_include_directories(emf2svg BEFORE PRIVATE ${DEPS_UEMF}/include)

set_target_properties(emf2svg
  PROPERTIES
  VERSION ${emf2svg_VERSION}
  SOVERSION ${emf2svg_VERSION_MAJOR}
)

target_link_libraries(emf2svg
  ${PNG_LIBRARIES}
  ${LIBXML2_LIBRARIES}
  ${EXTERNAL_ICONV}
  ${FREETYPE_LIBRARIES}
  ${FONTCONFIG_LIBRARIES}
  ${EXPAT_LIBRARY_RELEASE}
  ${EXTERNAL_FMEM}
)

if(FMEM_NAME)
  add_dependencies(emf2svg ${FMEM_NAME})
endif()

# WMF2SVG library
add_library(wmf2svg
  ${SHARED}
  src/lib/wmf2svg.c
  ${UWMF_SOURCES}
)

# Add libuemf include directory with high priority for wmf2svg
target_include_directories(wmf2svg BEFORE PRIVATE ${DEPS_UEMF}/include)

set_target_properties(wmf2svg
  PROPERTIES
  VERSION ${emf2svg_VERSION}
  SOVERSION ${emf2svg_VERSION_MAJOR}
)

target_link_libraries(wmf2svg
  ${LIBXML2_LIBRARIES}
  ${EXTERNAL_ICONV}
  ${EXTERNAL_FMEM}
)

if(FMEM_NAME)
  add_dependencies(wmf2svg ${FMEM_NAME})
endif()

if (MSVC)
  # x64: suppress mostly harmless warnings about 64 to 32 bit conversion
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std:c++14 -W3 -D_CRT_SECURE_NO_WARNINGS -wd4244 -wd4267 -wd4305")
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -std:c11 -W3 -D_CRT_SECURE_NO_WARNINGS -wd4244 -wd4267 -wd4305")
else(MSVC)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++14 -Wall")
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -std=c11 -Wall")
endif (MSVC)

if (NOT LIB_INSTALL_DIR)
    set(LIB_INSTALL_DIR lib)
endif ()

if (NOT BIN_INSTALL_DIR)
    set(BIN_INSTALL_DIR bin)
endif ()

if (NOT INCLUDE_INSTALL_DIR)
    set(INCLUDE_INSTALL_DIR include)
endif ()

list(APPEND BINTARGETS emf2svg wmf2svg)

INSTALL(TARGETS ${BINTARGETS}
  RUNTIME DESTINATION ${BIN_INSTALL_DIR}
  LIBRARY DESTINATION ${LIB_INSTALL_DIR}
  ARCHIVE DESTINATION ${LIB_INSTALL_DIR}
)

INSTALL(FILES inc/emf2svg.h inc/wmf2svg.h DESTINATION ${INCLUDE_INSTALL_DIR})
