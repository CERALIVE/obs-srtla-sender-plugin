# FindOBS.cmake - Find OBS Studio libraries and headers
#
# This module defines the following variables:
#
# OBS_FOUND - True if OBS is found
# OBS_INCLUDE_DIRS - OBS include directories
# OBS_LIBRARIES - OBS libraries
# OBS_FRONTEND_API_LIB - OBS frontend API library

# Find OBS include directory
find_path(OBS_INCLUDE_DIR
    NAMES obs-module.h
    PATHS
        /usr/include
        /usr/include/obs
        /usr/local/include
        /usr/local/include/obs
        /opt/local/include
        /opt/local/include/obs
    PATH_SUFFIXES obs
)

# Find libobs
find_library(OBS_LIB
    NAMES obs libobs
    PATHS
        /usr/lib
        /usr/lib64
        /usr/local/lib
        /usr/local/lib64
        /opt/local/lib
)

# Find obs-frontend-api
find_library(OBS_FRONTEND_API_LIB
    NAMES obs-frontend-api libobs-frontend-api
    PATHS
        /usr/lib
        /usr/lib64
        /usr/local/lib
        /usr/local/lib64
        /opt/local/lib
)

# Set OBS_INCLUDE_DIRS and OBS_LIBRARIES
if(OBS_INCLUDE_DIR AND OBS_LIB)
    set(OBS_INCLUDE_DIRS ${OBS_INCLUDE_DIR})
    set(OBS_LIBRARIES ${OBS_LIB})
    set(OBS_FOUND TRUE)

    message(STATUS "Found OBS: ${OBS_LIB}")
    message(STATUS "OBS include dirs: ${OBS_INCLUDE_DIRS}")
else()
    set(OBS_FOUND FALSE)
    message(STATUS "Could NOT find OBS")
endif()

# Handle the QUIETLY and REQUIRED arguments
include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(OBS DEFAULT_MSG OBS_LIBRARIES OBS_INCLUDE_DIRS)

mark_as_advanced(OBS_INCLUDE_DIR OBS_LIB OBS_FRONTEND_API_LIB)