/**
 * @file tests/xbox/guest/gtest_port.h
 * @brief Adapt GoogleTest's C library calls to the nxdk test environment.
 */
#pragma once

// standard includes
#include <cstdio>
#include <cstring>
#include <iostream>

/**
 * @brief Report that the guest test runner has no interactive terminal.
 * @param fd Stream descriptor.
 * @return Zero for every descriptor.
 */
inline int isatty(int fd) {
  (void) fd;
  return 0;
}

/**
 * @brief Compare strings case-insensitively through nxdk's Windows C library.
 * @param lhs First string.
 * @param rhs Second string.
 * @return The ordering returned by nxdk.
 */
inline int strcasecmp(const char *lhs, const char *rhs) {
  return _stricmp(lhs, rhs);
}

/** @brief Use PDCLib's bounded formatter in the GoogleTest Windows compiler path. */
#define _snprintf_s(buffer, size, count, ...) snprintf(buffer, size, __VA_ARGS__)

/** @brief Route GoogleTest's optional std::cout diagnostics to nxdk's available stream. */
#define cout cerr

// Shared test fixtures can run without the host-only stdio capture hooks.
#include <lizardbyte/common/testing.h>

/** @brief GoogleTest fixture base for shared tests running in the Xbox guest. */
using BaseTest = ::testing::Test;
