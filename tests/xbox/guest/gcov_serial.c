/**
 * @file tests/xbox/guest/gcov_serial.c
 * @brief Write Clang's gcda records to xemu over COM2 after the guest tests.
 */
// standard includes
#include <stddef.h>
#include <stdint.h>

// local includes
#include "gcov_serial.h"

/** @brief Clang-generated function that writes a translation unit's profile. */
typedef void (*GcovWriter)(void);

static GcovWriter writers[SUNSHINE_GCOV_MAX_WRITERS + 1];  ///< Registered at startup, with a null sentinel at the end.
static size_t writer_count;  ///< Number of successfully registered writers.
static char serial_hex[129];  ///< Holds up to 64 bytes encoded as hex plus a terminator.
static size_t serial_size;  ///< Number of encoded characters currently in the buffer.

/** @brief Xbox port addresses used for the second serial channel. */
enum {
  SUPER_IO_INDEX = 0x2e,
  SUPER_IO_DATA = 0x2f,
  COVERAGE_UART = 0x2f8
};

/**
 * @brief Write one byte to an Xbox I/O port.
 * @param port I/O port address.
 * @param value Byte to write.
 */
static void write_port(uint16_t port, uint8_t value) {
  __asm__ volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}

/**
 * @brief Set a register in the LPC47M157.
 * @param reg Index of the configuration register.
 * @param value Byte to write.
 */
static void set_super_io_register(uint8_t reg, uint8_t value) {
  write_port(SUPER_IO_INDEX, reg);
  write_port(SUPER_IO_DATA, value);
}

void sunshine_prepare_guest_records(void) {
  // Enable the second serial device, using polling so the guest does not need an IRQ handler.
  write_port(SUPER_IO_INDEX, 0x55);  // Enter configuration mode.
  set_super_io_register(0x07, 0x05);  // Select COM2.
  set_super_io_register(0x60, COVERAGE_UART >> 8);
  set_super_io_register(0x61, COVERAGE_UART & 0xff);
  set_super_io_register(0x70, 0);
  set_super_io_register(0x30, 1);
  write_port(SUPER_IO_INDEX, 0xaa);  // Apply the configuration.

  write_port(COVERAGE_UART + 1, 0);  // Disable UART interrupts.
  write_port(COVERAGE_UART + 3, 0x80);  // Select the baud divisor registers.
  write_port(COVERAGE_UART, 1);  // Use 115200 baud.
  write_port(COVERAGE_UART + 1, 0);
  write_port(COVERAGE_UART + 3, 3);  // Eight data bits, no parity, one stop bit.
}

void sunshine_write_guest_text(const char *text) {
  // COM1 stays available to DbgPrint and GoogleTest throughout the run.
  for (const char *byte = text; *byte != '\0'; ++byte) {
    uint8_t line_status;
    do {
      __asm__ volatile("inb %1, %0" : "=a"(line_status) : "Nd"((uint16_t) (COVERAGE_UART + 5)));
    } while ((line_status & 0x20) == 0);  // Wait for the transmitter to accept the byte.
    write_port(COVERAGE_UART, (uint8_t) *byte);
  }
}

/** @brief Send a complete gcda record line through the coverage serial port. */
static void flush_serial(void) {
  if (serial_size != 0) {
    serial_hex[serial_size] = '\0';
    sunshine_write_guest_text("SUNSHINE_GCOV_DATA ");
    sunshine_write_guest_text(serial_hex);
    sunshine_write_guest_text("\r\n");
    serial_size = 0;
  }
}

/**
 * @brief Append one little-endian word to the hex stream for the current gcda file.
 * @param value Word to append.
 */
static void write_word(uint32_t value) {
  static const char hex[] = "0123456789abcdef";
  for (unsigned int byte = 0; byte < 4; ++byte) {
    serial_hex[serial_size++] = hex[(value >> 4) & 0xf];
    serial_hex[serial_size++] = hex[value & 0xf];
    value >>= 8;
  }
  if (serial_size == sizeof(serial_hex) - 1) {
    flush_serial();
  }
}

/**
 * @brief Register a write callback from Clang's gcov initializer.
 * @param writeout Coverage writer for one instrumented translation unit.
 * @param reset Counter reset callback, unused for a single test run.
 */
void llvm_gcov_init(GcovWriter writeout, GcovWriter reset) {
  (void) reset;
  if (writer_count + 1 == sizeof(writers) / sizeof(writers[0])) {
    sunshine_write_guest_text("SUNSHINE_GCOV_ERROR Too many instrumented translation units\r\n");
    return;
  }
  writers[writer_count++] = writeout;
}

void sunshine_write_guest_coverage(void) {
  for (size_t index = 0; writers[index] != NULL; ++index) {
    writers[index]();
  }
}

/**
 * @brief Begin a gcda stream using the matching compiler's version and checksum.
 * @param filename Path to the gcda as emitted by Clang.
 * @param version Encoded gcov format.
 * @param checksum Checksum shared with the matching gcno file.
 */
void llvm_gcda_start_file(const char *filename, uint32_t version, uint32_t checksum) {
  const char *basename = filename;
  for (const char *current = filename; *current != '\0'; ++current) {
    if (*current == '/' || *current == '\\') {
      basename = current + 1;
    }
  }
  sunshine_write_guest_text("SUNSHINE_GCOV_BEGIN ");
  sunshine_write_guest_text(basename);
  sunshine_write_guest_text("\r\n");
  write_word(0x67636461);  // GCDA magic.
  write_word(version);
  write_word(checksum);
}

/**
 * @brief Write a gcov function record with both source and CFG checksums.
 * @param ident Function ID in the notes file.
 * @param function_checksum Function checksum.
 * @param cfg_checksum Control-flow graph checksum.
 */
void llvm_gcda_emit_function(uint32_t ident, uint32_t function_checksum, uint32_t cfg_checksum) {
  write_word(0x01000000);  // Function tag.
  write_word(3);
  write_word(ident);
  write_word(function_checksum);
  write_word(cfg_checksum);
}

/**
 * @brief Write a gcov arc record containing the guest's execution counts.
 * @param count Number of counters for the current function.
 * @param counters Execution counters assigned to the function.
 */
void llvm_gcda_emit_arcs(uint32_t count, const uint64_t *counters) {
  write_word(0x01a10000);  // Arc counter tag.
  write_word(count * 2);
  for (uint32_t index = 0; index < count; ++index) {
    write_word((uint32_t) counters[index]);
    write_word((uint32_t) (counters[index] >> 32));
  }
}

/** @brief Record one completed run in Clang's gcov object summary. */
void llvm_gcda_summary_info(void) {
  write_word(0xa1000000);  // Object summary tag.
  write_word(2);
  write_word(1);  // Runs.
  write_word(0);  // Maximum sum is unused by llvm-cov.
}

/** @brief Terminate the current gcda and send its final encoded bytes. */
void llvm_gcda_end_file(void) {
  write_word(0);
  write_word(0);
  flush_serial();
  sunshine_write_guest_text("SUNSHINE_GCOV_END\r\n");
}
