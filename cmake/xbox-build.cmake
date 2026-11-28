# Build the Xbox child project with the stock nxdk toolchain, then package the
# resulting executable into the XBE and ISO artifacts consumed by xemu and CI.

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/sources.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/nxdk.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/sunshine-dependencies.cmake")

#
# metadata
#
set(XBE_TITLE Sunshine)
set(XBOX_XBE_DIR "${CMAKE_CURRENT_BINARY_DIR}/xbe")
set(XBOX_ISO_NAME "Sunshine.iso")
if(SUNSHINE_SCREENSHOT_DEMO)
    set(XBE_TITLE "Sunshine Demo")
    set(XBOX_ISO_NAME "SunshineDemo.iso")
endif()
set(XBOX_ISO "${CMAKE_CURRENT_BINARY_DIR}/${XBOX_ISO_NAME}")

if(NOT EXISTS "${NXDK_DIR}/.moonlight-nxdk-copy")
    message(FATAL_ERROR "Use cmake/nxdk-toolchain.cmake to build with an isolated nxdk copy")
endif()
set(ENV{NXDK_DIR} "${NXDK_DIR}")
sunshine_prepare_nxdk("${NXDK_DIR}" "${NXDK_DIR}/.moonlight-bootstrap")

find_package(NXDK REQUIRED)
include("${CMAKE_SOURCE_DIR}/cmake/nxdk-compat.cmake")
sunshine_prepare_nxdk_compat()

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/sdl3.cmake")
sunshine_prepare_sdl3()

# add the automount_d_drive symbol to the linker flags, this is automatic with nxdk when using the Makefile option
# if this is not used, we must add some code to the main function to automount the D drive
# e.g. https://github.com/abaire/nxdk_pgraph_tests/blob/4b7934e6d612a6d17f9ec229a2d72601a5caefc4/src/main.cpp#L118-L122
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -include:_automount_d_drive")

file(MAKE_DIRECTORY "${XBOX_XBE_DIR}")
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/lucide.cmake")

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()
set(CMAKE_CXX_FLAGS_RELEASE "-O2")
set(CMAKE_C_FLAGS_RELEASE "-O2")

SUNSHINE_PREPARE_COMMON_DEPENDENCIES()

# Use nanors' scalar backend on the Xbox CPU, which lacks SSSE3 and newer SIMD
# instructions and cannot link the compiler runtime helpers used for x86 dispatch.
set_property(SOURCE
        "${moonlight-common-c_SOURCE_DIR}/nanors/deps/obl/oblas_lite.c"
        TARGET_DIRECTORY moonlight-common-c
        APPEND PROPERTY COMPILE_OPTIONS
        -include "${CMAKE_SOURCE_DIR}/src/_nxdk_compat/nanors_compat.h"
)

add_executable(sunshine_xbox_executable
        ${SUNSHINE_SOURCES}
)
set_target_properties(sunshine_xbox_executable PROPERTIES OUTPUT_NAME "${CMAKE_PROJECT_NAME}")
target_sources(sunshine_xbox_executable
        PRIVATE
        "${CMAKE_SOURCE_DIR}/src/_nxdk_compat/fenv_compat.cpp"
        $<TARGET_OBJECTS:sunshine_nxdk_net_init>
        $<TARGET_OBJECTS:sunshine_video_scanout>
)
set(SUNSHINE_XBOX_INCLUDE_DIRS
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${SUNSHINE_FFMPEG_INCLUDE_DIR}"
        "${tomlplusplus_SOURCE_DIR}/include"
        "${SUNSHINE_NXDK_NET_INCLUDE_DIR}"
        "${SUNSHINE_NXDK_LIBC_EXTENSIONS_DIR}"
        "${SUNSHINE_NXDK_LWIP_POSIX_COMPAT_DIR}"
        "${NXDK_DIR}/lib/usb/libusbohci/inc"
        "${NXDK_DIR}/lib/usb/libusbohci_xbox"
)
set(SUNSHINE_XBOX_LIBRARIES
        moonlight-common-c
        ${SUNSHINE_FFMPEG_LIBRARIES}
        NXDK::NXDK
        NXDK::NXDK_CXX
        NXDK::Net
        sunshine_nxdk_compat
        sunshine_pbkit_scanout
        SDL3::SDL3
        SDL3_image::SDL3_image-static
        SDL3_ttf::SDL3_ttf-static
        OpenSSL::Crypto
        OpenSSL::SSL
)
target_include_directories(sunshine_xbox_executable SYSTEM PRIVATE ${SUNSHINE_XBOX_INCLUDE_DIRS})
target_link_libraries(sunshine_xbox_executable PUBLIC ${SUNSHINE_XBOX_LIBRARIES})
target_compile_options(sunshine_xbox_executable
        PRIVATE
        ${SUNSHINE_COMPILE_OPTIONS}
        $<$<COMPILE_LANGUAGE:CXX>:-std=gnu++17>
)
# NASM's freestanding i386 objects do not carry Windows SafeSEH metadata; the
# final Xbox image is packaged as XBE and does not use the Windows PE loader.
target_link_options(sunshine_xbox_executable PRIVATE /safeseh:no)
set(SUNSHINE_XBOX_COMPILE_DEFINITIONS
        XBOX
        NXDK
        USBH_USE_EXTERNAL_CONFIG="usbh_config_xbox.h"
        TOML_EXCEPTIONS=0
        TOML_ENABLE_WINDOWS_COMPAT=0
        SUNSHINE_INPUT_DIAGNOSTICS=$<BOOL:${SUNSHINE_INPUT_DIAGNOSTICS}>)
