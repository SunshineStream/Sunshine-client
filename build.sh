#!/bin/bash

# Exit immediately if a command exits with a non-zero status
set -euo pipefail

usage() {
  echo "Usage: $0 [--build-dir <dir> | <dir>] [--clean|clean] [--distclean|distclean] [--xbox-coverage]"
  return 0
}

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
HOST_SYSTEM="$(uname -s)"
case "${HOST_SYSTEM}" in
  MINGW*|MSYS*|CYGWIN*) BUILD_DIR="cmake-build-release-ucrt64" ;;
  *) BUILD_DIR="cmake-build-release" ;;
esac
CLEAN_BUILD=0
ENABLE_XBOX_COVERAGE=OFF
POSITIONAL_BUILD_DIR_SET=0

while [[ "$#" -gt 0 ]]; do
  case "$1" in
    --build-dir|-B)
      if [[ "$#" -lt 2 ]]; then
        usage
        exit 1
      fi
      BUILD_DIR="$2"
      POSITIONAL_BUILD_DIR_SET=1
      shift 2
      ;;
    --clean|clean)
      CLEAN_BUILD=1
      shift
      ;;
    --distclean|distclean)
      CLEAN_BUILD=1
      shift
      ;;
    --xbox-coverage|--coverage)
      ENABLE_XBOX_COVERAGE=ON
      shift
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      if [[ "${POSITIONAL_BUILD_DIR_SET}" -eq 0 ]]; then
        BUILD_DIR="$1"
        POSITIONAL_BUILD_DIR_SET=1
        shift
      else
        echo "Unknown argument: $1"
        usage
        exit 1
      fi
      ;;
  esac
done

case "${BUILD_DIR}" in
  /*)
    BUILD_DIR_PATH="${BUILD_DIR}"
    ;;
  *)
    BUILD_DIR_PATH="${PROJECT_ROOT}/${BUILD_DIR}"
    ;;
esac

cmake_configure_args=(
  -S .
  -B "${BUILD_DIR_PATH}"
  -DBUILD_DOCS=OFF
  -DBUILD_NATIVE_CLIENT=OFF
  -DBUILD_TESTS=OFF
  -DBUILD_XBOX=ON
  -DSUNSHINE_BUILD_XBOX_TESTS="${ENABLE_XBOX_COVERAGE}"
  -DCMAKE_DEPENDS_USE_COMPILER=FALSE
  -DCMAKE_BUILD_TYPE=Release
)

case "${HOST_SYSTEM}" in
  MINGW*|MSYS*|CYGWIN*)
    if [[ "${MSYSTEM:-}" != "UCRT64" ]]; then
      echo "Run this build from the MSYS2 UCRT64 shell or use build-ucrt64.bat."
      exit 1
    fi
    if ! cmake --help 2>/dev/null | grep -q "MinGW Makefiles"; then
      echo "Windows builds require a CMake that supports MinGW Makefiles."
      echo "Install the MSYS2 package 'mingw-w64-ucrt-x86_64-cmake'."
      exit 1
    fi

    cmake_configure_args+=(
      -G "MinGW Makefiles"
      -DCMAKE_TOOLCHAIN_FILE="${PROJECT_ROOT}/cmake/host-ucrt64-clang.cmake"
    )
    ;;
  *)
    ;;
esac

if [[ "${CLEAN_BUILD}" -eq 1 ]]; then
  rm -rf "${BUILD_DIR_PATH}"
fi

cmake "${cmake_configure_args[@]}"

cmake --build "${BUILD_DIR_PATH}"
