/**
 * @file tests/unit/platform/filesystem_utils_test.cpp
 * @brief Verifies filesystem utility helpers.
 */
// test header include
#include "src/platform/filesystem_utils.h"

// standard includes
#include <cstdint>
#include <cstdio>
#include <string>

// lib includes
#include <lizardbyte/common/testing.h>

// test includes
#include "tests/support/filesystem_test_utils.h"

namespace {

  void write_test_file(const std::string &path, std::string_view content) {
    FILE *file = std::fopen(path.c_str(), "wb");
    ASSERT_NE(file, nullptr);
    ASSERT_EQ(std::fwrite(content.data(), 1, content.size(), file), content.size());
    ASSERT_EQ(std::fclose(file), 0);
  }

  class FilesystemUtilsTest: public BaseTest {  // NOSONAR(cpp:S3656) protected members are required by gtest
  protected:
    std::string rootDirectory = test_support::scratch_path("filesystem-utils-test");
    std::string nestedDirectory = test_support::join_path(test_support::join_path(rootDirectory, "level-one"), "level-two");
    std::string filePath = test_support::join_path(nestedDirectory, "payload.bin");

    void TearDown() override {
      test_support::remove_tree_if_present(rootDirectory);
      BaseTest::TearDown();
    }
  };

  TEST_F(FilesystemUtilsTest, JoinsPathsAndFindsParentAndFileNames) {
    EXPECT_EQ(platform::join_path({}, "child.txt"), "child.txt");
    EXPECT_EQ(platform::join_path("parent", {}), "parent");
    EXPECT_EQ(platform::join_path("parent", "child.txt"), test_support::join_path("parent", "child.txt"));
    EXPECT_EQ(platform::join_path(test_support::join_path("parent", "nested"), "child.txt"), test_support::join_path(test_support::join_path("parent", "nested"), "child.txt"));
    EXPECT_EQ(platform::parent_directory(filePath), nestedDirectory);
    EXPECT_EQ(platform::parent_directory("payload.bin"), "");
    EXPECT_EQ(platform::file_name_from_path(filePath), "payload.bin");
    EXPECT_EQ(platform::file_name_from_path("payload.bin"), "payload.bin");
  }

  TEST_F(FilesystemUtilsTest, EnsuresNestedDirectoriesExistAndTreatsEmptyPathsAsNoOp) {
    std::string errorMessage;

    EXPECT_TRUE(platform::ensure_directory_exists({}, &errorMessage)) << errorMessage;
    EXPECT_TRUE(platform::ensure_directory_exists(nestedDirectory, &errorMessage)) << errorMessage;
    EXPECT_TRUE(test_support::is_directory(nestedDirectory));
    EXPECT_TRUE(platform::ensure_directory_exists(nestedDirectory, &errorMessage)) << errorMessage;
  }

#ifdef NXDK
  /**
   * @brief A missing directory on the Xbox disc reports an error even when the caller omits diagnostics.
   */
  TEST(FolderPickerFilesystemTest, RefusesToCreateDirectoriesOnTheReadOnlyXboxDisc) {
    ASSERT_TRUE(test_support::exists("D:\\default.xbe"));
    const std::string directory = "D:\\sunshine-tests-output";
    std::string error;

    EXPECT_FALSE(platform::ensure_directory_exists(directory, &error));
    EXPECT_FALSE(error.empty());
    EXPECT_FALSE(platform::ensure_directory_exists(directory));
    EXPECT_FALSE(test_support::exists(directory));
  }
#endif

