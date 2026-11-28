/**
 * @file tests/unit/startup/storage_paths_test.cpp
 * @brief Verifies configured screenshot folders and the default save-data subdirectory.
 */
// test header include
#include "src/startup/storage_paths.h"

// lib includes
#include <lizardbyte/common/testing.h>

/**
 * @brief Default screenshots live together under save data; custom Xbox and host separators are preserved.
 */
TEST(StoragePathsTest, ResolvesScreenshotFoldersAndTrailingSeparators) {
#ifdef NXDK
  EXPECT_EQ(startup::screenshot_storage_path("", "capture.bmp"), startup::title_scoped_storage_root() + "screenshots\\capture.bmp");
#else
  EXPECT_EQ(startup::screenshot_storage_path("", "capture.bmp"), startup::default_storage_path("screenshots") + "/capture.bmp");
#endif
  EXPECT_EQ(startup::screenshot_storage_path("F:\\Screenshots", "capture.bmp"), "F:\\Screenshots\\capture.bmp");
  EXPECT_EQ(startup::screenshot_storage_path("F:\\Screenshots\\", "capture.bmp"), "F:\\Screenshots\\capture.bmp");
  EXPECT_EQ(startup::screenshot_storage_path("/tmp/screenshots", "capture.bmp"), "/tmp/screenshots/capture.bmp");
  EXPECT_EQ(startup::screenshot_storage_path("shots/", "capture.bmp"), "shots/capture.bmp");
}
