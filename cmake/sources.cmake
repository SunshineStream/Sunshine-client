# Define the shared Sunshine source lists used by both the host-native test build
# and the Xbox child build, including the sources that must stay out of host tests.

include_guard(GLOBAL)

set(SUNSHINE_SOURCE_ROOT "${CMAKE_SOURCE_DIR}")

file(GLOB_RECURSE SUNSHINE_SOURCES CONFIGURE_DEPENDS
        "${SUNSHINE_SOURCE_ROOT}/src/*.cpp"
)

list(REMOVE_ITEM SUNSHINE_SOURCES
        "${SUNSHINE_SOURCE_ROOT}/src/_nxdk_compat/fenv_compat.cpp"
        "${SUNSHINE_SOURCE_ROOT}/src/_nxdk_compat/poll_compat.cpp")

set(SUNSHINE_TEST_EXCLUDED_SOURCES
        "${SUNSHINE_SOURCE_ROOT}/src/input/shell_controller.cpp"
        "${SUNSHINE_SOURCE_ROOT}/src/main.cpp"
        "${SUNSHINE_SOURCE_ROOT}/src/streaming/audio_player.cpp"
        "${SUNSHINE_SOURCE_ROOT}/src/splash/splash_screen.cpp"
        "${SUNSHINE_SOURCE_ROOT}/src/streaming/ffmpeg_stream_backend.cpp"
        "${SUNSHINE_SOURCE_ROOT}/src/streaming/ffmpeg_video_frame.cpp"
        "${SUNSHINE_SOURCE_ROOT}/src/streaming/ffmpeg_video_presenter.cpp"
        "${SUNSHINE_SOURCE_ROOT}/src/startup/memory_stats.cpp"
        "${SUNSHINE_SOURCE_ROOT}/src/streaming/session.cpp"
        "${SUNSHINE_SOURCE_ROOT}/src/streaming/stream_controls.cpp"
        "${SUNSHINE_SOURCE_ROOT}/src/platform/usb_input.cpp"
        "${SUNSHINE_SOURCE_ROOT}/src/ui/shell_screen.cpp"
        "${SUNSHINE_SOURCE_ROOT}/src/ui/save_indicator.cpp"
        "${SUNSHINE_SOURCE_ROOT}/src/ui/sunshine_logo.cpp"
        "${SUNSHINE_SOURCE_ROOT}/src/ui/svg_assets.cpp"
)

set(SUNSHINE_TESTABLE_SOURCES ${SUNSHINE_SOURCES})
list(REMOVE_ITEM SUNSHINE_TESTABLE_SOURCES ${SUNSHINE_TEST_EXCLUDED_SOURCES})

if(SUNSHINE_SCREENSHOT_DEMO)
    set_property(SOURCE
            "${SUNSHINE_SOURCE_ROOT}/src/main.cpp"
            "${SUNSHINE_SOURCE_ROOT}/src/splash/splash_screen.cpp"
            "${SUNSHINE_SOURCE_ROOT}/src/startup/storage_paths.cpp"
            "${SUNSHINE_SOURCE_ROOT}/src/ui/shell_screen.cpp"
            APPEND PROPERTY COMPILE_DEFINITIONS SUNSHINE_SCREENSHOT_DEMO)
endif()

set(SUNSHINE_COMPILE_OPTIONS -Wno-builtin-macro-redefined)
