# Copyright (c) Microsoft Corporation. All rights reserved.
# Licensed under the MIT License.

# Create K4A library
add_library(k4a SHARED
            k4a.c
            dll_main.c
            ${CMAKE_CURRENT_BINARY_DIR}/version.rc)



# Generate k4a_export.h
# This is a platform specific header defining the macros for the export functions
# of the shared library. This header is referenced by k4a.h and needs to be installed
# with the SDK
include(GenerateExportHeader)
generate_export_header(k4a
    EXPORT_FILE_NAME "include/k4a/k4a_export.h")

configure_file(
    "${K4A_INCLUDE_DIR}/k4a/k4aversion.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/include/k4a/k4aversion.h"
    )

# Include ${CMAKE_CURRENT_BINARY_DIR}/version.rc in the target's sources
# to embed version information
set(K4A_FILEDESCRIPTION "Azure Kinect SDK")
set(K4A_ORIGINALFILENAME "k4a.dll")
configure_file(
    ${K4A_VERSION_RC}
    ${CMAKE_CURRENT_BINARY_DIR}/version.rc
    @ONLY
    )

# ${K4A_INCLUDE_DIR} contains k4a/k4a.h which is the primary header consumers should include
# ${CMAKE_CURRENT_BINARY_DIR}/include contains the generated k4a/k4a_export.h
target_include_directories(k4a PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>
    $<BUILD_INTERFACE:${K4A_INCLUDE_DIR}>
    $<INSTALL_INTERFACE:include>
    )

# Link in libraries
target_link_libraries(k4a PRIVATE
    k4ainternal::allocator
    k4ainternal::calibration
    k4ainternal::capturesync
    k4ainternal::color
    k4ainternal::color_mcu
    k4ainternal::depth
    k4ainternal::dewrapper
    k4ainternal::depth_mcu
    k4ainternal::image
    k4ainternal::imu
    k4ainternal::logging
    k4ainternal::queue
    k4ainternal::transformation)

# Define alias for k4a
add_library(k4a::k4a ALIAS k4a)

set_target_properties(
    k4a
    PROPERTIES
        VERSION ${K4A_VERSION}
        SOVERSION ${K4A_VERSION_MAJOR})


# Setup install
include(GNUInstallDirs)
install(
    TARGETS
        k4a
    EXPORT
        k4aTargets
    LIBRARY DESTINATION
        ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION
        ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION
        ${CMAKE_INSTALL_BINDIR}
)

include(CMakePackageConfigHelpers)
write_basic_package_version_file(
    k4aConfigVersion.cmake
    VERSION ${K4A_VERSION}
    COMPATIBILITY AnyNewerVersion
    )

install(
    EXPORT
        k4aTargets
    FILE
        k4aTargets.cmake
    NAMESPACE
        k4a::
    DESTINATION
        ${CMAKE_INSTALL_LIBDIR}/cmake/k4a
)

configure_file("${PROJECT_SOURCE_DIR}/cmake/k4aConfig.cmake.in" "${CMAKE_CURRENT_BINARY_DIR}/k4aConfig.cmake" COPYONLY)

install(
    FILES
        "${CMAKE_CURRENT_BINARY_DIR}/k4aConfig.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/k4aConfigVersion.cmake"
    DESTINATION
        ${CMAKE_INSTALL_LIBDIR}/cmake/k4a
)

install(
    FILES
        ${K4A_INCLUDE_DIR}/k4a/k4a.h
        ${K4A_INCLUDE_DIR}/k4a/k4a.hpp
        ${K4A_INCLUDE_DIR}/k4a/k4atypes.h
        ${CMAKE_CURRENT_BINARY_DIR}/include/k4a/k4aversion.h
        ${CMAKE_CURRENT_BINARY_DIR}/include/k4a/k4a_export.h
    DESTINATION
        ${CMAKE_INSTALL_INCLUDEDIR}/k4a
)

if ("${CMAKE_SYSTEM_NAME}" STREQUAL "Windows")
    install(
        FILES
            $<TARGET_PDB_FILE:k4a>
        DESTINATION
            ${CMAKE_INSTALL_BINDIR}
        OPTIONAL
    )
endif()
