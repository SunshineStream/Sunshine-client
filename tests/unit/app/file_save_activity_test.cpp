/**
 * @file tests/unit/app/file_save_activity_test.cpp
 * @brief Tests save activity lifetime across simultaneous file writers.
 */
// test header include
#include "src/app/file_save_activity.h"

// standard includes
#include <condition_variable>
#include <mutex>
#include <thread>

// lib includes
#include <lizardbyte/common/testing.h>

/**
 * @brief Verify an early return clears the only tracked save.
 */
TEST(FileSaveActivityTest, ClearsOnEarlyReturn) {
  const auto before = app::file_save_snapshot();
  EXPECT_FALSE(before.active);
  const auto failedWrite = [] {
    const app::FileSaveActivity saving;
    EXPECT_TRUE(app::file_save_snapshot().active);
    return false;
  };
  EXPECT_FALSE(failedWrite());
  const auto after = app::file_save_snapshot();
  EXPECT_FALSE(after.active);
  EXPECT_EQ(after.generation, before.generation + 1);
}

/**
 * @brief Verify the indicator stays active until both the shell and its background writer finish.
 */
TEST(FileSaveActivityTest, RemainsActiveUntilTheLastWriterFinishes) {
  const auto before = app::file_save_snapshot();
  std::mutex mutex;
  std::condition_variable changed;
  bool entered = false;
  bool finish = false;
  // nxdk implements condition variables, but its exception runtime cannot
  // construct the exception state used by std::promise.
  std::thread worker([&changed, &entered, &finish, &mutex] {
    const app::FileSaveActivity saving;
    std::unique_lock lock(mutex);
    entered = true;
    changed.notify_one();
    changed.wait(lock, [&finish] {
      return finish;
    });
  });
  {
    std::unique_lock lock(mutex);
    changed.wait(lock, [&entered] {
      return entered;
    });
  }
  {
    const app::FileSaveActivity saving;
    EXPECT_TRUE(app::file_save_snapshot().active);
    EXPECT_EQ(app::file_save_snapshot().generation, before.generation + 1);
  }
  EXPECT_TRUE(app::file_save_snapshot().active);
  {
    const std::lock_guard lock(mutex);
    finish = true;
  }
  changed.notify_one();
  worker.join();
  EXPECT_FALSE(app::file_save_snapshot().active);
  EXPECT_EQ(app::file_save_snapshot().generation, before.generation + 1);
}
