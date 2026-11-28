/**
 * @file tests/unit/app/screenshot_library_test.cpp
 * @brief Verifies capture provenance, journal recovery, unique filenames and app-scoped navigation.
 */
// test header include
#include "src/app/screenshot_library.h"

// standard includes
#include <fstream>
#include <limits>

// lib includes
#include <lizardbyte/common/testing.h>

// local includes
#include "src/app/client_state.h"
#include "src/startup/storage_paths.h"
#include "src/ui/shell_view.h"

// test includes
#include "tests/support/filesystem_test_utils.h"

namespace {
  /**
   * @brief Own isolated capture and journal files for one native test.
   */
  class ScreenshotLibraryTest: public BaseTest {  // NOSONAR(cpp:S3656) gtest fixture lifetime
  protected:
    std::string root = test_support::scratch_path("screenshot-library-test");  ///< Fixture-owned storage root.
    std::string journal = test_support::join_path(root, "index.tsv");  ///< Isolated screenshot index.
    app::ScreenshotSource source {"server-one", "192.0.2.1", 47989, "Office PC", 7, "Game"};  ///< Default capture identity.

    /**
     * @brief Remove only fixture-owned files after every test.
     */
    void TearDown() override {
      test_support::remove_tree_if_present(root);
      BaseTest::TearDown();
    }
  };

  /**
   * @brief UUID and app ID isolate libraries despite renamed labels, DHCP changes or identical app names.
   */
  TEST_F(ScreenshotLibraryTest, MatchesStableServerAndAppIdentity) {
    app::HostRecord host {"Office PC", "192.0.2.1"};
    host.uuid = "SERVER-ONE";
    auto requested = app::screenshot_source(host, {"Renamed Game", 7});
    EXPECT_EQ(requested.hostPort, app::DEFAULT_HOST_PORT);
    requested.hostAddress = "192.0.2.2";
    EXPECT_TRUE(app::screenshot_source_matches(source, requested));
    requested.appId = 8;
    EXPECT_FALSE(app::screenshot_source_matches(source, requested));
    requested.appId = 7;
    requested.hostUuid = "server-two";
    requested.hostAddress = source.hostAddress;
    EXPECT_FALSE(app::screenshot_source_matches(source, requested));
    requested.hostUuid.clear();
    EXPECT_TRUE(app::screenshot_source_matches(source, requested));
    requested.hostPort = 48000;
    EXPECT_FALSE(app::screenshot_source_matches(source, requested));
    requested.hostPort = 0;
    EXPECT_TRUE(app::screenshot_source_matches(source, requested));
    requested.hostAddress.clear();
    EXPECT_FALSE(app::screenshot_source_matches(source, requested));
    EXPECT_EQ(app::screenshot_library_path(), startup::default_storage_path("screenshots.tsv"));
  }

