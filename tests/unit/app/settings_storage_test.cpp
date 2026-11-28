/**
 * @file tests/unit/app/settings_storage_test.cpp
 * @brief Verifies application settings persistence.
 */
// test header include
#include "src/app/settings_storage.h"

// standard includes
#include <cstdio>
#include <string>
#include <string_view>

// lib includes
#include <lizardbyte/common/testing.h>

// test includes
#include "tests/support/filesystem_test_utils.h"

namespace {

  void write_text_file(const std::string &path, std::string_view content) {
    FILE *file = std::fopen(path.c_str(), "wb");
    ASSERT_NE(file, nullptr);
    ASSERT_EQ(std::fwrite(content.data(), 1, content.size(), file), content.size());
    ASSERT_EQ(std::fclose(file), 0);
  }

  class SettingsStorageTest: public BaseTest {  // NOSONAR(cpp:S3656) protected members are required by gtest
  protected:
    std::string testDirectory = test_support::scratch_path("settings-storage-test");
    std::string settingsPath = test_support::join_path(testDirectory, "moonlight.toml");

    void SetUp() override {
      BaseTest::SetUp();
      ASSERT_TRUE(test_support::create_directory(testDirectory));
    }

    void TearDown() override {
      test_support::remove_if_present(settingsPath);
      test_support::remove_directory_if_present(testDirectory);
      BaseTest::TearDown();
    }
  };

  /**
   * @brief Codec migration preserves canonical precedence, screenshot storage and keyboard placement.
   */
  TEST_F(SettingsStorageTest, LoadsCanonicalAndLegacyCodecKeysWithoutLosingPreferences) {
    write_text_file(settingsPath, "[streaming]\nvideo_decoder = \"mpeg2\"\n");
    EXPECT_EQ(app::load_app_settings(settingsPath).settings.videoCodec, app::VideoCodecSelection::mpeg2);
    write_text_file(settingsPath, "[streaming]\nvideo_codec = \"h263p\"\nvideo_decoder = \"h264\"\nscreenshot_directory = 'E:\\Shots'\nkeyboard_dock_top = true\n");
    const auto loaded = app::load_app_settings(settingsPath);
    EXPECT_EQ(loaded.settings.videoCodec, app::VideoCodecSelection::h263p);
    EXPECT_EQ(loaded.settings.screenshotDirectory, R"(E:\Shots)");
    EXPECT_TRUE(loaded.settings.keyboardDockTop);
    EXPECT_TRUE(loaded.warnings.empty());
    ASSERT_TRUE(app::save_app_settings(loaded.settings, settingsPath).success);
    const auto reloaded = app::load_app_settings(settingsPath);
    EXPECT_EQ(reloaded.settings.videoCodec, app::VideoCodecSelection::h263p);
    EXPECT_EQ(reloaded.settings.screenshotDirectory, R"(E:\Shots)");
    EXPECT_TRUE(reloaded.settings.keyboardDockTop);
    EXPECT_TRUE(reloaded.warnings.empty());
  }

  TEST_F(SettingsStorageTest, SavesAndLoadsTomlSettings) {
    const app::AppSettings savedSettings {
      logging::LogLevel::debug,
      logging::LogLevel::warning,
      app::LogViewerPlacement::left,
      VIDEO_MODE {1280, 720, 32, 60},
      true,
      24,
      2500,
      app::VideoCodecSelection::mpeg2,
      true,
      true,
      true,
    };

    const app::SaveAppSettingsResult saveResult = app::save_app_settings(savedSettings, settingsPath);
    ASSERT_TRUE(saveResult.success) << saveResult.errorMessage;

    const app::LoadAppSettingsResult loadResult = app::load_app_settings(settingsPath);
    EXPECT_TRUE(loadResult.fileFound);
    EXPECT_TRUE(loadResult.warnings.empty());
    EXPECT_EQ(loadResult.settings.loggingLevel, logging::LogLevel::debug);
    EXPECT_EQ(loadResult.settings.xemuConsoleLoggingLevel, logging::LogLevel::warning);
    EXPECT_EQ(loadResult.settings.logViewerPlacement, app::LogViewerPlacement::left);
    EXPECT_TRUE(loadResult.settings.preferredVideoModeSet);
    EXPECT_EQ(loadResult.settings.preferredVideoMode.width, 1280);
    EXPECT_EQ(loadResult.settings.preferredVideoMode.height, 720);
    EXPECT_EQ(loadResult.settings.preferredVideoMode.refresh, 60);
    EXPECT_EQ(loadResult.settings.streamFramerate, 24);
    EXPECT_EQ(loadResult.settings.streamBitrateKbps, 2500);
    EXPECT_EQ(loadResult.settings.videoCodec, app::VideoCodecSelection::mpeg2);
    EXPECT_TRUE(loadResult.settings.playAudioOnPc);
    EXPECT_TRUE(loadResult.settings.showPerformanceStats);
    EXPECT_TRUE(loadResult.settings.playAudioOnXbox);
    EXPECT_FALSE(loadResult.cleanupRequired);
  }

