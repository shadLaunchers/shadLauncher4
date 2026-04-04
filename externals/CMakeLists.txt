# SPDX-FileCopyrightText: Copyright 2025 shadLauncher4 Project
# SPDX-License-Identifier: GPL-2.0-or-later

cmake_minimum_required(VERSION 3.24)

set(BUILD_SHARED_LIBS OFF)
set(BUILD_TESTING OFF)
set_directory_properties(PROPERTIES
    EXCLUDE_FROM_ALL ON
    SYSTEM ON
)

# ---- fmt ----
if(NOT TARGET fmt::fmt)
    add_subdirectory(fmt)
endif()

# ---- nlohmann/json ----
set(JSON_BuildTests OFF CACHE INTERNAL "")
add_subdirectory(json)


#openal
if (NOT TARGET OpenAL::OpenAL)
set(ALSOFT_ENABLE_MODULES OFF CACHE BOOL "" FORCE)
set(LIBTYPE "STATIC" CACHE STRING "" FORCE)
# Disable everything we don't need
set(ALSOFT_UTILS OFF CACHE BOOL "" FORCE)
set(ALSOFT_EXAMPLES OFF CACHE BOOL "" FORCE)
set(ALSOFT_TESTS OFF CACHE BOOL "" FORCE)
set(ALSOFT_INSTALL OFF CACHE BOOL "" FORCE)
set(ALSOFT_CONFIG OFF CACHE BOOL "" FORCE)

# Backends (platform-specific)
if (WIN32)
    set(ALSOFT_BACKEND_WASAPI ON CACHE BOOL "" FORCE)
    set(ALSOFT_BACKEND_DSOUND OFF CACHE BOOL "" FORCE)
    set(ALSOFT_BACKEND_WINMM OFF CACHE BOOL "" FORCE)
elseif (APPLE)
    set(ALSOFT_BACKEND_COREAUDIO ON CACHE BOOL "" FORCE)
else()
    set(ALSOFT_BACKEND_ALSA ON CACHE BOOL "" FORCE)
    set(ALSOFT_BACKEND_PULSEAUDIO ON CACHE BOOL "" FORCE)
    set(ALSOFT_BACKEND_PIPEWIRE OFF CACHE BOOL "" FORCE)
endif()
# Headless-safe
set(ALSOFT_BACKEND_NULL ON CACHE BOOL "" FORCE)

# Static build
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

add_subdirectory(openal-soft EXCLUDE_FROM_ALL)
endif()

#libdeflate

# enable SIMD instructions
if(MSVC)
    add_compile_options(/O2 /arch:AVX2)
else()
    add_compile_options(-O3 -march=native)
endif()

add_subdirectory(libdeflate)

# staticwindlibs
if (WIN32)
add_subdirectory(staticwinlibs)
else()
# SDL3
if (NOT TARGET SDL3::SDL3)
    set(SDL_TEST_LIBRARY OFF)
    set(SDL_PIPEWIRE OFF)
    add_subdirectory(sdl3)
endif()

endif()
