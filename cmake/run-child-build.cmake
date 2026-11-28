# Configure or build the internal Xbox child project inside the vendored nxdk environment.

foreach(required_var
        SUNSHINE_COMMAND_MODE
        SUNSHINE_CMAKE_COMMAND
        SUNSHINE_DESCRIPTION
        SUNSHINE_NXDK_DIR
        SUNSHINE_BINARY_DIR
        SUNSHINE_WORKING_DIRECTORY)
    if(NOT DEFINED ${required_var} OR "${${required_var}}" STREQUAL "")
        message(FATAL_ERROR "${required_var} must be defined when running run-child-build.cmake")
    endif()
endforeach()

include("${CMAKE_CURRENT_LIST_DIR}/nxdk.cmake")

if(SUNSHINE_COMMAND_MODE STREQUAL "configure")
    foreach(required_var SUNSHINE_SOURCE_DIR SUNSHINE_NXDK_SOURCE_DIR SUNSHINE_MAKE_PROGRAM SUNSHINE_TOOLCHAIN_FILE)
        if(NOT DEFINED ${required_var} OR "${${required_var}}" STREQUAL "")
            message(FATAL_ERROR "${required_var} must be defined for configure mode")
        endif()
    endforeach()

    set(_command_args
            "${SUNSHINE_CMAKE_COMMAND}"
            -G "Unix Makefiles"
            "-DCMAKE_MAKE_PROGRAM:FILEPATH=${SUNSHINE_MAKE_PROGRAM}"
            -S "${SUNSHINE_SOURCE_DIR}"
            -B "${SUNSHINE_BINARY_DIR}"
            -DSUNSHINE_BUILD_KIND:STRING=XBOX
            "-DSUNSHINE_BUILD_VIDEO_EXPERIMENTS:BOOL=${SUNSHINE_BUILD_VIDEO_EXPERIMENTS}"
            "-DSUNSHINE_BUILD_AUDIO_EXPERIMENTS:BOOL=${SUNSHINE_BUILD_AUDIO_EXPERIMENTS}"
            "-DSUNSHINE_BUILD_XBOX_TESTS:BOOL=${SUNSHINE_BUILD_XBOX_TESTS}"
            "-DSUNSHINE_INPUT_DIAGNOSTICS:BOOL=${SUNSHINE_INPUT_DIAGNOSTICS}"
            "-DSUNSHINE_SCREENSHOT_DEMO:BOOL=${SUNSHINE_SCREENSHOT_DEMO}"
            "-DSUNSHINE_NXDK_DIR:PATH=${SUNSHINE_NXDK_SOURCE_DIR}"
            "-DSUNSHINE_NXDK_BUILD_DIR:PATH=${SUNSHINE_NXDK_DIR}"
            -DBUILD_DOCS:BOOL=OFF
            -DCMAKE_EXPORT_COMPILE_COMMANDS:BOOL=ON
            "-DCMAKE_TOOLCHAIN_FILE:FILEPATH=${SUNSHINE_TOOLCHAIN_FILE}"
            -DCMAKE_DEPENDS_USE_COMPILER:BOOL=FALSE
            -DCMAKE_TRY_COMPILE_TARGET_TYPE:STRING=STATIC_LIBRARY
    )

    if(DEFINED SUNSHINE_BUILD_TYPE AND NOT SUNSHINE_BUILD_TYPE STREQUAL "")
        list(APPEND _command_args "-DCMAKE_BUILD_TYPE:STRING=${SUNSHINE_BUILD_TYPE}")
    endif()
elseif(SUNSHINE_COMMAND_MODE STREQUAL "build")
    set(_command_args
            "${SUNSHINE_CMAKE_COMMAND}"
            --build "${SUNSHINE_BINARY_DIR}"
    )
else()
    message(FATAL_ERROR "Unsupported SUNSHINE_COMMAND_MODE: ${SUNSHINE_COMMAND_MODE}")
endif()

sunshine_run_nxdk_command(
        "${SUNSHINE_DESCRIPTION}"
        "${SUNSHINE_NXDK_DIR}"
        "${SUNSHINE_WORKING_DIRECTORY}"
        ${_command_args}
)
