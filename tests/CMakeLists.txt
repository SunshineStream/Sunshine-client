# Build the host-native googletest target that exercises platform-neutral
# Sunshine code without pulling the Xbox-only entrypoints into the test binary.

cmake_minimum_required(VERSION 3.13)
# https://github.com/google/oss-policies-info/blob/main/foundational-cxx-support-matrix.md#foundational-c-support

project(test_sunshine LANGUAGES CXX)

if(NOT DEFINED SUNSHINE_TESTABLE_SOURCES)
    message(FATAL_ERROR "tests/CMakeLists.txt must be configured from the top-level CMake project")
endif()

set(GTEST_SOURCE_DIR "${CMAKE_SOURCE_DIR}/third-party/lizardbyte-common/third-party/googletest")
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
set(INSTALL_GMOCK OFF CACHE BOOL "" FORCE)
add_subdirectory("${GTEST_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/googletest" EXCLUDE_FROM_ALL)

set(LIZARDBYTE_COMMON_BUILD_TEST_SUPPORT ON CACHE BOOL "Build lizardbyte-common GoogleTest support helpers" FORCE)
if(NOT TARGET lizardbyte::common)
    add_subdirectory("${CMAKE_SOURCE_DIR}/third-party/lizardbyte-common"
                     "${CMAKE_CURRENT_BINARY_DIR}/lizardbyte-common"
                     EXCLUDE_FROM_ALL)
elseif(NOT TARGET lizardbyte::test_support)
    message(FATAL_ERROR "Sunshine tests require lizardbyte::test_support")
endif()
if(NOT TARGET moonlight-common-c OR NOT TARGET SunshineClient::OpenSSL)
    message(FATAL_ERROR
            "tests/CMakeLists.txt requires the shared Sunshine dependency targets from the top-level configure")
endif()

set(TEST_COVERAGE_COMPILE_OPTIONS)
set(TEST_COVERAGE_LINK_OPTIONS)
if(WIN32)
    set(_coverage_default OFF)
else()
    set(_coverage_default ON)
endif()
option(SUNSHINE_TEST_COVERAGE "Instrument host tests for code coverage" ${_coverage_default})
if(SUNSHINE_TEST_COVERAGE AND CMAKE_CXX_COMPILER_ID MATCHES "Clang" AND WIN32)
    list(APPEND TEST_COVERAGE_COMPILE_OPTIONS -fprofile-instr-generate -fcoverage-mapping -O0)
    # LLD keeps the COFF coverage metadata and its associated counters together.
    list(APPEND TEST_COVERAGE_LINK_OPTIONS -fprofile-instr-generate -fuse-ld=lld)
elseif(SUNSHINE_TEST_COVERAGE AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    list(APPEND TEST_COVERAGE_COMPILE_OPTIONS -fprofile-arcs -ftest-coverage -ggdb -O0)
    list(APPEND TEST_COVERAGE_LINK_OPTIONS -fprofile-arcs -ftest-coverage)
endif()

if(WIN32 OR MINGW OR CMAKE_HOST_WIN32)
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)  # cmake-lint: disable=C0103
endif()

include("${CMAKE_CURRENT_SOURCE_DIR}/sources.cmake")

add_executable(${PROJECT_NAME}
        ${SUNSHINE_UNIT_TEST_SOURCES}
        "${CMAKE_SOURCE_DIR}/src/_nxdk_compat/ffmpeg_memory.c"
        ${SUNSHINE_TESTABLE_SOURCES})

set_target_properties(${PROJECT_NAME} PROPERTIES CXX_STANDARD 17)

target_include_directories(${PROJECT_NAME}
        PRIVATE
        ${CMAKE_SOURCE_DIR}/tests/support
        ${CMAKE_SOURCE_DIR}
        ${tomlplusplus_SOURCE_DIR}/include
        ${GTEST_SOURCE_DIR}/googletest/include
        ${GTEST_SOURCE_DIR}/googlemock/include)
target_link_libraries(${PROJECT_NAME}
        PRIVATE
        gtest_main
        SunshineClient::OpenSSL
        lizardbyte::test_support
        moonlight-common-c)
sunshine_prioritize_bundled_openssl_headers(${PROJECT_NAME})
target_compile_definitions(${PROJECT_NAME}
        PRIVATE
        TOML_EXCEPTIONS=0
        TOML_ENABLE_WINDOWS_COMPAT=0)
if(WIN32 OR MINGW OR CMAKE_HOST_WIN32)
    target_link_libraries(${PROJECT_NAME} PRIVATE ws2_32)
