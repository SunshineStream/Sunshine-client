/**
 * @file tests/unit/splash/splash_layout_test.cpp
 * @brief Verifies splash screen layout calculations.
 */
// test header include
#include "src/splash/splash_layout.h"

// standard includes
#include <array>
#include <cfenv>
#include <limits>
#include <utility>

// lib includes
#include <lizardbyte/common/testing.h>

// local includes
#include "src/app/stream_bitrate.h"

namespace {
  /**
   * @brief Read the active floating-point rounding mode.
   * @return The rounding mode supported by the active target's math runtime.
   */
  int current_rounding_mode() {
#ifdef NXDK
    unsigned short controlWord = 0;
    asm volatile("fnstcw %0" : "=m"(controlWord));
    return controlWord & 0x0c00;
#else
    return std::fegetround();
#endif
  }

  /**
   * @brief Preserve the calling thread's floating-point environment around width regressions.
   */
  class SplashWidthTest: public BaseTest {  // NOSONAR(cpp:S3656) protected members are required by gtest
  protected:
    std::fenv_t originalEnvironment {};  ///< Environment restored after each test.

    /**
     * @brief Save the environment before exercising different rounding modes.
     */
    void SetUp() override {
      BaseTest::SetUp();
#ifdef NXDK
      ASSERT_EQ(std::feholdexcept(&originalEnvironment), 0);
#else
      ASSERT_EQ(std::fegetenv(&originalEnvironment), 0);
#endif
    }

    /**
     * @brief Restore rounding and exception state for subsequent tests.
     */
    void TearDown() override {
      std::fesetenv(&originalEnvironment);
      BaseTest::TearDown();
    }
  };

  /**
   * @brief Stream bitrate calculations and rounding modes must not shift the logical screen width.
   */
  TEST_F(SplashWidthTest, KeepsScreenWidthStableAcrossSettingChangesAndRoundingModes) {
    for (const int roundingMode : std::array {FE_TONEAREST, FE_DOWNWARD, FE_UPWARD, FE_TOWARDZERO}) {
      ASSERT_EQ(std::fesetround(roundingMode), 0);
      for (const int framerate : std::array {15, 20, 24, 25, 30, 60}) {
        const auto bitrate = app::default_stream_bitrate_kbps(1280, 720, framerate);
        ASSERT_GT(bitrate, 0);
        EXPECT_EQ(splash::calculate_display_width(720, {1280, 720, 32, 60}, VIDEO_WIDESCREEN), 1280);
        EXPECT_EQ(splash::calculate_display_width(1080, {1920, 1080, 32, 60}, VIDEO_WIDESCREEN), 1920);
        EXPECT_EQ(splash::calculate_display_width(480, {640, 480, 32, 60}, 0UL), 640);
        EXPECT_EQ(splash::calculate_display_width(720, {1280, 720, 32, 60}, 0UL), 960);
        EXPECT_EQ(splash::calculate_display_width(480, {720, 480, 32, 60}, VIDEO_WIDESCREEN), 853);
        EXPECT_EQ(current_rounding_mode(), roundingMode);
      }
    }
  }

  /**
   * @brief Retain near-matching framebuffer ratios and correct modes outside the aspect tolerance.
   */
  TEST(SplashLayoutTest, ScalesNearbyFramebufferRatiosAndCorrectsOtherModes) {
    EXPECT_EQ(splash::calculate_display_width(360, {1300, 720, 32, 60}, VIDEO_WIDESCREEN), 650);
    EXPECT_EQ(splash::calculate_display_width(720, {1315, 720, 32, 60}, VIDEO_WIDESCREEN), 1315);
    EXPECT_EQ(splash::calculate_display_width(720, {1317, 720, 32, 60}, VIDEO_WIDESCREEN), 1280);
    EXPECT_EQ(splash::calculate_display_width(480, {660, 480, 32, 60}, 0UL), 660);
    EXPECT_EQ(splash::calculate_display_width(480, {665, 480, 32, 60}, 0UL), 640);
    EXPECT_EQ(splash::calculate_display_width(576, {720, 576, 32, 50}, VIDEO_WIDESCREEN), 1024);
  }

