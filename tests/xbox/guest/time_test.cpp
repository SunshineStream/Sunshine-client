/**
 * @file tests/xbox/guest/time_test.cpp
 * @brief Exercise Sunshine's timing adapters against the Xbox kernel.
 */
#if defined(NXDK)
  // test header include
  #include "src/_nxdk_compat/include/sunshine_time.h"

  // standard includes
  #include <cerrno>

  // lib includes
  #include <lizardbyte/common/testing.h>

  // local includes
  #include "src/_nxdk_compat/include/sys/time.h"

/** @brief Convert known timestamps through the Xbox kernel and preserve caller-owned storage. */
TEST(XboxGuestTime, ConvertsEpochAndLeapDay) {
  std::tm epoch {};
  ASSERT_TRUE(sunshine_gmtime(0, epoch));
  EXPECT_EQ(epoch.tm_year, 70);
  EXPECT_EQ(epoch.tm_mon, 0);
  EXPECT_EQ(epoch.tm_mday, 1);
  EXPECT_EQ(epoch.tm_wday, 4);
  EXPECT_EQ(epoch.tm_yday, 0);

  std::tm leapDay {};
  ASSERT_TRUE(sunshine_gmtime(951827696, leapDay));
  EXPECT_EQ(leapDay.tm_year, 100);
  EXPECT_EQ(leapDay.tm_mon, 1);
  EXPECT_EQ(leapDay.tm_mday, 29);
  EXPECT_EQ(leapDay.tm_hour, 12);
  EXPECT_EQ(leapDay.tm_min, 34);
  EXPECT_EQ(leapDay.tm_sec, 56);
  EXPECT_EQ(leapDay.tm_yday, 59);
  EXPECT_EQ(epoch.tm_mday, 1);
}

/** @brief Reject pre-epoch timestamps without changing caller-owned storage. */
TEST(XboxGuestTime, RejectsNegativeTimestamps) {
  std::tm result {};
  result.tm_year = 123;
  EXPECT_FALSE(sunshine_gmtime(-1, result));
  EXPECT_EQ(result.tm_year, 123);
}

/** @brief Read a normalized Unix timestamp from the running Xbox kernel. */
TEST(XboxGuestTime, ReadsKernelSystemTime) {
  timeval now {};
  ASSERT_EQ(gettimeofday(&now, nullptr), 0);
  EXPECT_GT(now.tv_sec, 0);
  EXPECT_GE(now.tv_usec, 0);
  EXPECT_LT(now.tv_usec, 1000000);
}

/** @brief Reject unsupported gettimeofday arguments without writing to caller storage. */
TEST(XboxGuestTime, RejectsInvalidTimevalArguments) {
  timeval now {123, 456};
  EXPECT_EQ(gettimeofday(nullptr, nullptr), -1);
  EXPECT_EQ(errno, EINVAL);
  EXPECT_EQ(gettimeofday(&now, &now), -1);
  EXPECT_EQ(errno, EINVAL);
  EXPECT_EQ(now.tv_sec, 123);
  EXPECT_EQ(now.tv_usec, 456);
}
#endif
