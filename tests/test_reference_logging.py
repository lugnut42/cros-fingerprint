#!/usr/bin/env python3
"""Offline regression for the pinned candidate's command logger only.

Compile the actual static logger with instrumented logging sinks. This requires
an explicit reference checkout, never a reader or an installed libfprint.
"""
import pathlib
import subprocess
import sys
import tempfile

source = pathlib.Path(sys.argv[1]).read_text()
start = source.index("static void\ncrfpmoc_ec_transfer_log_cmd (")
end = source.index("\nstatic void\ncrfpmoc_ec_transfer_log_eventmask (", start)
logger = source[start:end]
harness = r'''
#include <assert.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
typedef struct {
  unsigned command;
  int outsize, insize;
  unsigned char *outdata;
} CrfpMocEcTransfer;
static int enabled, metadata_calls, payload_calls;
#define G_LIKELY(x) (x)
static int fpi_log_is_debug_transfer_enabled(void) { return enabled; }
static void g_debug(const char *format, ...) {
  char message[256];
  va_list args;
  va_start(args, format);
  vsnprintf(message, sizeof(message), format, args);
  va_end(args);
  ++metadata_calls;
}
static void payload_sink(const void *data, size_t size) {
  (void)data; (void)size; ++payload_calls;
}
#define fp_dbg_hex_dump_data payload_sink
'''
harness += logger
harness += r'''
int main(void) {
  unsigned char synthetic_payload[64] = {0x53, 0x59, 0x4e};
  CrfpMocEcTransfer transfer = {0, sizeof(synthetic_payload), 64, synthetic_payload};
  (void)payload_sink;
  /* No command is permitted to opt back into raw request logging. */
  for (unsigned command = 0; command <= 0xffff; ++command) {
    transfer.command = command;
    for (enabled = 0; enabled <= 1; ++enabled) {
      metadata_calls = payload_calls = 0;
      crfpmoc_ec_transfer_log_cmd(&transfer);
      if (payload_calls) {
        fprintf(stderr, "FAIL: command 0x%04x logs request payload\n", command);
        return 1;
      }
      assert(metadata_calls == enabled);
    }
  }
  transfer.outdata = NULL;
  transfer.outsize = 0;
  enabled = 1;
  crfpmoc_ec_transfer_log_cmd(&transfer);
  assert(payload_calls == 0);
  return 0;
}
'''
with tempfile.TemporaryDirectory(prefix="cfp-reference-log-") as directory:
    test_c = pathlib.Path(directory) / "logging.c"
    binary = pathlib.Path(directory) / "logging"
    test_c.write_text(harness)
    subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                    str(test_c), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
print("reference command logging: PASS")