  TEST_F(FilesystemUtilsTest, ReportsRegularFileSizesAndRejectsDirectoriesOrMissingPaths) {
    ASSERT_TRUE(platform::ensure_directory_exists(nestedDirectory, nullptr));
    write_test_file(filePath, "payload");

    std::uint64_t sizeBytes = 0;
    EXPECT_TRUE(platform::try_get_file_size(filePath, &sizeBytes));
    EXPECT_EQ(sizeBytes, 7U);
    EXPECT_TRUE(platform::try_get_file_size(filePath, nullptr));
    EXPECT_FALSE(platform::try_get_file_size(nestedDirectory, &sizeBytes));
    EXPECT_FALSE(platform::try_get_file_size(test_support::join_path(rootDirectory, "missing.bin"), &sizeBytes));
  }

  TEST_F(FilesystemUtilsTest, ComparesPrefixesUsingPlatformPathRules) {
    const std::string prefix = test_support::join_path(rootDirectory, "level-one");
    const std::string matchingPath = test_support::join_path(prefix, "child.txt");

    EXPECT_TRUE(platform::path_has_prefix(matchingPath, prefix));
    EXPECT_FALSE(platform::path_has_prefix(matchingPath, {}));
    EXPECT_FALSE(platform::path_has_prefix(prefix, matchingPath));
#if defined(_WIN32) || defined(NXDK)
    EXPECT_TRUE(platform::path_has_prefix("C:\\Sunshine\\Data", "c:\\sunshine"));
#else
    EXPECT_FALSE(platform::path_has_prefix("/Sunshine/Data", "/sunshine"));
#endif
  }

  /**
   * @brief Directory parents preserve absolute drive and POSIX roots and handle trailing separators.
   */
  TEST(FolderPickerFilesystemTest, KeepsRootsAbsoluteWhenGoingUp) {
    EXPECT_EQ(platform::directory_parent("F:\\Shots\\Games\\"), "F:\\Shots");
    EXPECT_EQ(platform::directory_parent("F:\\Shots"), "F:\\");
    EXPECT_EQ(platform::directory_parent("F:\\"), "");
    EXPECT_EQ(platform::directory_parent("F:"), "");
    EXPECT_EQ(platform::directory_parent("/shots/games/"), "/shots");
    EXPECT_EQ(platform::directory_parent("/shots"), "/");
    EXPECT_EQ(platform::directory_parent("/"), "");
    EXPECT_EQ(platform::directory_parent("\\"), "");
    EXPECT_EQ(platform::directory_parent("relative"), "");
    EXPECT_EQ(platform::directory_parent(""), "");
  }

  /**
   * @brief Folder names exclude traversal, reserved characters and Xbox-incompatible component lengths.
   */
  TEST(FolderPickerFilesystemTest, ValidatesSingleFolderNames) {
    for (const std::string name : {"Screenshots", "Game captures", "Shots-2026_(1)", "!#$%&'@^{}~"}) {
      EXPECT_TRUE(platform::valid_directory_name(name)) << name;
    }
    EXPECT_TRUE(platform::valid_directory_name(std::string(42, 'x')));
    EXPECT_FALSE(platform::valid_directory_name(std::string(43, 'x')));
    for (const std::string name : {"", ".", "..", "../outside", "child\\outside", "F:Shots", " trailing ", "end.", "one\ntwo", "\x7f", "bad?", "bad*", "bad|", "bad<", "bad>", "bad\"", "bad+", "bad=", "bad,", "bad;", "bad[", "bad]"}) {
      EXPECT_FALSE(platform::valid_directory_name(name)) << name;
    }
  }

