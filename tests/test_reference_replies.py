#!/usr/bin/env python3
"""Exercise the pinned candidate's actual ioctl helper with synthetic replies."""
import os
import pathlib
import shlex
import subprocess
import sys
import tempfile

source = pathlib.Path(sys.argv[1]).read_text()
start = source.index("static int\ndo_ioctl_xcmd (")
end = source.index("\nstatic void\ncommand_thread_func (", start)
harness = r'''
#include <gio/gio.h>
#include <errno.h>
#include <string.h>
#include <assert.h>
typedef struct { void *device; int insize; void *indata; int variable_reply; int received; } CrfpMocEcTransfer;
struct crfpmoc_cros_ec_command_v2 { unsigned result; unsigned char data[64]; };
#define FPI_DEVICE_UDEV_SUBTYPE_MISC 0
#define CRFPMOC_CROS_EC_DEV_IOCXCMD_V2 0
#define EC_RES_SUCCESS 0
#define FP_DEVICE_ERROR G_IO_ERROR
#define FP_DEVICE_ERROR_PROTO G_IO_ERROR_INVALID_DATA
static int reply_size;
static int fpi_device_get_udev_fd(void *device, int subtype) {
  (void)device; (void)subtype; return 42;
}
static int fake_ioctl(int fd, int request, void *packet) {
  (void)request; (void)packet; assert(fd == 42); errno = EIO;
  return reply_size;
}
#define ioctl fake_ioctl
static int ec_result_to_fp_error(int result) { (void)result; return FP_DEVICE_ERROR_PROTO; }
static const char *crfpmoc_strresult(int result) { (void)result; return "synthetic failure"; }
'''
harness += source[start:end]
harness += r'''
int main(void) {
  unsigned char output[64];
  struct crfpmoc_cros_ec_command_v2 packet = {0};
  CrfpMocEcTransfer transfer = {NULL, 0, output, 0, 0};
  memset(packet.data, 0x5a, sizeof(packet.data));
  for (int expected = 0; expected <= 32; ++expected) {
    transfer.insize = expected;
    transfer.indata = expected ? output : NULL;
    for (reply_size = -1; reply_size <= 64; ++reply_size) {
      for (packet.result = 0; packet.result <= 1; ++packet.result) {
        GError *error = NULL;
        memset(output, 0xa5, sizeof(output));
        int result = do_ioctl_xcmd(&transfer, &packet, &error);
        int valid = reply_size == expected && packet.result == EC_RES_SUCCESS;
        if ((result >= 0) != valid) {
          fprintf(stderr, "FAIL: expected %d bytes, received %d, EC status %u: accepted=%d\n",
                  expected, reply_size, packet.result, result >= 0);
          return 1;
        }
        assert((error == NULL) == valid);
        if (valid) assert(result == expected);
        if (!valid && reply_size >= 0 && packet.result == EC_RES_SUCCESS)
          assert(g_error_matches(error, FP_DEVICE_ERROR, FP_DEVICE_ERROR_PROTO));
        for (int i = 0; i < (int)sizeof(output); ++i)
          assert(output[i] == (valid && i < expected ? 0x5a : 0xa5));
        g_clear_error(&error);
      }
    }
  }
  /* Opt-in variable-length replies (FP_INFO v2/v3): accept 0..insize bytes,
   * report the actual length, copy only what was received, never accept an
   * oversized reply. Fixed-size callers above are unaffected. */
  transfer.variable_reply = 1;
  for (int capacity = 0; capacity <= 32; ++capacity) {
    transfer.insize = capacity;
    transfer.indata = capacity ? output : NULL;
    for (reply_size = -1; reply_size <= 64; ++reply_size) {
      for (packet.result = 0; packet.result <= 1; ++packet.result) {
        GError *error = NULL;
        memset(output, 0xa5, sizeof(output));
        transfer.received = -7;
        int result = do_ioctl_xcmd(&transfer, &packet, &error);
        int valid = reply_size >= 0 && reply_size <= capacity &&
                    packet.result == EC_RES_SUCCESS;
        if ((result >= 0) != valid) {
          fprintf(stderr, "FAIL: variable capacity %d, received %d, EC status %u: accepted=%d\n",
                  capacity, reply_size, packet.result, result >= 0);
          return 1;
        }
        assert((error == NULL) == valid);
        if (valid) {
          assert(result == reply_size);
          assert(transfer.received == reply_size);
        }
        if (!valid && reply_size >= 0 && packet.result == EC_RES_SUCCESS)
          assert(g_error_matches(error, FP_DEVICE_ERROR, FP_DEVICE_ERROR_PROTO));
        for (int i = 0; i < (int)sizeof(output); ++i)
          assert(output[i] == (valid && i < reply_size ? 0x5a : 0xa5));
        g_clear_error(&error);
      }
    }
  }
  return 0;
}
'''
harness = "#include <stdio.h>\n" + harness
with tempfile.TemporaryDirectory(prefix="cfp-reference-replies-") as directory:
    test_c = pathlib.Path(directory) / "replies.c"
    binary = pathlib.Path(directory) / "replies"
    test_c.write_text(harness)
    flags = shlex.split(subprocess.check_output(
        ["pkg-config", "--cflags", "--libs", "gio-2.0"], text=True))
    subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                    *shlex.split(os.environ.get("CFLAGS", "")),
                    str(test_c), "-o", str(binary), *flags], check=True)
    subprocess.run([str(binary)], check=True)
print("reference fixed-size and variable-length replies: PASS")
