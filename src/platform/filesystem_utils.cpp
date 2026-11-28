/**
 * @file src/platform/filesystem_utils.cpp
 * @brief Implements filesystem utility helpers.
 */
// class header include
#include "src/platform/filesystem_utils.h"

// standard includes
#include <algorithm>
#include <array>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <string_view>
#include <system_error>

// platform includes
#if defined(_WIN32) || defined(NXDK)
  #include <windows.h>  // NOSONAR(cpp:S3806) nxdk requires lowercase header names
extern "C" {
  #include <direct.h>
}
#else
  #include <dirent.h>
  #include <fcntl.h>
  #include <sys/stat.h>
  #include <sys/types.h>
  #include <unistd.h>
#endif
#ifdef NXDK
  #include <nxdk/mount.h>
#endif

namespace {

#ifdef NXDK
  /**
   * @brief Mount a standard Xbox partition alias on first use, retaining dashboard-provided aliases.
   * @param path Absolute folder whose drive may not have been mounted during direct disc boot.
   */
  void mount_directory_drive(std::string_view path) {
    if (path.size() < 2 || path[1] != ':') {
      return;
    }
    constexpr std::string_view letters = "CEFGXYZ";
    constexpr std::array<int, 7> partitions {2, 1, 6, 7, 3, 4, 5};
    const auto letter = static_cast<char>(std::toupper(static_cast<unsigned char>(path[0])));
    const auto index = letters.find(letter);
    if (index != std::string_view::npos && !nxIsDriveMounted(letter)) {
      const auto target = std::string("\\Device\\Harddisk0\\Partition") + std::to_string(partitions[index]) + "\\";
      nxMountDrive(letter, target.c_str());
    }
  }
#endif

  bool is_root_path(std::string_view path) {
#if defined(_WIN32) || defined(NXDK)
    return path.size() >= 2 && path.size() <= 3 && path[1] == ':';
#else
    return path == "/";
#endif
  }

  std::string normalize_directory_component(std::string path) {
    while (path.size() > 1 && platform::is_path_separator(path.back())) {
      if (is_root_path(path)) {
        break;
      }
      path.pop_back();
    }
    return path;
  }

  /**
   * @brief Create a missing path component or report why it cannot be created.
   * @param path Path component to create.
   * @param errorMessage Receives an optional filesystem error.
   * @return True when the directory is available.
   */
  bool create_directory_if_missing(const std::string &path, std::string *errorMessage) {
#ifdef NXDK
    // nxdk's _mkdir asserts for some kernel errors, including a file used as
    // a directory. Call Win32 directly so the client can report the failure.
    if (CreateDirectoryA(path.c_str(), nullptr)) {
      return true;
    }
    const DWORD error = GetLastError();
    const DWORD attributes = GetFileAttributesA(path.c_str());
    if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
      return true;
    }
    if (errorMessage != nullptr) {
      *errorMessage = "Failed to create directory '" + path + "' (Xbox error " + std::to_string(error) + ").";
    }
#else
  #ifdef _WIN32
    if (_mkdir(path.c_str()) == 0 || errno == EEXIST) {
      return true;
    }
  #else
    if (mkdir(path.c_str(), 0750) == 0 || errno == EEXIST) {
      return true;
    }
  #endif

    if (errorMessage != nullptr) {
      *errorMessage = "Failed to create directory '" + path + "': " + std::error_code(errno, std::generic_category()).message();
    }
#endif
    return false;
  }

  bool path_char_equal(char left, char right) {
#if defined(_WIN32) || defined(NXDK)
    return std::tolower(static_cast<unsigned char>(left)) == std::tolower(static_cast<unsigned char>(right));
#else
    return left == right;
#endif
  }

}  // namespace

namespace platform {