  /**
   * @brief Reject invalid dimensions and keep calculated widths inside the integer range.
   */
  TEST(SplashLayoutTest, BoundsLogicalWidthForInvalidOrExtremeDimensions) {
    EXPECT_EQ(splash::calculate_display_width(0, {640, 480, 32, 60}, 0UL), 1);
    EXPECT_EQ(splash::calculate_display_width(-1, {640, 480, 32, 60}, 0UL), 1);
    EXPECT_EQ(splash::calculate_display_width(480, {0, 480, 32, 60}, 0UL), 1);
    EXPECT_EQ(splash::calculate_display_width(480, {640, 0, 32, 60}, 0UL), 1);
    EXPECT_EQ(splash::calculate_display_width(1, {640, 480, 32, 60}, VIDEO_WIDESCREEN), 2);
    constexpr int largestDimension = std::numeric_limits<int>::max();
    EXPECT_EQ(splash::calculate_display_width(largestDimension, {1280, 720, 32, 60}, VIDEO_WIDESCREEN), largestDimension);
    EXPECT_EQ(splash::calculate_display_width(largestDimension, {largestDimension, largestDimension, 32, 60}, 0UL), largestDimension);
  }

  /**
   * @brief Correct standard-definition anamorphic video for a widescreen display.
   */
  TEST(SplashLayoutTest, AppliesWidescreenCorrectionToStandardDefinitionModes) {
    const VIDEO_MODE videoMode {720, 480, 32, 60};

    EXPECT_NEAR(splash::get_display_aspect_ratio(videoMode, VIDEO_WIDESCREEN), 16.0f / 9.0f, 0.001f);
  }

  /**
   * @brief Preserve the aspect ratio of a framebuffer matching the display.
   */
  TEST(SplashLayoutTest, KeepsFramebufferAspectRatioWhenCorrectionIsNotNeeded) {
    const VIDEO_MODE videoMode {1280, 720, 32, 60};

    EXPECT_NEAR(splash::get_display_aspect_ratio(videoMode, VIDEO_WIDESCREEN), 1280.0f / 720.0f, 0.001f);
  }

  /**
   * @brief Correct HD video to four-by-three when widescreen is disabled.
   */
  TEST(SplashLayoutTest, AppliesFourByThreeCorrectionToHighDefinitionModesWhenWidescreenIsDisabled) {
    const VIDEO_MODE videoMode {1280, 720, 32, 60};

    EXPECT_NEAR(splash::get_display_aspect_ratio(videoMode, 0UL), 4.0f / 3.0f, 0.001f);
  }

  /**
   * @brief Calculate the expected logical width for both encoder aspect settings.
   */
  TEST(SplashLayoutTest, CalculatesLogicalDisplayWidthFromTheEffectiveAspectRatio) {
    const VIDEO_MODE videoMode {1280, 720, 32, 60};

    EXPECT_EQ(splash::calculate_display_width(720, videoMode, 0UL), 960);
    EXPECT_EQ(splash::calculate_display_width(720, videoMode, VIDEO_WIDESCREEN), 1280);
  }

  /**
   * @brief Center the logo after fitting it inside the configured bounds.
   */
  TEST(SplashLayoutTest, ScalesAndCentersTheLogoInsideTheConfiguredBounds) {
    const VIDEO_MODE videoMode {640, 480, 32, 60};

    const splash::SplashLayout layout = splash::calculate_logo_destination(640, 480, 1000, 200, videoMode, 0UL);

    EXPECT_EQ(layout.width, 460);
    EXPECT_EQ(layout.height, 92);
    EXPECT_EQ(layout.x, 90);
    EXPECT_EQ(layout.y, 194);
  }

  /**
   * @brief Keep square splash artwork centered as the phone framebuffer rotates.
   */
  TEST(SplashLayoutTest, CentersAnUndistortedLogoInBothPhoneOrientations) {
    const VIDEO_MODE videoMode {1280, 720, 32, 60};
    for (const auto &[width, height] : {std::pair {1080, 2400}, std::pair {2400, 1080}}) {
      const auto layout = splash::calculate_logo_destination(width, height, 512, 512, videoMode, VIDEO_WIDESCREEN);
      EXPECT_GT(layout.width, 0);
      EXPECT_EQ(layout.width, layout.height);
      EXPECT_EQ(layout.x, (width - layout.width) / 2);
      EXPECT_EQ(layout.y, (height - layout.height) / 2);
    }
  }

}  // namespace
