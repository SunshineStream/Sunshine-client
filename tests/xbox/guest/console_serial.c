/**
 * @file tests/xbox/guest/console_serial.c
 * @brief Connect the Xbox test XBE's standard streams to xemu's debug serial console.
 */
// standard includes
#include <stdio.h>

// lib includes
#include <pdclib/_PDCLIB_glue.h>
#include <xboxkrnl/xboxkrnl.h>

/**
 * @brief Flush a regular file stream through the original nxdk implementation.
 * @param stream PDCLib stream to flush.
 * @return Zero on success or EOF on failure.
 */
int sunshine_pdclib_flush_file(FILE *stream);

/**
 * @brief Flush stdout and stderr to the Xbox debug port and preserve normal file I/O.
 * @param stream PDCLib stream to flush.
 * @return Zero on success or EOF on failure.
 */
int _PDCLIB_flushbuffer(FILE *stream) {
  // nxdk starts the standard output streams without backing file handles.
  if (stream != stdout && stream != stderr) {
    return sunshine_pdclib_flush_file(stream);
  }

  size_t offset = 0;
  while (offset < stream->bufidx) {
    const size_t remaining = stream->bufidx - offset;
    const int length = remaining > 200 ? 200 : (int) remaining;
    DbgPrint("%.*s", length, stream->buffer + offset);
    offset += length;
  }
  stream->pos.offset += stream->bufidx;
  stream->bufidx = 0;
  return 0;
}
