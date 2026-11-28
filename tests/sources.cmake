# Desktop and Xbox builds share the regular test suite.
file(GLOB_RECURSE SUNSHINE_UNIT_TEST_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_SOURCE_DIR}/tests/unit/*.cpp")