  DirectoryListing list_directories(const std::string &path) {
    DirectoryListing result;
#ifdef NXDK
    mount_directory_drive(path);
#endif
#if defined(_WIN32) || defined(NXDK)
    if (path.empty()) {
  #ifdef NXDK
      // nxdk normally automounts only D:. Expose existing standard disk
      // partitions without replacing aliases supplied by a dashboard.
      for (const char letter : std::string_view("CEFGXYZ")) {
        mount_directory_drive(std::string(1, letter) + ":\\");
      }
  #endif
      const DWORD drives = GetLogicalDrives();
      for (int i = 0; i < 26; ++i) {
        const std::string root = std::string(1, static_cast<char>('A' + i)) + ":\\";
        const auto attributes = (drives & (1UL << i)) != 0 ? GetFileAttributesA(root.c_str()) : INVALID_FILE_ATTRIBUTES;
        if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
          result.paths.push_back(root);
        }
      }
      return result;
    }
    if (const DWORD attributes = GetFileAttributesA(path.c_str()); attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0) {
      result.error = "Folder is unavailable: " + path;
      return result;
    }
    WIN32_FIND_DATAA data {};
    const auto search = join_path(path, "*");
    HANDLE handle = FindFirstFileA(search.c_str(), &data);
    if (handle == INVALID_HANDLE_VALUE) {
      if (GetLastError() != ERROR_FILE_NOT_FOUND && GetLastError() != ERROR_NO_MORE_FILES) {
        result.error = "Unable to read folder (error " + std::to_string(GetLastError()) + ").";
      }
      return result;
    }
    do {
      const std::string name = data.cFileName;
      if (name != "." && name != ".." && (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
        result.paths.push_back(join_path(path, name));
      }
    } while (FindNextFileA(handle, &data));
    const DWORD readError = GetLastError();
    FindClose(handle);
    if (readError != ERROR_NO_MORE_FILES) {
      result.error = "Unable to finish reading folder (error " + std::to_string(readError) + ").";
    }
#else
    if (path.empty()) {
      result.paths = {"/"};
      return result;
    }
    DIR *directory = opendir(path.c_str());
    if (directory == nullptr) {
      result.error = "Unable to read folder: " + std::string(std::error_code(errno, std::generic_category()).message());
      return result;
    }
    errno = 0;
    while (const auto *entry = readdir(directory)) {
      if (const std::string name = entry->d_name; name != "." && name != "..") {
        const auto child = join_path(path, name);
        struct stat status {};
        if (stat(child.c_str(), &status) == 0 && S_ISDIR(status.st_mode)) {
          result.paths.push_back(child);
        }
      }
      errno = 0;
    }
    const int readError = errno;
    closedir(directory);
    if (readError != 0) {
      result.error = "Unable to finish reading folder: " + std::string(std::error_code(readError, std::generic_category()).message());
    }
#endif
    std::sort(result.paths.begin(), result.paths.end());
    return result;
  }

  std::string directory_parent(std::string path) {
    while (path.size() > 1 && is_path_separator(path.back()) && !(path.size() == 3 && path[1] == ':')) {
      path.pop_back();
    }
    if (path == "/" || path == "\\" || (path.size() >= 2 && path.size() <= 3 && path[1] == ':')) {
      return {};
    }
    const auto index = path.find_last_of("\\/");
    if (index == std::string::npos) {
      return {};
    }
    return path.substr(0, index == 0 || (index == 2 && path[1] == ':') ? index + 1 : index);
  }

  bool valid_directory_name(std::string_view name) {
    return !name.empty() && name.size() <= 42 && name != "." && name != ".." && name.back() != ' ' && name.back() != '.' && std::none_of(name.begin(), name.end(), [](unsigned char character) {
      return character < 32 || character >= 127 || std::string_view(R"(<>:"/\|?*+=,;[])").find(static_cast<char>(character)) != std::string_view::npos;
    });
  }