target_compile_definitions(sunshine_xbox_executable PRIVATE ${SUNSHINE_XBOX_COMPILE_DEFINITIONS})
add_dependencies(sunshine_xbox_executable moonlight-common-c)

if(BUILD_DOCS)
    include(third-party/dockle/cmake/Dockle.cmake)
    dockle_add_docs(docs TARGETS api)
endif()

set(_packaging_env)
set(_packaging_tool_suffix "")
if(CMAKE_HOST_WIN32)
    set(_packaging_tool_suffix ".exe")
    if(SUNSHINE_NXDK_NATIVE_COMPILER)
        # cxbe needs the UCRT64 C++ runtime DLLs when launched from CLion.
        sunshine_get_windows_msys2_msystem_bin(_packaging_ucrt_bin)
        set(_packaging_env "${CMAKE_COMMAND}" -E env --modify "PATH=path_list_prepend:${_packaging_ucrt_bin}")
    endif()
endif()
set(_cxbe ${_packaging_env} "${NXDK_DIR}/tools/cxbe/cxbe${_packaging_tool_suffix}")
set(_extract_xiso ${_packaging_env} "${NXDK_DIR}/tools/extract-xiso/build/extract-xiso${_packaging_tool_suffix}")

if(SUNSHINE_BUILD_XBOX_TESTS)
    add_subdirectory(tests/xbox/guest)
endif()

# Expose the same ISO target in the direct Xbox profile and its host parent.
add_custom_target(sunshine_xbox ALL
        COMMAND "${CMAKE_COMMAND}" -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/assets" "${XBOX_XBE_DIR}/assets"
        COMMAND "${CMAKE_COMMAND}" -E copy
            "${NXDK_DIR}/samples/sdl_ttf/vegur-regular.ttf" "${XBOX_XBE_DIR}/assets/fonts/vegur-regular.ttf"
        COMMAND "${CMAKE_COMMAND}"
            "-DLUCIDE_SOURCE_DIR=${SUNSHINE_LUCIDE_SOURCE_DIR}"
            "-DLUCIDE_ASSET_DIR=${XBOX_XBE_DIR}/assets"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/stage-lucide.cmake"
        COMMAND ${_cxbe} "-OUT:${XBOX_XBE_DIR}/default.xbe" "-TITLE:${XBE_TITLE}"
            "$<TARGET_FILE:sunshine_xbox_executable>"
        COMMAND ${_extract_xiso} -c "${XBOX_XBE_DIR}" "${XBOX_ISO_NAME}"
        DEPENDS sunshine_xbox_executable
        BYPRODUCTS "${XBOX_XBE_DIR}/default.xbe" "${XBOX_ISO}"
        COMMENT "Package Sunshine for Original Xbox"
        WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
        VERBATIM
)

if(SUNSHINE_BUILD_AUDIO_EXPERIMENTS)
    add_executable(audio_playback_probe
            "${CMAKE_SOURCE_DIR}/experiments/audio_playback_probe.cpp"
            "${CMAKE_SOURCE_DIR}/src/logging/logger.cpp"
            "${CMAKE_SOURCE_DIR}/src/streaming/audio_configuration.cpp"
            "${CMAKE_SOURCE_DIR}/src/streaming/audio_player.cpp"
            "${CMAKE_SOURCE_DIR}/src/_nxdk_compat/fenv_compat.cpp")
    target_include_directories(audio_playback_probe PRIVATE
            "${CMAKE_SOURCE_DIR}"
            "${SUNSHINE_FFMPEG_INCLUDE_DIR}")
    target_compile_definitions(audio_playback_probe PRIVATE NXDK XBOX)
    target_compile_options(audio_playback_probe PRIVATE -std=gnu++17)
    target_link_options(audio_playback_probe PRIVATE /safeseh:no)
    target_link_libraries(audio_playback_probe PRIVATE
            ${SUNSHINE_FFMPEG_LIBRARIES}
            NXDK::NXDK NXDK::NXDK_CXX SDL3::SDL3 sunshine_nxdk_compat moonlight-common-c)
    set(_audio_playback_probe_xbe_dir "${CMAKE_CURRENT_BINARY_DIR}/audio-playback-probe")
    file(MAKE_DIRECTORY "${_audio_playback_probe_xbe_dir}")
    add_custom_target(audio_playback_probe_iso ALL
            COMMAND ${_cxbe}
                "-OUT:${_audio_playback_probe_xbe_dir}/default.xbe"
                "-TITLE:Opus audio playback probe"
                "$<TARGET_FILE:audio_playback_probe>"
            COMMAND ${_extract_xiso}
                -c "${_audio_playback_probe_xbe_dir}" "AudioPlaybackProbe.iso"
            DEPENDS audio_playback_probe
            WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
            COMMENT "Package the standalone Opus playback probe"
            VERBATIM)