  TEST_F(SettingsStorageTest, SavesAndLoadsXboxStreamResolutionModes) {
    const app::AppSettings savedSettings {
      logging::LogLevel::none,
      logging::LogLevel::none,
      app::LogViewerPlacement::full,
      VIDEO_MODE {640, 480, 32, 60},
      true,
      15,
      500,
      app::VideoCodecSelection::h264,
      false,
      false,
      false,
    };

    const app::SaveAppSettingsResult saveResult = app::save_app_settings(savedSettings, settingsPath);
    ASSERT_TRUE(saveResult.success) << saveResult.errorMessage;

    const app::LoadAppSettingsResult loadResult = app::load_app_settings(settingsPath);
    EXPECT_TRUE(loadResult.fileFound);
    EXPECT_TRUE(loadResult.warnings.empty());
    EXPECT_TRUE(loadResult.settings.preferredVideoModeSet);
    EXPECT_EQ(loadResult.settings.preferredVideoMode.width, 640);
    EXPECT_EQ(loadResult.settings.preferredVideoMode.height, 480);
    EXPECT_EQ(loadResult.settings.preferredVideoMode.bpp, 32);
    EXPECT_EQ(loadResult.settings.preferredVideoMode.refresh, 60);
    EXPECT_EQ(loadResult.settings.streamFramerate, 15);
    EXPECT_EQ(loadResult.settings.streamBitrateKbps, 500);
    EXPECT_EQ(loadResult.settings.videoCodec, app::VideoCodecSelection::h264);
    EXPECT_FALSE(loadResult.settings.playAudioOnXbox);
  }

  /** @brief Persist an explicit software selection and default to GPU with old or invalid settings. */
  TEST_F(SettingsStorageTest, SavesAndLoadsPresentationPreference) {
    write_text_file(settingsPath, "[streaming]\nfps = 30\n");
    const auto partial = app::load_app_settings(settingsPath);
    EXPECT_TRUE(partial.settings.gpuPresentation);
    EXPECT_EQ(partial.settings.streamFramerate, 30);
    EXPECT_EQ(partial.settings.streamBitrateKbps, 1000);

    app::AppSettings settings;
    settings.gpuPresentation = false;
    ASSERT_TRUE(app::save_app_settings(settings, settingsPath).success);
    const auto software = app::load_app_settings(settingsPath);
    EXPECT_FALSE(software.settings.gpuPresentation);
    EXPECT_TRUE(software.warnings.empty());
    EXPECT_FALSE(software.cleanupRequired);

    write_text_file(settingsPath, "[streaming]\ngpu_presentation = 'software'\n");
    const auto invalid = app::load_app_settings(settingsPath);
    EXPECT_TRUE(invalid.settings.gpuPresentation);
    ASSERT_EQ(invalid.warnings.size(), 1U);
    EXPECT_NE(invalid.warnings.front().find("streaming.gpu_presentation"), std::string::npos);
  }

  /** @brief Round-trip all GPU buffer choices and default older settings to Auto. */
  TEST_F(SettingsStorageTest, SavesAndLoadsGpuBufferCount) {
    using streaming::GpuBufferCount;
    write_text_file(settingsPath, "[streaming]\nfps = 30\n");
    const auto oldSettings = app::load_app_settings(settingsPath);
    EXPECT_EQ(oldSettings.settings.gpuBufferCount, GpuBufferCount::automatic);
    EXPECT_TRUE(oldSettings.warnings.empty());

    for (const auto count : {GpuBufferCount::automatic, GpuBufferCount::two, GpuBufferCount::three}) {
      app::AppSettings settings;
      settings.gpuBufferCount = count;
      ASSERT_TRUE(app::save_app_settings(settings, settingsPath).success);
      const auto reloaded = app::load_app_settings(settingsPath);
      EXPECT_EQ(reloaded.settings.gpuBufferCount, count);
      EXPECT_TRUE(reloaded.warnings.empty());
      EXPECT_FALSE(reloaded.cleanupRequired);
    }
  }

