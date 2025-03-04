cmake_minimum_required(VERSION 3.16)
project(obs-srtla-sender)

# Set default install prefix to /usr if not specified
if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    set(CMAKE_INSTALL_PREFIX "/usr" CACHE PATH "Default install prefix" FORCE)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

# Add cmake modules path
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake/Modules")

# Find OBS using our custom FindOBS.cmake module
find_package(OBS REQUIRED)

# Find Qt6
find_package(Qt6 COMPONENTS Core Widgets REQUIRED)

# Include OBS headers
include_directories(${OBS_INCLUDE_DIRS})

# Output detected OBS paths for debugging
message(STATUS "OBS Include Dirs: ${OBS_INCLUDE_DIRS}")
message(STATUS "OBS Libraries: ${OBS_LIBRARIES}")
message(STATUS "OBS Frontend API: ${OBS_FRONTEND_API_LIB}")
message(STATUS "Qt6 Version: ${Qt6_VERSION}")
message(STATUS "Qt6 Include Dirs: ${Qt6Widgets_INCLUDE_DIRS}")

# Source files
set(SOURCES
    src/plugin-main.cpp
    src/srtla-relay.cpp
    src/network-monitor.cpp)

set(HEADERS
    src/srtla-relay.h
    src/network-monitor.h)

add_library(${PROJECT_NAME} MODULE ${SOURCES} ${HEADERS})

target_link_libraries(${PROJECT_NAME}
    ${OBS_LIBRARIES}
    ${OBS_FRONTEND_API_LIB}
    Qt6::Core
    Qt6::Widgets)

# Remove "lib" prefix for all platforms
set_target_properties(${PROJECT_NAME} PROPERTIES PREFIX "")

# Set default build type to Release if not specified
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build Type" FORCE)
endif()

# Linux-only project, no special suffix needed

# Install - Linux only 
# For Linux, use /usr/lib/obs-plugins by default for system-wide installation
set(OBS_PLUGIN_DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/obs-plugins")

# For data files, use /usr/share/obs/obs-plugins/${PROJECT_NAME}
set(OBS_DATA_DESTINATION "${CMAKE_INSTALL_PREFIX}/share/obs/obs-plugins/${PROJECT_NAME}")

# Set up user plugin path for local installation
set(OBS_USER_PLUGIN_PATH "$ENV{HOME}/.config/obs-studio/plugins/${PROJECT_NAME}/bin/64bit")
set(OBS_USER_DATA_PATH "$ENV{HOME}/.config/obs-studio/plugins/${PROJECT_NAME}/data")

message(STATUS "OBS plugin install path: ${OBS_PLUGIN_DESTINATION}")
message(STATUS "OBS data install path: ${OBS_DATA_DESTINATION}")
message(STATUS "OBS user plugin path: ${OBS_USER_PLUGIN_PATH}")

# Main installation target (system-wide)
install(TARGETS ${PROJECT_NAME}
    LIBRARY DESTINATION ${OBS_PLUGIN_DESTINATION}
    RUNTIME DESTINATION ${OBS_PLUGIN_DESTINATION})
    
# Install data files (locale)
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/data/
    DESTINATION ${OBS_DATA_DESTINATION})

# Add a user-local installation target
add_custom_target(install-user
    COMMAND ${CMAKE_COMMAND} -E make_directory ${OBS_USER_PLUGIN_PATH}
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:${PROJECT_NAME}> ${OBS_USER_PLUGIN_PATH}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${OBS_USER_DATA_PATH}
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/data/ ${OBS_USER_DATA_PATH}
    COMMENT "Installing to user's OBS plugin directory: ${OBS_USER_PLUGIN_PATH}")

# Add a target to help with the Flatpak installation
add_custom_target(install-flatpak
    COMMAND ${CMAKE_COMMAND} -E make_directory $ENV{HOME}/.var/app/com.obsproject.Studio/plugins/
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:${PROJECT_NAME}> $ENV{HOME}/.var/app/com.obsproject.Studio/plugins/
    COMMENT "Installing to Flatpak OBS plugin directory")