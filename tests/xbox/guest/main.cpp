/**
 * @file tests/xbox/guest/main.cpp
 * @brief Report GoogleTest results and coverage to the host from the Xbox test XBE.
 */
// standard includes
#include <string>

// lib includes
#include <lizardbyte/common/testing.h>
#include <synchapi.h>
#include <xboxkrnl/xboxkrnl.h>

// local includes
#include "gcov_serial.h"
#include "src/platform/filesystem_utils.h"

// test includes
#include "tests/support/filesystem_test_utils.h"

namespace {
  /** @brief Emit result records on COM2 for the test runner. */
  class SerialResults: public testing::EmptyTestEventListener {
    /**
     * @brief Emit a result record after each test.
     * @param info The completed test.
     */
    void OnTestEnd(const testing::TestInfo &info) override {
      const auto *result = info.result();
      const char *status = result->Passed() ? "PASS" : "FAIL";
      if (result->Skipped()) {
        status = "SKIP";
      }
      const auto record = std::string("SUNSHINE_TEST_CASE ") + status + ' ' + info.test_suite_name() + ' ' + info.name() + "\r\n";
      sunshine_write_guest_text(record.c_str());
    }
  };
}  // namespace

/**
 * @brief Run the instrumented guest tests and report completion after exporting counters.
 * @return This process waits for the emulator runner to shut down the guest.
 */
int main() {
  sunshine_prepare_guest_records();
  if (!platform::ensure_directory_exists(test_support::guest_scratch_directory)) {
    DbgPrint("Failed to create the Xbox guest test directory\r\n");
    sunshine_write_guest_text("SUNSHINE_TEST_DONE 1 0\r\n");
    return 1;
  }
  testing::InitGoogleTest();
  auto *tests = testing::UnitTest::GetInstance();
  tests->listeners().Append(new SerialResults);

  const int result = RUN_ALL_TESTS();
  sunshine_write_guest_coverage();
  const auto done = "SUNSHINE_TEST_DONE " + std::to_string(result) + ' ' + std::to_string(tests->test_to_run_count()) + "\r\n";
  sunshine_write_guest_text(done.c_str());
  Sleep(INFINITE);
  return result;
}