endif()

if(SUNSHINE_BUILD_VIDEO_EXPERIMENTS)
    add_executable(pvideo_presentation_probe
            "${CMAKE_SOURCE_DIR}/experiments/pvideo_presentation_probe.cpp"
            "${CMAKE_SOURCE_DIR}/src/streaming/stats_overlay.cpp"
            "${CMAKE_SOURCE_DIR}/src/streaming/video_acceleration.cpp"
            "${CMAKE_SOURCE_DIR}/src/streaming/pvideo_presenter.cpp"
            "${CMAKE_SOURCE_DIR}/src/streaming/video_overlay.cpp")
    target_include_directories(pvideo_presentation_probe PRIVATE "${CMAKE_SOURCE_DIR}")
    target_compile_definitions(pvideo_presentation_probe PRIVATE NXDK XBOX)
    target_compile_options(pvideo_presentation_probe PRIVATE -std=gnu++17)
    target_link_libraries(pvideo_presentation_probe PRIVATE
            sunshine_nxdk_compat sunshine_pbkit_scanout NXDK::NXDK NXDK::NXDK_CXX)
    set(_pvideo_probe_xbe_dir "${CMAKE_CURRENT_BINARY_DIR}/pvideo-presentation-probe")
    file(MAKE_DIRECTORY "${_pvideo_probe_xbe_dir}")
    add_custom_target(pvideo_presentation_probe_iso ALL
            COMMAND ${_cxbe}
                "-OUT:${_pvideo_probe_xbe_dir}/default.xbe"
                "-TITLE:PVIDEO presentation probe"
                "$<TARGET_FILE:pvideo_presentation_probe>"
            COMMAND ${_extract_xiso}
                -c "${_pvideo_probe_xbe_dir}" "PvideoPresentationProbe.iso"
            DEPENDS pvideo_presentation_probe
            WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
            COMMENT "Package the isolated overlay presentation and teardown probe"
            VERBATIM)

    add_executable(mpeg2_motion_probe
            "${CMAKE_SOURCE_DIR}/experiments/mpeg2_motion_probe.cpp"
            "${CMAKE_SOURCE_DIR}/src/streaming/mpeg2_prediction.cpp"
            "${CMAKE_SOURCE_DIR}/src/streaming/video_acceleration.cpp"
            "${CMAKE_SOURCE_DIR}/src/streaming/pvideo_presenter.cpp"
            "${CMAKE_SOURCE_DIR}/src/streaming/video_overlay.cpp")
    target_include_directories(mpeg2_motion_probe PRIVATE "${CMAKE_SOURCE_DIR}")
    target_compile_definitions(mpeg2_motion_probe PRIVATE NXDK XBOX)
    target_compile_options(mpeg2_motion_probe PRIVATE -std=gnu++17)
    target_link_libraries(mpeg2_motion_probe PRIVATE
            sunshine_nxdk_compat sunshine_pbkit_scanout NXDK::NXDK NXDK::NXDK_CXX)
    set(_mpeg2_probe_xbe_dir "${CMAKE_CURRENT_BINARY_DIR}/mpeg2-motion-probe")
    file(MAKE_DIRECTORY "${_mpeg2_probe_xbe_dir}")
    add_custom_target(mpeg2_motion_probe_iso ALL
            COMMAND ${_cxbe}
                "-OUT:${_mpeg2_probe_xbe_dir}/default.xbe"
                "-TITLE:MPEG-2 motion probe"
                "$<TARGET_FILE:mpeg2_motion_probe>"
            COMMAND ${_extract_xiso}
                -c "${_mpeg2_probe_xbe_dir}" "Mpeg2MotionProbe.iso"
            DEPENDS mpeg2_motion_probe
            WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
            COMMENT "Package the isolated MPEG-2 motion prediction experiment"
            VERBATIM)
endif()