  bool create_child_directory(const std::string &parent, const std::string &name, std::string &error) {
    error.clear();
    if (parent.empty() || !valid_directory_name(name)) {
      error = "Use a name of 1-42 printable characters without reserved characters or a trailing space or period.";
      return false;
    }
#ifdef NXDK
    mount_directory_drive(parent);
#endif
    const auto child = join_path(parent, name);
#if defined(_WIN32) || defined(NXDK)
    if (const int result = _mkdir(child.c_str()); result != 0) {
#else
    if (const int result = mkdir(child.c_str(), 0750); result != 0) {
#endif
      error = errno == EEXIST ? "A file or folder with this name already exists." : "Could not create folder: " + std::string(std::error_code(errno, std::generic_category()).message());
      return false;
    }
    return true;
  }

  bool directory_is_writable(const std::string &path, std::string &error) {
    error.clear();
#ifdef NXDK
    mount_directory_drive(path);
#endif
    if (path.empty()) {
      error = "Choose a folder first.";
      return false;
    }
    for (int attempt = 0; attempt < 32; ++attempt) {
      const auto probe = join_path(path, "sunshine-write-" + std::to_string(attempt + 1) + ".tmp");
#if defined(_WIN32) || defined(NXDK)
      HANDLE file = CreateFileA(probe.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
      if (file == INVALID_HANDLE_VALUE) {
        if (GetLastError() == ERROR_FILE_EXISTS || GetLastError() == ERROR_ALREADY_EXISTS) {
          continue;
        }
        error = "Cannot write to this folder (error " + std::to_string(GetLastError()) + ").";
        return false;
      }
      CloseHandle(file);
      if (!DeleteFileA(probe.c_str())) {
        error = "Cannot remove temporary files in this folder.";
        return false;
      }
#else
      const int file = open(probe.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0600);
      if (file < 0) {
        if (errno == EEXIST) {
          continue;
        }
        error = "Cannot write to this folder: " + std::string(std::error_code(errno, std::generic_category()).message());
        return false;
      }
      close(file);
      if (unlink(probe.c_str()) != 0) {
        error = "Cannot remove temporary files in this folder.";
        return false;
      }
#endif
      return true;
    }
    error = "Could not create a unique temporary file in this folder.";
    return false;
  }

  char preferred_path_separator() {
#if defined(_WIN32) || defined(NXDK)
    return '\\';
#else
    return '/';
#endif
  }

  bool is_path_separator(char character) {
    return character == '\\' || character == '/';
  }

  std::string join_path(const std::string &left, const std::string &right) {
    if (left.empty()) {
      return right;
    }
    if (right.empty()) {
      return left;
    }
    if (is_path_separator(left.back())) {
      return left + right;
    }
    return left + preferred_path_separator() + right;
  }

  std::string parent_directory(std::string_view filePath) {
    const std::size_t separatorIndex = filePath.find_last_of("\\/");
    if (separatorIndex == std::string::npos) {
      return {};
    }
    return std::string(filePath.substr(0, separatorIndex));
  }

  std::string file_name_from_path(std::string_view path) {
    const std::size_t separatorIndex = path.find_last_of("\\/");
    return separatorIndex == std::string::npos ? std::string(path) : std::string(path.substr(separatorIndex + 1U));
  }

  bool ensure_directory_exists(const std::string &directoryPath, std::string *errorMessage) {
#ifdef NXDK
    mount_directory_drive(directoryPath);
#endif
    if (directoryPath.empty()) {
      return true;
    }

    std::string partialPath;
    std::size_t startIndex = 0;
#if defined(_WIN32) || defined(NXDK)
    if (directoryPath.size() >= 2 && directoryPath[1] == ':') {
      partialPath = directoryPath.substr(0, 2);
      startIndex = 2;
    }
#else
    if (is_path_separator(directoryPath.front())) {
      partialPath = "/";
      startIndex = 1;
    }
#endif

    for (std::size_t index = startIndex; index < directoryPath.size(); ++index) {
      partialPath.push_back(directoryPath[index]);
      const bool atSeparator = is_path_separator(directoryPath[index]);
      if (const bool atPathEnd = index + 1 == directoryPath.size(); !atSeparator && !atPathEnd) {
        continue;
      }

      const std::string normalizedPath = normalize_directory_component(partialPath);
      if (normalizedPath.empty() || is_root_path(normalizedPath)) {
        continue;
      }

      if (!create_directory_if_missing(normalizedPath, errorMessage)) {
        return false;
      }
    }

    return true;
  }

  bool try_get_file_size(std::string_view path, std::uint64_t *sizeBytes) {
#ifdef NXDK
    mount_directory_drive(path);
#endif
#if defined(_WIN32) || defined(NXDK)
    WIN32_FILE_ATTRIBUTE_DATA fileData {};
    if (const std::string ownedPath(path); !GetFileAttributesExA(ownedPath.c_str(), GetFileExInfoStandard, &fileData)) {
      return false;
    }
    if ((fileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0U) {
      return false;
    }

    if (sizeBytes != nullptr) {
      *sizeBytes = (static_cast<std::uint64_t>(fileData.nFileSizeHigh) << 32U) |
                   static_cast<std::uint64_t>(fileData.nFileSizeLow);
    }
    return true;
#else
    struct stat status {};
    if (const std::string ownedPath(path); stat(ownedPath.c_str(), &status) != 0 || !S_ISREG(status.st_mode)) {
      return false;
    }

    if (sizeBytes != nullptr) {
      *sizeBytes = static_cast<std::uint64_t>(status.st_size);
    }
    return true;
#endif
  }

  bool path_has_prefix(const std::string &path, const std::string &prefix) {
    if (prefix.empty() || path.size() < prefix.size()) {
      return false;
    }

    for (std::size_t index = 0; index < prefix.size(); ++index) {
      if (!path_char_equal(path[index], prefix[index])) {
        return false;
      }
    }
    return true;
  }

}  // namespace platform
