# cmake/FindLibHACKRF.cmake
find_path(LIBHACKRF_INCLUDE_DIR NAMES libhackrf/hackrf.h
          PATHS /opt/homebrew/include /usr/local/include)

find_library(LIBHACKRF_LIBRARIES NAMES hackrf
             PATHS /opt/homebrew/lib /usr/local/lib)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(LibHACKRF
    DEFAULT_MSG
    LIBHACKRF_INCLUDE_DIR
    LIBHACKRF_LIBRARIES
)

if(LIBHACKRF_FOUND)
    set(LIBHACKRF_INCLUDE_DIRS ${LIBHACKRF_INCLUDE_DIR})
endif()

mark_as_advanced(LIBHACKRF_INCLUDE_DIR LIBHACKRF_LIBRARIES)