endif()
target_compile_options(${PROJECT_NAME}
        PRIVATE
        ${TEST_COVERAGE_COMPILE_OPTIONS})
target_link_options(${PROJECT_NAME}
        PRIVATE
        ${TEST_COVERAGE_LINK_OPTIONS})

add_dependencies(${PROJECT_NAME} gtest gtest_main gmock gmock_main)

include(GoogleTest)
set(TEST_RUNTIME_PROPERTIES)
if(WIN32 OR MINGW OR CMAKE_HOST_WIN32)
    set(_test_runtime_path "${SUNSHINE_HOST_TOOL_DIR}")
    if(_test_runtime_path STREQUAL "")
        get_filename_component(_test_runtime_path "${CMAKE_CXX_COMPILER}" DIRECTORY)
    endif()

    list(APPEND TEST_RUNTIME_PROPERTIES
            ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${_test_runtime_path}")
endif()

gtest_discover_tests(${PROJECT_NAME}
        DISCOVERY_MODE PRE_TEST
        PROPERTIES ${TEST_RUNTIME_PROPERTIES})

# Compile the production MMIO presenter against deterministic Xbox API shims.
# Keep NXDK and the fake Windows headers isolated from ordinary host tests.
add_executable(test_pvideo_presenter
        "${CMAKE_SOURCE_DIR}/tests/gpu/pvideo_presenter_test.cpp"
        "${CMAKE_SOURCE_DIR}/src/_nxdk_compat/ffmpeg_memory.c"
        "${CMAKE_SOURCE_DIR}/src/streaming/pvideo_presenter.cpp"
        "${CMAKE_SOURCE_DIR}/src/streaming/software_scanout.cpp"
        "${CMAKE_SOURCE_DIR}/src/streaming/video_acceleration.cpp"
        "${CMAKE_SOURCE_DIR}/src/streaming/video_overlay.cpp")
target_compile_definitions(test_pvideo_presenter PRIVATE NXDK)
target_include_directories(test_pvideo_presenter PRIVATE
        "${CMAKE_SOURCE_DIR}/tests/support/pvideo"
        "${CMAKE_SOURCE_DIR}")
target_link_libraries(test_pvideo_presenter PRIVATE gtest_main lizardbyte::test_support)
target_compile_options(test_pvideo_presenter PRIVATE ${TEST_COVERAGE_COMPILE_OPTIONS})
target_link_options(test_pvideo_presenter PRIVATE ${TEST_COVERAGE_LINK_OPTIONS})

gtest_discover_tests(test_pvideo_presenter
        DISCOVERY_MODE PRE_TEST
        PROPERTIES ${TEST_RUNTIME_PROPERTIES})

# Exercise production SDL controller ownership without requiring an SDL installation
# or hardware in the native test build.
add_executable(test_shell_controller
        "${CMAKE_SOURCE_DIR}/tests/controller/shell_controller_test.cpp"
        "${CMAKE_SOURCE_DIR}/src/input/shell_controller.cpp")
target_compile_features(test_shell_controller PRIVATE cxx_std_17)
target_include_directories(test_shell_controller PRIVATE
        "${CMAKE_SOURCE_DIR}/tests/support/shell_controller"
        "${CMAKE_SOURCE_DIR}")
target_link_libraries(test_shell_controller PRIVATE gtest_main lizardbyte::test_support)
target_compile_options(test_shell_controller PRIVATE ${TEST_COVERAGE_COMPILE_OPTIONS})
target_link_options(test_shell_controller PRIVATE ${TEST_COVERAGE_LINK_OPTIONS})

gtest_discover_tests(test_shell_controller
        DISCOVERY_MODE PRE_TEST
        PROPERTIES ${TEST_RUNTIME_PROPERTIES})

# Exercise Xbox UTC conversion with deterministic kernel responses on the host.
add_executable(test_xbox_time "${CMAKE_SOURCE_DIR}/tests/xbox/time_test.cpp")
target_compile_features(test_xbox_time PRIVATE cxx_std_17)
target_include_directories(test_xbox_time PRIVATE
        "${CMAKE_SOURCE_DIR}/tests/support/xbox_time"
        "${CMAKE_SOURCE_DIR}")
target_link_libraries(test_xbox_time PRIVATE gtest_main lizardbyte::test_support)
target_compile_options(test_xbox_time PRIVATE ${TEST_COVERAGE_COMPILE_OPTIONS})
target_link_options(test_xbox_time PRIVATE ${TEST_COVERAGE_LINK_OPTIONS})
gtest_discover_tests(test_xbox_time
        DISCOVERY_MODE PRE_TEST
        PROPERTIES ${TEST_RUNTIME_PROPERTIES})
