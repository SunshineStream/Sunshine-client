/**
 * @file tests/support/filesystem_test_utils.h
 * @brief Provides test support for filesystem test utility helpers.
 */
#pragma once

// standard includes
#include <cstdint>
#include <cstdio>
#include <iterator>
#include <string>
#include <string_view>
#ifndef NXDK
  #include <filesystem>
#endif

// lib includes
#ifdef NXDK
  #include <windows.h>
#endif

namespace test_support {

#ifdef NXDK
  inline constexpr char guest_scratch_directory[] = "E:\\sunshine-tests";  ///< Writable root in the xemu disk snapshot.
#endif

  /**
   * @brief Resolve a fixture path on a writable filesystem.
   * @param name Basename relative to the test's scratch directory.
   * @return Fixture path for the active platform.
   */
  inline std::string scratch_path(const std::string &name) {
#ifdef NXDK
    return std::string(guest_scratch_directory) + '\\' + name;
#else
    return name;
#endif
  }

  /**
   * @brief Join two fixture path components using the active filesystem's separator.
   * @param left Existing directory.
   * @param right Child path.
   * @return Joined path.
   */
  inline std::string join_path(const std::string &left, const std::string &right) {
#ifdef NXDK
    return left + '\\' + right;
#else
    return (std::filesystem::path(left) / right).make_preferred().string();
#endif
  }

  /**
   * @brief Check that the fixture path exists.
   * @param path Fixture path.
   * @return True when the file or directory exists.
   */
  inline bool exists(const std::string &path) {
#ifdef NXDK
    return GetFileAttributesA(path.c_str()) != INVALID_FILE_ATTRIBUTES;
#else
    return std::filesystem::exists(path);
#endif
  }

  /**
   * @brief Check whether a fixture path is a directory.
   * @param path Fixture path.
   * @return True for an existing directory.
   */
  inline bool is_directory(const std::string &path) {
#ifdef NXDK
    const auto attributes = GetFileAttributesA(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
#else
    return std::filesystem::is_directory(path);
#endif
  }

  /**
   * @brief Read the size of a fixture file.
   * @param path Fixture file path.
   * @return Size in bytes, or UINT64_MAX when the Xbox filesystem cannot read the file metadata.
   */
  inline std::uint64_t file_size(const std::string &path) {
#ifdef NXDK
    WIN32_FILE_ATTRIBUTE_DATA attributes {};
    if (!GetFileAttributesExA(path.c_str(), GetFileExInfoStandard, &attributes)) {
      return UINT64_MAX;
    }
    return (static_cast<std::uint64_t>(attributes.nFileSizeHigh) << 32) | attributes.nFileSizeLow;
#else
    return std::filesystem::file_size(path);
#endif
  }

  /**
   * @brief Extract the final name of a fixture path.
   * @param path Path to inspect.
   * @return File or directory name without its parent.
   */
  inline std::string file_name(const std::string &path) {
#ifdef NXDK
    const auto separator = path.find_last_of("\\/");
    return separator == std::string::npos ? path : path.substr(separator + 1);
#else
    return std::filesystem::path(path).filename().string();
#endif
  }

  /** @brief Remove a fixture file, if present. @param path File path. */
  inline void remove_if_present(const std::string &path) {
    std::remove(path.c_str());
  }

  /** @brief Remove an empty fixture directory, if present. @param path Directory path. */
  inline void remove_directory_if_present(const std::string &path) {
#ifdef NXDK
    RemoveDirectoryA(path.c_str());
#else
    std::error_code error;
    std::filesystem::remove(path, error);
#endif
  }

#ifdef NXDK
  /**
   * @brief Visit each immediate child of an Xbox fixture directory.
   * @tparam Visitor Callable receiving the directory record.
   * @param path Directory to list.
   * @param visit Called with each file and subdirectory.
   */
  template<class Visitor>
  void for_each_entry(const std::string &path, Visitor visit) {
    WIN32_FIND_DATAA entry {};
    const auto handle = FindFirstFileA(join_path(path, "*").c_str(), &entry);
    if (handle == INVALID_HANDLE_VALUE) {
      return;
    }
    do {
      const std::string_view name(entry.cFileName);
      if (name != "." && name != "..") {
        visit(entry);
      }
    } while (FindNextFileA(handle, &entry));
    FindClose(handle);
  }
#endif

  /**
   * @brief Count fixture directory entries.
   * @param path Directory to list.
   * @return Number of immediate children.
   */
  inline std::size_t entry_count(const std::string &path) {
#ifdef NXDK
    std::size_t count = 0;
    for_each_entry(path, [&count](const WIN32_FIND_DATAA &) {
      ++count;
    });
    return count;
#else
    return static_cast<std::size_t>(std::distance(std::filesystem::directory_iterator(path), {}));
#endif
  }

  /** @brief Remove a directory owned by the test fixture. @param path Directory to remove. */
  inline void remove_tree_if_present(const std::string &path) {
#ifdef NXDK
    for_each_entry(path, [&path](const WIN32_FIND_DATAA &entry) {
      const auto child = join_path(path, entry.cFileName);
      if ((entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
        remove_tree_if_present(child);
      } else {
        remove_if_present(child);
      }
    });
    remove_directory_if_present(path);
#else
    std::error_code error;
    std::filesystem::remove_all(path, error);
#endif
  }

  /**
   * @brief Create a fixture directory if it does not already exist.
   * @param path Fixture directory.
   * @return True when the directory is ready for use.
   */
#ifdef NXDK
  inline bool create_directory(const std::string &path) {
    return CreateDirectoryA(path.c_str(), nullptr) || is_directory(path);
  }
#else
  inline bool create_directory(const std::filesystem::path &path) {
    std::error_code error;
    return std::filesystem::create_directory(path, error) || std::filesystem::is_directory(path, error);
  }
#endif

}  // namespace test_support