  /** @brief Accept only the three documented integer values for GPU buffering. */
  TEST_F(SettingsStorageTest, RejectsInvalidGpuBufferCounts) {
    for (const auto value : {
           "-1",
           "1",
           "4",
           "4294967299",
           "inf",
           "'3'",
           "true",
#ifndef NXDK
           // Decimal TOML floats use strtod(), which nxdk leaves unimplemented.
           "3.0",
#endif
         }) {
      SCOPED_TRACE(value);
      write_text_file(settingsPath, std::string("[streaming]\ngpu_buffer_count = ") + value + "\n");
      const auto invalid = app::load_app_settings(settingsPath);
      EXPECT_EQ(invalid.settings.gpuBufferCount, streaming::GpuBufferCount::automatic);
      ASSERT_EQ(invalid.warnings.size(), 1U);
      EXPECT_NE(invalid.warnings.front().find("streaming.gpu_buffer_count"), std::string::npos);
      EXPECT_FALSE(invalid.cleanupRequired);
    }
  }

  /** @brief The new default codec preference round-trips while a saved Auto preference remains valid. */
  TEST_F(SettingsStorageTest, SavesMpeg2PreferenceAndPreservesStandardAuto) {
    app::AppSettings settings;
    ASSERT_TRUE(app::save_app_settings(settings, settingsPath).success);
    const auto preferred = app::load_app_settings(settingsPath);
    EXPECT_EQ(preferred.settings.videoCodec, app::VideoCodecSelection::preferMpeg2);
    EXPECT_TRUE(preferred.warnings.empty());
    write_text_file(settingsPath, "[streaming]\nvideo_codec = 'auto'\n");
    EXPECT_EQ(app::load_app_settings(settingsPath).settings.videoCodec, app::VideoCodecSelection::autoDetect);
  }

  /** @brief A partial settings file derives bitrate from the saved resolution and active frame rate. */
  TEST_F(SettingsStorageTest, DerivesMissingBitrateFromSavedStreamMode) {
    write_text_file(settingsPath, "[streaming]\nvideo_width = 1280\nvideo_height = 720\nvideo_mode_selected = true\n");
    EXPECT_EQ(app::load_app_settings(settingsPath).settings.streamBitrateKbps, 10000);
  }

  /** @brief Apply audio defaults to older files and preserve explicit mode and playback choices. */
  TEST_F(SettingsStorageTest, SavesAndLoadsAudioModes) {
    using streaming::AudioMode;
    write_text_file(settingsPath, "[streaming]\nfps = 30\n");
    const auto oldSettings = app::load_app_settings(settingsPath);
    EXPECT_EQ(oldSettings.settings.audioMode, AudioMode::stereo);
    EXPECT_FALSE(oldSettings.settings.playAudioOnPc);
    EXPECT_TRUE(oldSettings.settings.playAudioOnXbox);
    EXPECT_TRUE(oldSettings.warnings.empty());

    for (const auto mode : {AudioMode::stereo, AudioMode::mono}) {
      app::AppSettings settings;
      settings.audioMode = mode;
      settings.playAudioOnPc = true;
      settings.playAudioOnXbox = false;
      ASSERT_TRUE(app::save_app_settings(settings, settingsPath).success);
      const auto reloaded = app::load_app_settings(settingsPath);
      EXPECT_EQ(reloaded.settings.audioMode, mode);
      EXPECT_TRUE(reloaded.settings.playAudioOnPc);
      EXPECT_FALSE(reloaded.settings.playAudioOnXbox);
      EXPECT_TRUE(reloaded.warnings.empty());
      EXPECT_FALSE(reloaded.cleanupRequired);
    }
  }

  /** @brief Warn about unknown or mistyped audio modes and use the stereo default. */
  TEST_F(SettingsStorageTest, InvalidAudioModesFallBackToStereo) {
    for (const auto *value : {"'5.1'", "6"}) {
      write_text_file(settingsPath, std::string("[streaming]\naudio_mode = ") + value + "\n");
      const auto invalid = app::load_app_settings(settingsPath);
      EXPECT_EQ(invalid.settings.audioMode, streaming::AudioMode::stereo);
      ASSERT_EQ(invalid.warnings.size(), 1U);
      EXPECT_NE(invalid.warnings.front().find("streaming.audio_mode"), std::string::npos);
    }
  }

