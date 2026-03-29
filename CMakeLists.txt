cmake_minimum_required(VERSION 3.14)
project(LTE_Uplink_Monitor C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

find_package(OpenGL REQUIRED)
find_package(glfw3 REQUIRED)
find_package(GLEW REQUIRED)

# FFTW3f - explicit handling for Homebrew on Apple Silicon / Intel
find_package(PkgConfig REQUIRED)
pkg_check_modules(FFTW3 REQUIRED fftw3f)

# libhackrf
pkg_check_modules(HACKRF QUIET libhackrf)
if(NOT HACKRF_FOUND)
    find_path(HACKRF_INCLUDE_DIR NAMES libhackrf/hackrf.h
              PATHS /opt/homebrew/include /usr/local/include)
    find_library(HACKRF_LIBRARY NAMES hackrf
                 PATHS /opt/homebrew/lib /usr/local/lib)
    if(HACKRF_INCLUDE_DIR AND HACKRF_LIBRARY)
        set(HACKRF_INCLUDE_DIRS ${HACKRF_INCLUDE_DIR})
        set(HACKRF_LIBRARIES ${HACKRF_LIBRARY})
    else()
        message(FATAL_ERROR "libhackrf not found. Run: brew install hackrf")
    endif()
endif()

include_directories(${HACKRF_INCLUDE_DIRS} ${FFTW3_INCLUDE_DIRS})

add_executable(lte_uplink_monitor src/main.c)

target_link_libraries(lte_uplink_monitor
    PRIVATE
        glfw
        GLEW::GLEW
        OpenGL::GL
        ${FFTW3_LIBRARIES}
        ${HACKRF_LIBRARIES}
        m
)

# Explicitly add Homebrew lib path (critical on Apple Silicon)
target_link_directories(lte_uplink_monitor PRIVATE /opt/homebrew/lib /usr/local/lib)

target_compile_options(lte_uplink_monitor PRIVATE -O2 -Wall -Wextra)

message(STATUS "=== Build Configuration ===")
message(STATUS "HackRF include: ${HACKRF_INCLUDE_DIRS}")
message(STATUS "HackRF lib:     ${HACKRF_LIBRARIES}")
message(STATUS "FFTW3 libs:     ${FFTW3_LIBRARIES}")
message(STATUS "FFTW3 include:  ${FFTW3_INCLUDE_DIRS}")
