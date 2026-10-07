# Locked In house defaults.
#
# include() this BEFORE project() in every plugin repo:
#
#   cmake_minimum_required(VERSION 3.22)
#   include(locked-in-core/cmake/LockedInDefaults.cmake)
#   project(Trunk VERSION 1.0.0)
#
# It sets things CMake only honours when they exist before project():
# universal Mac binaries, the minimum macOS version, and a static MSVC runtime
# (so Windows customers never see "VCRUNTIME140.dll is missing").

include_guard(GLOBAL)

if(CMAKE_HOST_APPLE)
    if(NOT DEFINED CMAKE_OSX_ARCHITECTURES)
        set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Mac architectures (universal by default)")
    endif()
    if(NOT DEFINED CMAKE_OSX_DEPLOYMENT_TARGET)
        set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")
    endif()
endif()

# Static runtime on Windows (needs policy CMP0091, which cmake_minimum_required >= 3.15 enables).
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" CACHE STRING "MSVC runtime")

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
