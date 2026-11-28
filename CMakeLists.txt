# Configure the host-native top-level build, bootstrap vendored nxdk prerequisites,
# and delegate the Xbox target to an internal child configure that uses the stock nxdk toolchain.

cmake_minimum_required(VERSION 3.18)

# Allow third-party subdirectories that use cmake_minimum_required < 3.5 (removed in CMake 4.x)
set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE STRING "")
set(CMAKE_EXPORT_COMPILE_COMMANDS ON CACHE BOOL "Export compile commands for IDE tooling" FORCE)

set(SUNSHINE_BUILD_KIND "HOST" CACHE STRING "Internal Sunshine build mode")
set_property(CACHE SUNSHINE_BUILD_KIND PROPERTY STRINGS HOST XBOX)
option(SUNSHINE_BUILD_VIDEO_EXPERIMENTS "Build standalone GPU presentation and MPEG-2 sampling experiments" OFF)
option(SUNSHINE_BUILD_AUDIO_EXPERIMENTS "Build the standalone Opus playback probe" OFF)
option(SUNSHINE_BUILD_XBOX_TESTS "Build a coverage-instrumented Original Xbox test ISO" OFF)
option(SUNSHINE_INPUT_DIAGNOSTICS "Display digital controller state while diagnosing stream input" OFF)
option(SUNSHINE_SCREENSHOT_DEMO "Use fictional hosts and apps for documentation screenshots" OFF)

if(SUNSHINE_BUILD_KIND STREQUAL "XBOX")
    project(SunshineClient C CXX)
    include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/packages.cmake")

    # add custom modules
    set(CMAKE_MODULE_PATH
            ${CMAKE_MODULE_PATH}
            "${CMAKE_CURRENT_SOURCE_DIR}/cmake/modules/"
    )

    set(CMAKE_CXX_STANDARD 17)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)

    option(BUILD_DOCS "Build documentation" OFF)

    include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/xbox-build.cmake")
    return()
endif()

project(SunshineClient C CXX)
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/packages.cmake")

set(CMAKE_MODULE_PATH
        ${CMAKE_MODULE_PATH}
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/modules/"
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(CMAKE_HOST_WIN32 AND CMAKE_GENERATOR MATCHES "Makefiles")
    set(CMAKE_DEPENDS_USE_COMPILER
            FALSE
            CACHE BOOL
            "Use CMake depfile scanning with Windows makefile generators"
            FORCE)
endif()

#
# Options
#
option(BUILD_DOCS "Build documentation" ON)
option(BUILD_TESTS "Build host-native unit tests" ON)
option(BUILD_XBOX "Build the original Xbox client through an internal nxdk configure" OFF)
# Honor callers and build caches created before BUILD_NATIVE_CLIENT was introduced.
set(_native_client_default ON)
if(DEFINED BUILD_CLIENT)
    set(_native_client_default "${BUILD_CLIENT}")
endif()
option(BUILD_NATIVE_CLIENT "Build the desktop or mobile client for the selected target" ${_native_client_default})
option(SUNSHINE_FORCE_NXDK_DISTCLEAN "Force a fresh nxdk distclean during configure" OFF)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/sources.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/sunshine-dependencies.cmake")

if(BUILD_DOCS)
    include(third-party/dockle/cmake/Dockle.cmake)
    dockle_add_docs(docs TARGETS api)
endif()

if(BUILD_NATIVE_CLIENT AND CMAKE_CROSSCOMPILING)
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)
endif()

if(BUILD_TESTS OR BUILD_NATIVE_CLIENT)
    sunshine_prepare_common_dependencies()
endif()

if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(BUILD_NATIVE_CLIENT)
    include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/client-build.cmake")
endif()

if(BUILD_XBOX)
    include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/nxdk.cmake")

    sunshine_resolve_nxdk_dir(source_nxdk_dir)
    set(_nxdk_build_dir "${SUNSHINE_NXDK_BUILD_DIR}")
    sunshine_stage_nxdk("${source_nxdk_dir}" "${_nxdk_build_dir}")
    sunshine_prepare_nxdk("${_nxdk_build_dir}" "${_nxdk_build_dir}/.moonlight-bootstrap")
    sunshine_add_xbox_build(
            NXDK_DIR "${_nxdk_build_dir}"
            SOURCE_NXDK_DIR "${source_nxdk_dir}"
            BINARY_DIR "${CMAKE_BINARY_DIR}/xbox"
    )
endif()