  /**
   * @brief Listing returns sorted immediate folders, including empty ones, and never files or dot entries.
   */
  TEST_F(FilesystemUtilsTest, ListsOnlyImmediateDirectoriesAndReportsUnavailablePaths) {
    ASSERT_TRUE(platform::ensure_directory_exists(nestedDirectory));
    const auto empty = platform::list_directories(nestedDirectory);
    EXPECT_TRUE(empty.error.empty());
    EXPECT_TRUE(empty.paths.empty());
    const auto another = test_support::join_path(rootDirectory, "another");
    ASSERT_TRUE(platform::ensure_directory_exists(another));
    write_test_file(test_support::join_path(rootDirectory, "file.txt"), "keep");
    auto result = platform::list_directories(rootDirectory);
    EXPECT_TRUE(result.error.empty());
    ASSERT_EQ(result.paths.size(), 2);
    EXPECT_EQ(result.paths[0], another);
    EXPECT_EQ(result.paths[1], test_support::join_path(rootDirectory, "level-one"));
    EXPECT_FALSE(platform::list_directories(test_support::join_path(rootDirectory, "missing")).error.empty());
    EXPECT_FALSE(platform::list_directories(test_support::join_path(rootDirectory, "file.txt")).error.empty());
    result = platform::list_directories({});
    EXPECT_TRUE(result.error.empty());
    EXPECT_FALSE(result.paths.empty());
  }

  /**
   * @brief Folder creation refuses existing files/directories and cannot create traversal paths or missing parents.
   */
  TEST_F(FilesystemUtilsTest, CreatesOnlyANewValidatedChildFolder) {
    ASSERT_TRUE(platform::ensure_directory_exists(rootDirectory));
    std::string error = "old";
    EXPECT_TRUE(platform::create_child_directory(rootDirectory, "New folder", error));
    EXPECT_TRUE(error.empty());
    EXPECT_TRUE(test_support::is_directory(test_support::join_path(rootDirectory, "New folder")));
    EXPECT_FALSE(platform::create_child_directory(rootDirectory, "New folder", error));
    EXPECT_FALSE(error.empty());
    write_test_file(test_support::join_path(rootDirectory, "file.txt"), "keep");
    EXPECT_FALSE(platform::create_child_directory(rootDirectory, "file.txt", error));
    EXPECT_FALSE(platform::create_child_directory(rootDirectory, "../escape", error));
    EXPECT_FALSE(platform::create_child_directory({}, "Folder", error));
    EXPECT_FALSE(platform::create_child_directory(test_support::join_path(rootDirectory, "missing"), "Folder", error));
    EXPECT_EQ(test_support::file_size(test_support::join_path(rootDirectory, "file.txt")), 4);
  }

  /**
   * @brief Destination validation removes its own probe and never overwrites a pre-existing probe name.
   */
  TEST_F(FilesystemUtilsTest, ChecksWritabilityWithoutLeavingFilesOrOverwritingCollisions) {
    ASSERT_TRUE(platform::ensure_directory_exists(rootDirectory));
    const auto sentinel = test_support::join_path(rootDirectory, "sunshine-write-1.tmp");
    write_test_file(sentinel, "keep");
    std::string error = "old";
    EXPECT_TRUE(platform::directory_is_writable(rootDirectory, error)) << error;
    EXPECT_TRUE(error.empty());
    EXPECT_EQ(test_support::file_size(sentinel), 4);
    EXPECT_EQ(test_support::entry_count(rootDirectory), 1);
    EXPECT_FALSE(platform::directory_is_writable({}, error));
    EXPECT_FALSE(platform::directory_is_writable(sentinel, error));
    EXPECT_FALSE(platform::directory_is_writable(test_support::join_path(rootDirectory, "missing"), error));
    EXPECT_FALSE(error.empty());
  }

  /**
   * @brief Exhausted temporary names fail safely and leave every existing file intact.
   */
  TEST_F(FilesystemUtilsTest, FailsSafelyWhenAllWriteProbeNamesAlreadyExist) {
    ASSERT_TRUE(platform::ensure_directory_exists(rootDirectory));
    for (int i = 1; i <= 32; ++i) {
      write_test_file(test_support::join_path(rootDirectory, "sunshine-write-" + std::to_string(i) + ".tmp"), "keep");
    }
    std::string error;
    EXPECT_FALSE(platform::directory_is_writable(rootDirectory, error));
    EXPECT_FALSE(error.empty());
    EXPECT_EQ(test_support::entry_count(rootDirectory), 32);
  }

}  // namespace