  /** @brief Missing settings leave the default stream profile and produce no warnings. */
  TEST_F(SettingsStorageTest, MissingFilesReturnDefaultsWithoutWarnings) {
    const app::LoadAppSettingsResult loadResult = app::load_app_settings(settingsPath);

    EXPECT_FALSE(loadResult.fileFound);
    EXPECT_TRUE(loadResult.warnings.empty());
    EXPECT_FALSE(loadResult.cleanupRequired);
    EXPECT_EQ(loadResult.settings.loggingLevel, logging::LogLevel::none);
    EXPECT_EQ(loadResult.settings.xemuConsoleLoggingLevel, logging::LogLevel::none);
    EXPECT_EQ(loadResult.settings.logViewerPlacement, app::LogViewerPlacement::full);
    EXPECT_FALSE(loadResult.settings.preferredVideoModeSet);
    EXPECT_EQ(loadResult.settings.streamFramerate, 60);
    EXPECT_EQ(loadResult.settings.streamBitrateKbps, 3000);
    EXPECT_EQ(loadResult.settings.videoCodec, app::VideoCodecSelection::preferMpeg2);
    EXPECT_TRUE(loadResult.settings.gpuPresentation);
    EXPECT_FALSE(loadResult.settings.playAudioOnPc);
    EXPECT_FALSE(loadResult.settings.showPerformanceStats);
    EXPECT_TRUE(loadResult.settings.playAudioOnXbox);
  }

  /** @brief Invalid saved preferences produce warnings and fall back to the current defaults. */
  TEST_F(SettingsStorageTest, InvalidValuesFallBackToDefaultsWithWarnings) {
    write_text_file(
      settingsPath,
      "[logging]\n"
      "file_minimum_level = \"loud\"\n"
      "xemu_console_minimum_level = \"chatty\"\n\n"
      "[debug]\n"
      "startup_console_enabled = \"sometimes\"\n\n"
      "[ui]\n"
      "log_viewer_placement = \"top\"\n\n"
      "[streaming]\n"
      "video_width = \"wide\"\n"
      "fps = \"fast\"\n"
      "video_decoder = \"vp9\"\n"
      "play_audio_on_pc = \"sometimes\"\n"
      "play_audio_on_xbox = \"sometimes\"\n"
    );

    const app::LoadAppSettingsResult loadResult = app::load_app_settings(settingsPath);
    EXPECT_TRUE(loadResult.fileFound);
    EXPECT_GE(loadResult.warnings.size(), 6U);
    EXPECT_TRUE(loadResult.cleanupRequired);
    EXPECT_EQ(loadResult.settings.loggingLevel, logging::LogLevel::none);
    EXPECT_EQ(loadResult.settings.xemuConsoleLoggingLevel, logging::LogLevel::none);
    EXPECT_EQ(loadResult.settings.logViewerPlacement, app::LogViewerPlacement::full);
    EXPECT_FALSE(loadResult.settings.preferredVideoModeSet);
    EXPECT_EQ(loadResult.settings.streamFramerate, 60);
    EXPECT_EQ(loadResult.settings.videoCodec, app::VideoCodecSelection::preferMpeg2);
    EXPECT_FALSE(loadResult.settings.playAudioOnPc);
    EXPECT_TRUE(loadResult.settings.playAudioOnXbox);
  }

  TEST_F(SettingsStorageTest, LegacyLoggingKeyLoadsAndRequestsCleanup) {
    write_text_file(
      settingsPath,
      "[logging]\n"
      "minimum_level = \"error\"\n\n"
      "[ui]\n"
      "log_viewer_placement = \"right\"\n"
    );

    const app::LoadAppSettingsResult loadResult = app::load_app_settings(settingsPath);

    EXPECT_TRUE(loadResult.fileFound);
    EXPECT_TRUE(loadResult.cleanupRequired);
    EXPECT_EQ(loadResult.settings.loggingLevel, logging::LogLevel::error);
    EXPECT_EQ(loadResult.settings.xemuConsoleLoggingLevel, logging::LogLevel::none);
    EXPECT_EQ(loadResult.settings.logViewerPlacement, app::LogViewerPlacement::right);
  }

  TEST_F(SettingsStorageTest, AcceptsMixedCaseLoggingValuesAndWarnAlias) {
    write_text_file(
      settingsPath,
      "[logging]\n"
      "file_minimum_level = \"DeBuG\"\n"
      "xemu_console_minimum_level = \"WARN\"\n\n"
      "[ui]\n"
      "log_viewer_placement = \"RIGHT\"\n"
    );

    const app::LoadAppSettingsResult loadResult = app::load_app_settings(settingsPath);

    EXPECT_TRUE(loadResult.fileFound);
    EXPECT_TRUE(loadResult.warnings.empty());
    EXPECT_EQ(loadResult.settings.loggingLevel, logging::LogLevel::debug);
    EXPECT_EQ(loadResult.settings.xemuConsoleLoggingLevel, logging::LogLevel::warning);
    EXPECT_EQ(loadResult.settings.logViewerPlacement, app::LogViewerPlacement::right);
  }