  /**
   * @brief Labels and paths round-trip without breaking the journal's line boundaries.
   */
  TEST_F(ScreenshotLibraryTest, EscapesMetadataAndRejectsDamagedRecords) {
    auto record = app::ScreenshotRecord {source, "F:\\My shots\\capture.bmp", 1700000000};
    record.source.hostName = "PC\tname\nwith\\escapes\r";
    record.source.appName = "Game / \"name\"";
    const auto encoded = app::serialize_screenshot(record);
    EXPECT_EQ(encoded.find('\n'), std::string::npos);
    const auto decoded = app::parse_screenshot(encoded);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->source.hostName, record.source.hostName);
    EXPECT_EQ(decoded->source.appName, record.source.appName);
    EXPECT_EQ(decoded->path, record.path);
    EXPECT_EQ(decoded->capturedAt, record.capturedAt);
    for (const auto &invalid : {std::string {}, std::string("2") + encoded.substr(1), encoded + "\tunexpected", encoded.substr(0, encoded.size() - 2), encoded + "\\", std::string(8192, 'x'), encoded + std::string(1, '\0'), encoded + "\n", encoded + "\r"}) {
      EXPECT_FALSE(app::parse_screenshot(invalid).has_value());
    }
    auto badEscape = encoded;
    badEscape.replace(badEscape.find("\\t"), 2, "\\z");
    EXPECT_FALSE(app::parse_screenshot(badEscape).has_value());
    auto badPort = encoded;
    badPort.replace(badPort.find("47989"), 5, "65536");
    EXPECT_FALSE(app::parse_screenshot(badPort).has_value());
    badPort.replace(badPort.find("65536"), 5, "0");
    EXPECT_FALSE(app::parse_screenshot(badPort).has_value());
    auto invalid = record;
    invalid.path.clear();
    EXPECT_TRUE(app::serialize_screenshot(invalid).empty());
    invalid = record;
    invalid.source.appId = -1;
    EXPECT_TRUE(app::serialize_screenshot(invalid).empty());
    invalid = record;
    invalid.capturedAt = -1;
    EXPECT_TRUE(app::serialize_screenshot(invalid).empty());
    invalid = record;
    invalid.source.hostUuid.clear();
    invalid.source.hostAddress.clear();
    EXPECT_TRUE(app::serialize_screenshot(invalid).empty());
    invalid = record;
    invalid.path += '\0';
    EXPECT_TRUE(app::serialize_screenshot(invalid).empty());
    invalid = record;
    invalid.path.assign(8192, 'x');
    EXPECT_TRUE(app::serialize_screenshot(invalid).empty());
    EXPECT_FALSE(app::parse_screenshot("1\t999999999999999999999\t7\t47989\tuuid\tip\thost\tapp\tfile.bmp\tend"));
    EXPECT_FALSE(app::parse_screenshot("1\tnan\t7\t47989\tuuid\tip\thost\tapp\tfile.bmp\tend"));
  }

  /**
   * @brief Captures from old destinations and missing files remain linked, filtered and newest first.
   */
  TEST_F(ScreenshotLibraryTest, LoadsAcrossFoldersAndRecoversAfterInterruptedAppends) {
    EXPECT_TRUE(app::load_screenshots(source, journal).entries.empty());
    EXPECT_TRUE(app::load_screenshots(source, journal).error.empty());
    std::string error;
    EXPECT_TRUE(app::append_screenshot({source, test_support::join_path(test_support::join_path(root, "old"), "capture.bmp"), 10}, journal, error)) << error;
    auto other = source;
    other.appId = 8;
    EXPECT_TRUE(app::append_screenshot({other, test_support::join_path(root, "other.bmp"), 99}, journal, error));
    {
      std::ofstream damaged(journal, std::ios::app | std::ios::binary);
      damaged << std::string(9000, 'x') << "\ninvalid\n"
              << "1\t20\t7\t47989\tserver-one\tip\thost\tapp\tpartial.bmp";
    }
    EXPECT_TRUE(app::append_screenshot({source, test_support::join_path(test_support::join_path(root, "new"), "first.bmp"), 20}, journal, error));
    EXPECT_TRUE(app::append_screenshot({source, test_support::join_path(test_support::join_path(root, "new"), "second.bmp"), 20}, journal, error));
    auto result = app::load_screenshots(source, journal);
    ASSERT_EQ(result.entries.size(), 3U);
    EXPECT_EQ(result.entries[0].path, test_support::join_path(test_support::join_path(root, "new"), "second.bmp"));
    EXPECT_EQ(result.entries[1].path, test_support::join_path(test_support::join_path(root, "new"), "first.bmp"));
    EXPECT_EQ(result.entries[2].path, test_support::join_path(test_support::join_path(root, "old"), "capture.bmp"));
    EXPECT_FALSE(result.error.empty());
    {
      std::ofstream partial(journal, std::ios::app | std::ios::binary);
      partial << app::serialize_screenshot({source, "not-finalized.bmp", 100});
    }
    EXPECT_EQ(app::load_screenshots(source, journal).entries.size(), 3U);
    EXPECT_FALSE(app::append_screenshot({}, journal, error));
    EXPECT_FALSE(error.empty());
    EXPECT_FALSE(app::append_screenshot({source, "file.bmp", 30}, test_support::join_path(journal, "not-a-directory"), error));
    EXPECT_FALSE(error.empty());
    EXPECT_FALSE(app::append_screenshot({source, "file.bmp", 30}, root, error));
    EXPECT_FALSE(error.empty());
  }

  /**
   * @brief Capture names survive restarts and repeated captures in the same second without replacing files.
   */
  TEST_F(ScreenshotLibraryTest, ChoosesUnusedFatxNames) {
    std::string error;
    const std::string first = app::next_screenshot_path(test_support::join_path(root, "shots"), 1700000000, error);
    ASSERT_FALSE(first.empty()) << error;
    {
      std::ofstream image(first);
      image << "original capture";
    }
    const std::string second = app::next_screenshot_path(test_support::join_path(root, "shots"), 1700000000, error);
    EXPECT_NE(first, second);
    EXPECT_LT(test_support::file_name(second).size(), 43U);
    EXPECT_EQ(test_support::file_size(first), 16U);
    EXPECT_TRUE(app::next_screenshot_path(test_support::join_path(first, "blocked"), 1, error).empty());
    EXPECT_FALSE(error.empty());
    EXPECT_TRUE(app::next_screenshot_path(root, -1, error).empty());
    EXPECT_FALSE(error.empty());
    EXPECT_EQ(app::screenshot_time_label(1700000000), "2023-11-14 22:13:20 UTC");
    EXPECT_EQ(app::screenshot_time_label(-1), "Capture time unavailable");
  }

  /**
   * @brief App actions open an offline library, keep navigation inside it and restore the parent menu.
   */
  TEST(ScreenshotViewerTest, OpensBrowsesAndReturnsToAppActions) {
    auto state = app::create_initial_state();
    app::HostRecord host {"Offline PC", "192.0.2.1", 0, app::PairingState::paired, app::HostReachability::offline};
    host.uuid = "server-one";
    host.apps = {{"Game", 7}, {"Other", 8}};
    app::replace_hosts(state, {host});
    ASSERT_TRUE(app::begin_selected_host_app_browse(state, false));
    EXPECT_EQ(state.hosts.active.appListState, app::HostAppListState::ready);
    EXPECT_NE(state.hosts.active.appListStatusMessage.find("PC offline"), std::string::npos);
    app::handle_command(state, input::UiCommand::open_context_menu);
    for (int i = 0; i < 3; ++i) {
      app::handle_command(state, input::UiCommand::move_down);
    }
    auto view = ui::build_shell_view_model(state, {});
    ASSERT_EQ(view.modal.actions.size(), 4U);
    EXPECT_EQ(view.modal.actions[3].id, "view-screenshots");
    EXPECT_TRUE(view.modal.actions[3].selected);
    const auto opened = app::handle_command(state, input::UiCommand::activate);
    EXPECT_TRUE(opened.requests.screenshotsRequested);
    EXPECT_FALSE(opened.requests.appsBrowseRequested);
    EXPECT_EQ(state.modal.id, app::ModalId::screenshot_viewer);
    EXPECT_EQ(state.screenshotViewer.source.hostUuid, "server-one");
    EXPECT_EQ(state.screenshotViewer.source.appId, 7);
    view = ui::build_shell_view_model(state, {});
    EXPECT_TRUE(view.screenshot.visible);
    EXPECT_TRUE(view.screenshot.path.empty());
    EXPECT_NE(view.screenshot.status.find("No screenshots"), std::string::npos);
    app::handle_command(state, input::UiCommand::next_page);
    EXPECT_EQ(state.screenshotViewer.selectedIndex, 0U);
    state.screenshotViewer.entries = {{{}, "first.bmp", 20}, {{}, "second.bmp", 10}};
    app::handle_command(state, input::UiCommand::move_right);
    EXPECT_EQ(state.screenshotViewer.selectedIndex, 1U);
    view = ui::build_shell_view_model(state, {});
    EXPECT_EQ(view.screenshot.path, "second.bmp");
    EXPECT_EQ(view.screenshot.caption, "2 / 2    1970-01-01 00:00:10 UTC");
    app::handle_command(state, input::UiCommand::next_page);
    EXPECT_EQ(state.screenshotViewer.selectedIndex, 0U);
    app::handle_command(state, input::UiCommand::move_left);
    EXPECT_EQ(state.screenshotViewer.selectedIndex, 1U);
    app::handle_command(state, input::UiCommand::previous_page);
    EXPECT_EQ(state.screenshotViewer.selectedIndex, 0U);
    app::handle_command(state, input::UiCommand::activate);
    app::handle_command(state, input::UiCommand::move_down);
    EXPECT_EQ(state.apps.selectedAppIndex, 0U);
    EXPECT_EQ(state.modal.id, app::ModalId::screenshot_viewer);
    state.screenshotViewer.selectedIndex = 999;
    view = ui::build_shell_view_model(state, {});
    EXPECT_EQ(view.screenshot.path, "second.bmp");
    app::handle_command(state, input::UiCommand::previous_page);
    EXPECT_EQ(state.screenshotViewer.selectedIndex, 0U);
    state.shell.inputDevice = input::UiInputDevice::keyboard;
    view = ui::build_shell_view_model(state, {});
    ASSERT_EQ(view.modal.footerActions.size(), 4U);
    EXPECT_EQ(view.modal.footerActions[0].keyLabel, "PgUp");
    EXPECT_EQ(view.modal.footerActions[1].keyLabel, "PgDn");
    EXPECT_EQ(view.modal.footerActions[3].keyLabel, "Esc");
    app::handle_command(state, input::UiCommand::back);
    EXPECT_EQ(state.modal.id, app::ModalId::app_actions);
    EXPECT_EQ(state.modal.selectedActionIndex, 3U);
    EXPECT_TRUE(state.screenshotViewer.entries.empty());
    app::handle_command(state, input::UiCommand::back);
    EXPECT_EQ(state.modal.id, app::ModalId::none);
    app::handle_command(state, input::UiCommand::move_right);
    EXPECT_EQ(state.apps.selectedAppIndex, 1U);
  }

  /**
   * @brief Deletion removes the selected bitmap and persists only its removal across library reloads.
   */
  TEST_F(ScreenshotLibraryTest, DeletesOnlyTheChosenCaptureAndCanReuseItsFilename) {
    std::string error;
    const auto first = app::next_screenshot_path(test_support::join_path(root, "shots"), 10, error);
    const auto otherPath = test_support::join_path(root, "other.bmp");
    {
      std::ofstream image(first);
      image << "first image";
    }
    {
      std::ofstream image(otherPath);
      image << "other app image";
    }
    const app::ScreenshotRecord record {source, first, 10};
    auto other = source;
    other.appId = 8;
    ASSERT_TRUE(app::append_screenshot(record, journal, error));
    ASSERT_TRUE(app::append_screenshot({other, otherPath, 11}, journal, error));
    ASSERT_TRUE(app::delete_screenshot(record, journal, error)) << error;
    EXPECT_FALSE(test_support::exists(first));
    EXPECT_TRUE(test_support::exists(otherPath));
    EXPECT_TRUE(app::load_screenshots(source, journal).entries.empty());
    EXPECT_EQ(app::load_screenshots(other, journal).entries.size(), 1U);
    EXPECT_TRUE(app::load_screenshots(source, journal).error.empty());
    EXPECT_TRUE(app::delete_screenshot(record, journal, error));
    {
      std::ofstream replacement(first);
      replacement << "replacement capture";
    }
    ASSERT_TRUE(app::append_screenshot({source, first, 20}, journal, error));
    const auto reloaded = app::load_screenshots(source, journal);
    ASSERT_EQ(reloaded.entries.size(), 1U);
    EXPECT_EQ(reloaded.entries[0].capturedAt, 20);
    EXPECT_FALSE(reloaded.entries[0].deleted);
    auto tombstone = record;
    tombstone.deleted = true;
    const auto parsed = app::parse_screenshot(app::serialize_screenshot(tombstone));
    ASSERT_TRUE(parsed.has_value());
    EXPECT_TRUE(parsed->deleted);
  }

  /**
   * @brief Invalid metadata, an unavailable index and filesystem errors never silently remove a visible capture.
   */
  TEST_F(ScreenshotLibraryTest, ReportsDeletionFailuresWithoutLosingOtherCaptures) {
    std::string error;
    const std::string first = app::next_screenshot_path(root, 10, error);
    {
      std::ofstream image(first);
      image << "capture";
    }
    const app::ScreenshotRecord record {source, first, 10};
    ASSERT_TRUE(app::append_screenshot(record, journal, error));
    EXPECT_FALSE(app::delete_screenshot({}, journal, error));
    EXPECT_FALSE(error.empty());
    EXPECT_FALSE(app::delete_screenshot(record, test_support::join_path(test_support::join_path(root, "missing"), "index.tsv"), error));
    EXPECT_TRUE(test_support::exists(first));
    ASSERT_TRUE(test_support::create_directory(test_support::join_path(root, "protected.bmp")));
    {
      std::ofstream child(test_support::join_path(test_support::join_path(root, "protected.bmp"), "child.txt"));
      child << "keep";
    }
    EXPECT_FALSE(app::delete_screenshot({source, test_support::join_path(root, "protected.bmp"), 10}, journal, error));
    EXPECT_FALSE(error.empty());
    EXPECT_TRUE(test_support::exists(test_support::join_path(test_support::join_path(root, "protected.bmp"), "child.txt")));
    EXPECT_EQ(app::load_screenshots(source, journal).entries.size(), 1U);
    EXPECT_TRUE(test_support::exists(first));
  }
}  // namespace
