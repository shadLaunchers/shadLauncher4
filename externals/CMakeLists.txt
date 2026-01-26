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
    add_subdirectory(ext-fmt)
endif()

# ---- nlohmann/json ----
set(JSON_BuildTests OFF CACHE INTERNAL "")
add_subdirectory(ext-json)

# SDL3
if (NOT TARGET SDL3::SDL3)
    set(SDL_TEST_LIBRARY OFF)
    set(SDL_PIPEWIRE OFF)
    add_subdirectory(ext-sdl3)
endif()

#libdeflate

# enable SIMD instructions
if(MSVC)
    add_compile_options(/O2 /arch:AVX2)
else()
    add_compile_options(-O3 -march=native)
endif()

add_subdirectory(ext-libdeflate)

# staticwindlibs
if (WIN32)
add_subdirectory(staticwinlibs)
endif()
