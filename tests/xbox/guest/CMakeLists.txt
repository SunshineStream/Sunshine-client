# Use the vendored GoogleTest sources with only the features nxdk supplies.
set(_gtest_source "${CMAKE_SOURCE_DIR}/third-party/lizardbyte-common/third-party/googletest/googletest")
add_library(sunshine_xbox_gtest STATIC "${_gtest_source}/src/gtest-all.cc")
target_include_directories(sunshine_xbox_gtest SYSTEM PUBLIC
        "${_gtest_source}/include"
        "${CMAKE_SOURCE_DIR}/third-party/lizardbyte-common/tests/support/include")
target_include_directories(sunshine_xbox_gtest SYSTEM PRIVATE "${_gtest_source}")
target_include_directories(sunshine_xbox_gtest PUBLIC "${CMAKE_SOURCE_DIR}/src/_nxdk_compat/include")
target_compile_definitions(sunshine_xbox_gtest PUBLIC
        GOOGLETEST_INCLUDE_GTEST_INTERNAL_GTEST_PORT_ARCH_H_
        GTEST_HAS_FILE_SYSTEM=0
        GTEST_HAS_POSIX_RE=0
        GTEST_HAS_PTHREAD=0
        GTEST_HAS_SEH=0
        LIZARDBYTE_COMMON_TESTING_NO_GLOBAL_ALIASES
        LIZARDBYTE_COMMON_TESTING_KEEP_GTEST_TEST)
target_compile_options(sunshine_xbox_gtest PUBLIC
        # The stock nxdk wrapper can lead CMake to probe the host's C++ default on Windows.
        $<$<COMPILE_LANGUAGE:CXX>:-std=gnu++17>
        "$<$<COMPILE_LANGUAGE:CXX>:SHELL:-include \"${CMAKE_CURRENT_SOURCE_DIR}/gtest_port.h\">")

include("${CMAKE_SOURCE_DIR}/tests/sources.cmake")
add_library(sunshine_xbox_coverage OBJECT
        time_test.cpp
        ${SUNSHINE_TESTABLE_SOURCES}
        "${CMAKE_SOURCE_DIR}/src/_nxdk_compat/fenv_compat.cpp"
        "${CMAKE_SOURCE_DIR}/src/_nxdk_compat/ffmpeg_memory.c"
        "${CMAKE_SOURCE_DIR}/src/_nxdk_compat/runtime.c")
target_include_directories(sunshine_xbox_coverage SYSTEM PRIVATE ${SUNSHINE_XBOX_INCLUDE_DIRS})
target_compile_definitions(sunshine_xbox_coverage PRIVATE ${SUNSHINE_XBOX_COMPILE_DEFINITIONS})
target_link_libraries(sunshine_xbox_coverage PRIVATE sunshine_xbox_gtest ${SUNSHINE_XBOX_LIBRARIES})
# nxdk has no compiler-rt profiling library. Link the serial writer separately
# so the writer itself stays uninstrumented.
target_compile_options(sunshine_xbox_coverage PRIVATE -O0 -fprofile-arcs -ftest-coverage)

# Retain nxdk's regular file flushing while redirecting the guest's standard streams.
set(_nxdk_flush "${NXDK_DIR}/lib/pdclib/platform/xbox/functions/_PDCLIB/_PDCLIB_flushbuffer.c")
set_source_files_properties("${_nxdk_flush}" PROPERTIES
        COMPILE_DEFINITIONS "_PDCLIB_flushbuffer=sunshine_pdclib_flush_file")
add_executable(sunshine_xbox_tests main.cpp console_serial.c gcov_serial.c ${SUNSHINE_UNIT_TEST_SOURCES}
        "${_nxdk_flush}"
        $<TARGET_OBJECTS:sunshine_nxdk_net_init>
        $<TARGET_OBJECTS:sunshine_video_scanout>)
target_include_directories(sunshine_xbox_tests SYSTEM PRIVATE ${SUNSHINE_XBOX_INCLUDE_DIRS})
get_target_property(_coverage_sources sunshine_xbox_coverage SOURCES)
list(LENGTH _coverage_sources _coverage_source_count)
target_compile_definitions(sunshine_xbox_tests PRIVATE ${SUNSHINE_XBOX_COMPILE_DEFINITIONS}
        SUNSHINE_GCOV_MAX_WRITERS=${_coverage_source_count})
target_link_libraries(sunshine_xbox_tests PRIVATE
        sunshine_xbox_coverage sunshine_xbox_gtest ${SUNSHINE_XBOX_LIBRARIES})
target_link_options(sunshine_xbox_tests PRIVATE /safeseh:no)

set(_test_xbe_dir "${CMAKE_CURRENT_BINARY_DIR}/xbe")
file(MAKE_DIRECTORY "${_test_xbe_dir}")
add_custom_target(sunshine_xbox_test_iso ALL
        COMMAND ${_cxbe} "-OUT:${_test_xbe_dir}/default.xbe" "-TITLE:Sunshine Tests"
            "$<TARGET_FILE:sunshine_xbox_tests>"
        COMMAND ${_extract_xiso} -c "${_test_xbe_dir}" "SunshineTests.iso"
        BYPRODUCTS "${_test_xbe_dir}/default.xbe" "${CMAKE_BINARY_DIR}/SunshineTests.iso"
        DEPENDS sunshine_xbox_tests
        COMMENT "Package the instrumented Original Xbox tests"
        WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
        VERBATIM)