  TEST_F(SettingsStorageTest, MarksUnknownKeysAndLegacySectionsForCleanup) {
    write_text_file(
      settingsPath,
      "[logging]\n"
      "file_minimum_level = \"info\"\n"
      "obsolete_key = true\n\n"
      "[ui]\n"
      "log_viewer_placement = \"left\"\n\n"
      "[streaming]\n"
      "fps = 30\n"
      "obsolete_key = true\n"
      "theme = \"green\"\n\n"
      "[debug]\n"
      "startup_console_enabled = true\n\n"
      "[other]\n"
      "value = 1\n"
    );

    const app::LoadAppSettingsResult loadResult = app::load_app_settings(settingsPath);

    EXPECT_TRUE(loadResult.fileFound);
    EXPECT_TRUE(loadResult.cleanupRequired);
    EXPECT_GE(loadResult.warnings.size(), 5U);
    EXPECT_EQ(loadResult.settings.loggingLevel, logging::LogLevel::info);
    EXPECT_EQ(loadResult.settings.logViewerPlacement, app::LogViewerPlacement::left);
    EXPECT_EQ(loadResult.settings.streamFramerate, 30);
  }

  TEST_F(SettingsStorageTest, ReportsParseAndTypeErrorsAsWarnings) {
    write_text_file(
      settingsPath,
      "[logging]\n"
      "file_minimum_level = 7\n"
      "xemu_console_minimum_level = false\n\n"
      "[ui]\n"
      "log_viewer_placement = 42\n\n"
      "[streaming]\n"
      "show_performance_stats = \"sometimes\"\n"
    );

    app::LoadAppSettingsResult loadResult = app::load_app_settings(settingsPath);
    EXPECT_TRUE(loadResult.fileFound);
    EXPECT_GE(loadResult.warnings.size(), 4U);
    EXPECT_EQ(loadResult.settings.loggingLevel, logging::LogLevel::none);
    EXPECT_EQ(loadResult.settings.xemuConsoleLoggingLevel, logging::LogLevel::none);
    EXPECT_EQ(loadResult.settings.logViewerPlacement, app::LogViewerPlacement::full);
    EXPECT_FALSE(loadResult.settings.showPerformanceStats);

    write_text_file(settingsPath, "[logging\nfile_minimum_level = \"info\"\n");
    loadResult = app::load_app_settings(settingsPath);
    EXPECT_TRUE(loadResult.fileFound);
    ASSERT_FALSE(loadResult.warnings.empty());
    EXPECT_NE(loadResult.warnings.front().find("Failed to parse settings file"), std::string::npos);
  }

}  // namespace

/**
 * @brief Screenshot paths with backslashes, quotes and spaces survive TOML serialization with keyboard docking.
 */
TEST_F(SettingsStorageTest, PreservesScreenshotDirectoryAndKeyboardDocking) {
  app::AppSettings settings;
  settings.screenshotDirectory = R"(F:\Screenshots\Sunshine "test")";
  settings.keyboardDockTop = true;
  ASSERT_TRUE(app::save_app_settings(settings, settingsPath).success);
  const auto loaded = app::load_app_settings(settingsPath);
  EXPECT_TRUE(loaded.warnings.empty());
  EXPECT_FALSE(loaded.cleanupRequired);
  EXPECT_EQ(loaded.settings.screenshotDirectory, settings.screenshotDirectory);
  EXPECT_TRUE(loaded.settings.keyboardDockTop);
}

/**
 * @brief Missing settings preserve sensible defaults; malformed paths and docking types produce warnings.
 */
TEST_F(SettingsStorageTest, DefaultsInvalidScreenshotAndDockingSettings) {
  write_text_file(settingsPath, "[streaming]\nscreenshot_directory = 42\nkeyboard_dock_top = 'yes'\n");
  const auto loaded = app::load_app_settings(settingsPath);
  EXPECT_TRUE(loaded.settings.screenshotDirectory.empty());
  EXPECT_FALSE(loaded.settings.keyboardDockTop);
  EXPECT_EQ(loaded.warnings.size(), 2);
  write_text_file(settingsPath, "[streaming]\nscreenshot_directory = \"bad\\npath\"\n");
  EXPECT_TRUE(app::load_app_settings(settingsPath).settings.screenshotDirectory.empty());
}
