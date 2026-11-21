#!/usr/bin/env python3
"""Offline regression for the pinned candidate's FP_INFO version handling.

Extract the driver's actual FP_INFO negotiation, request and decode helpers and
compile them against a fake transfer constructor and synthetic replies. This
needs only the reference checkout and GLib headers, never a reader.

usage: test_reference_fp_info.py <crfpmoc-driver-directory>
"""
import os
import pathlib
import re
import shlex
import subprocess
import sys
import tempfile

driver = pathlib.Path(sys.argv[1])
source = (driver / "crfpmoc.c").read_text()
header = (driver / "crfpmoc.h").read_text()

# Every FP_INFO request must go through the negotiated-version constructor:
# no call site may hard-code a command version.
fp_info_requests = re.findall(r"CRFPMOC_EC_CMD_FP_INFO\s*,", source)
if len(fp_info_requests) != 1:
    sys.exit(f"FAIL: expected exactly one FP_INFO transfer constructor, found {len(fp_info_requests)}")
call_sites = re.findall(r"=\s*crfpmoc_fp_info_transfer_new\s*\(", source)
if len(call_sites) != 4:
    sys.exit(f"FAIL: expected open, enroll capacity, enroll commit and verify to "
             f"use crfpmoc_fp_info_transfer_new, found {len(call_sites)}")

h_start = header.index("/* EC_CMD_GET_CMD_VERSIONS")
h_end = header.index("} CrfpMocFpInfo;", h_start) + len("} CrfpMocFpInfo;")
c_start = source.index("/* FP_INFO version negotiation and bounded decoding.")
c_end = source.index("\n/* Parse the device info response", c_start)

harness = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <gio/gio.h>
#define FP_DEVICE_ERROR G_IO_ERROR
#define FP_DEVICE_ERROR_PROTO G_IO_ERROR_INVALID_DATA
#define FP_DEVICE_ERROR_NOT_SUPPORTED G_IO_ERROR_NOT_SUPPORTED
#define CRFPMOC_EC_CMD_FP_INFO 0x0403
typedef struct { int unused; } FpDevice;
typedef struct { FpDevice parent; guint8 fp_info_version; } FpiDeviceCrfpMoc;
#define FPI_DEVICE_CRFPMOC(d) ((FpiDeviceCrfpMoc *) (d))
typedef struct {
  FpDevice *device;
  int command, version;
  void *outdata; int outsize;
  void *indata; int insize;
  gboolean variable_reply; int received;
} CrfpMocEcTransfer;
static CrfpMocEcTransfer *
crfpmoc_ec_transfer_new_cmd (FpDevice *device, int command, int version,
                             gconstpointer outdata, int outsize, int insize)
{
  CrfpMocEcTransfer *t = g_new0 (CrfpMocEcTransfer, 1);
  t->device = device; t->command = command; t->version = version;
  t->outsize = outsize; t->insize = insize;
  if (outsize > 0) t->outdata = g_memdup2 (outdata, outsize);
  if (insize > 0) t->indata = g_malloc0 (insize);
  return t;
}
static void
transfer_free (CrfpMocEcTransfer *t)
{
  g_free (t->outdata); g_free (t->indata); g_free (t);
}
'''
harness += header[h_start:h_end] + "\n"
harness += source[c_start:c_end]
harness += r'''
static void put16 (guint8 *p, guint16 v) { p[0] = v; p[1] = v >> 8; }
static void put32 (guint8 *p, guint32 v) { put16 (p, v); put16 (p + 2, v >> 16); }
static int failures;
#define CHECK(c) do { if (!(c)) { fprintf (stderr, "FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)

static int
select_mask (guint32 mask, guint8 *version, GError **error)
{
  guint8 bytes[4];
  put32 (bytes, mask);
  return crfpmoc_fp_info_parse_versions (bytes, sizeof (bytes), version, error);
}

/* Synthetic FP_INFO header: model 543, 160x160 8bpp formats, 5 slots of 5156
 * bytes, template format 4, errors 0x03ff (dead-pixel count unknown). */
static gsize
build_v2v3 (guint8 *buf, gsize cap, int version, guint16 count, guint16 filled)
{
  gsize stride = version == 3 ? 20 : 16;
  memset (buf, 0, cap);
  put32 (buf + 0, 0x20435046); /* "FPC " */
  put32 (buf + 4, 9);
  put32 (buf + 8, 543);
  put32 (buf + 12, 1);
  put16 (buf + 16, count);
  put16 (buf + 18, 0x03ff);
  put32 (buf + 20, 5156);
  put16 (buf + 24, 5);
  put16 (buf + 26, 2);
  put32 (buf + 28, 0x2);
  put32 (buf + 32, 4);
  for (guint16 i = 0; i < filled; i++)
    {
      guint8 *p = buf + 36 + i * stride;
      put32 (p, 160 * 160 + i);
      if (version == 3)
        {
          put32 (p + 4, 2 + i);
          p += 4;
        }
      put32 (p + 4, 0x59455247); /* GREY */
      put16 (p + 8, 160);
      put16 (p + 10, 160);
      put16 (p + 12, 8);
      p[14] = i;
    }
  return 36 + 9 * stride;
}

static void
expect_proto (int version, const guint8 *data, gsize size, int line)
{
  CrfpMocFpInfo info;
  GError *error = NULL;
  memset (&info, 0x5c, sizeof (info));
  if (crfpmoc_fp_info_decode (version, data, size, &info, &error) ||
      !g_error_matches (error, FP_DEVICE_ERROR, FP_DEVICE_ERROR_PROTO))
    {
      fprintf (stderr, "FAIL line %d: v%d reply of %zu bytes not rejected as PROTO\n",
               line, version, size);
      ++failures;
    }
  for (gsize i = 0; i < sizeof (info); i++)
    if (((guint8 *) &info)[i] != 0x5c)
      {
        fprintf (stderr, "FAIL line %d: output modified on failure\n", line);
        ++failures;
        break;
      }
  g_clear_error (&error);
}
#define EXPECT_PROTO(v, d, s) expect_proto (v, d, s, __LINE__)

static void
test_negotiation (void)
{
  static const struct { guint32 mask; int version; } cases[] = {
    { 1u << 3, 3 },                       /* this machine's firmware is 2|3 */
    { (1u << 2) | (1u << 3), 3 },
    { 1u << 2, 2 },
    { 1u << 1, 1 },                       /* legacy firmware */
    { (1u << 0) | (1u << 1), 1 },
    { (1u << 1) | (1u << 2), 2 },
    { 0xfffffff0u | (1u << 2), 2 },       /* unknown future versions ignored */
    { 0, 0 },
    { 1u << 0, 0 },                       /* v0 has no template info */
    { 0xfffffff0u, 0 },
    { 0xfffffff1u, 0 },
  };
  for (gsize i = 0; i < G_N_ELEMENTS (cases); i++)
    {
      GError *error = NULL;
      guint8 version = 0x77;
      gboolean ok = select_mask (cases[i].mask, &version, &error);
      if (cases[i].version)
        {
          CHECK (ok && error == NULL && version == cases[i].version);
          FpiDeviceCrfpMoc self = { .fp_info_version = version };
          CrfpMocEcTransfer *t = crfpmoc_fp_info_transfer_new ((FpDevice *) &self);
          CHECK (t->command == CRFPMOC_EC_CMD_FP_INFO);
          CHECK (t->version == cases[i].version);
          /* never fall back to v1 when the firmware does not advertise it */
          CHECK (t->version != 1 || (cases[i].mask & (1u << 1)));
          CHECK (t->outsize == 0 && t->outdata == NULL);
          CHECK (t->insize == (version == 1 ? 48 : 216));
          CHECK (t->variable_reply == (version != 1));
          transfer_free (t);
        }
      else
        {
          CHECK (!ok && version == 0x77);
          CHECK (g_error_matches (error, FP_DEVICE_ERROR, FP_DEVICE_ERROR_NOT_SUPPORTED));
        }
      g_clear_error (&error);
    }

  /* The capability query itself: GET_CMD_VERSIONS v1 for 16-bit 0x0403. */
  FpiDeviceCrfpMoc self = { .fp_info_version = 0 };
  CrfpMocEcTransfer *t = crfpmoc_fp_info_versions_transfer_new ((FpDevice *) &self);
  CHECK (t->command == 0x0008 && t->version == 1);
  CHECK (t->outsize == 2 && memcmp (t->outdata, "\x03\x04", 2) == 0);
  CHECK (t->insize == 4 && !t->variable_reply);
  transfer_free (t);

  /* Malformed capability replies. */
  guint8 bytes[8] = { 0x08, 0, 0, 0, 0, 0, 0, 0 };
  for (gsize size = 0; size <= sizeof (bytes); size++)
    {
      GError *error = NULL;
      guint8 version = 0x77;
      gboolean ok = crfpmoc_fp_info_parse_versions (size ? bytes : NULL, size, &version, &error);
      if (size == 4)
        CHECK (ok && version == 3);
      else
        CHECK (!ok && version == 0x77 &&
               g_error_matches (error, FP_DEVICE_ERROR, FP_DEVICE_ERROR_PROTO));
      g_clear_error (&error);
    }
}

static void
test_v3 (void)
{
  guint8 buf[300];
  CrfpMocFpInfo info;
  GError *error = NULL;
  gsize full = build_v2v3 (buf, sizeof (buf), 3, 6, 6);

  CHECK (full == 216);
  CHECK (crfpmoc_fp_info_decode (3, buf, full, &info, &error));
  CHECK (error == NULL);
  CHECK (info.vendor_id == 0x20435046 && info.product_id == 9);
  CHECK (info.model_id == 543 && info.version == 1);
  CHECK (info.num_capture_types == 6);
  CHECK (info.width == 160 && info.height == 160 && info.bpp == 8);
  CHECK (info.frame_size == 160 * 160 && info.pixel_format == 0x59455247);
  CHECK (info.errors == 0x03ff); /* dead-pixel count unknown is not a fault */
  CHECK (info.template_size == 5156 && info.template_max == 5);
  CHECK (info.template_valid == 2 && info.template_dirty == 0x2);
  CHECK (info.template_version == 4);
  g_clear_error (&error);

  /* A reply that stops right after the advertised records is complete. */
  CHECK (crfpmoc_fp_info_decode (3, buf, 36 + 6 * 20, &info, NULL));
  /* Truncations. */
  EXPECT_PROTO (3, NULL, 0);
  EXPECT_PROTO (3, buf, 0);
  EXPECT_PROTO (3, buf, 20);
  EXPECT_PROTO (3, buf, 35);
  EXPECT_PROTO (3, buf, 36);
  EXPECT_PROTO (3, buf, 36 + 6 * 20 - 1);
  /* Oversized. */
  EXPECT_PROTO (3, buf, 217);
  EXPECT_PROTO (3, buf, sizeof (buf));

  /* Advertised count larger than the reply (or the protocol) can hold. */
  build_v2v3 (buf, sizeof (buf), 3, 10, 9);
  EXPECT_PROTO (3, buf, 216);
  build_v2v3 (buf, sizeof (buf), 3, 0xffff, 9);
  EXPECT_PROTO (3, buf, 216);
  build_v2v3 (buf, sizeof (buf), 3, 9, 9);
  CHECK (crfpmoc_fp_info_decode (3, buf, 216, &info, NULL) && info.num_capture_types == 9);

  /* Zero-count reply: padding records are not data, even if non-zero. */
  build_v2v3 (buf, sizeof (buf), 3, 0, 9);
  CHECK (crfpmoc_fp_info_decode (3, buf, 216, &info, NULL));
  CHECK (info.num_capture_types == 0 && info.width == 0 && info.height == 0 &&
         info.bpp == 0 && info.frame_size == 0);
  CHECK (info.template_size == 5156 && info.template_max == 5);

  /* More valid templates than slots is malformed. */
  build_v2v3 (buf, sizeof (buf), 3, 6, 6);
  put16 (buf + 26, 6);
  EXPECT_PROTO (3, buf, 216);
}

static void
test_v2 (void)
{
  guint8 buf[300];
  CrfpMocFpInfo info;
  GError *error = NULL;
  gsize full = build_v2v3 (buf, sizeof (buf), 2, 6, 6);

  CHECK (full == 180);
  CHECK (crfpmoc_fp_info_decode (2, buf, full, &info, &error));
  CHECK (error == NULL);
  CHECK (info.model_id == 543 && info.num_capture_types == 6);
  CHECK (info.width == 160 && info.height == 160 && info.bpp == 8);
  CHECK (info.frame_size == 160 * 160 && info.pixel_format == 0x59455247);
  CHECK (info.template_size == 5156 && info.template_max == 5 &&
         info.template_valid == 2 && info.template_version == 4);
  g_clear_error (&error);

  CHECK (crfpmoc_fp_info_decode (2, buf, 36 + 6 * 16, &info, NULL));
  EXPECT_PROTO (2, buf, 35);
  EXPECT_PROTO (2, buf, 36 + 6 * 16 - 1);
  EXPECT_PROTO (2, buf, 181);
  EXPECT_PROTO (2, buf, 216);
  build_v2v3 (buf, sizeof (buf), 2, 10, 9);
  EXPECT_PROTO (2, buf, 180);
  build_v2v3 (buf, sizeof (buf), 2, 0xffff, 9);
  EXPECT_PROTO (2, buf, 180);
}

static void
test_v1 (void)
{
  guint8 buf[64] = { 0 };
  CrfpMocFpInfo info;
  GError *error = NULL;

  put32 (buf + 0, 0x20435046);
  put32 (buf + 4, 9);
  put32 (buf + 8, 0x211);
  put32 (buf + 12, 1);
  put32 (buf + 16, 160 * 160);
  put32 (buf + 20, 0x59455247);
  put16 (buf + 24, 160);
  put16 (buf + 26, 160);
  put16 (buf + 28, 8);
  put16 (buf + 30, 0x03ff);
  put32 (buf + 32, 5156);
  put16 (buf + 36, 5);
  put16 (buf + 38, 1);
  put32 (buf + 40, 0x1);
  put32 (buf + 44, 4);

  CHECK (crfpmoc_fp_info_reply_capacity (1) == 48);
  CHECK (crfpmoc_fp_info_decode (1, buf, 48, &info, &error));
  CHECK (error == NULL);
  CHECK (info.vendor_id == 0x20435046 && info.model_id == 0x211);
  CHECK (info.num_capture_types == 1 && info.width == 160 && info.height == 160 &&
         info.bpp == 8 && info.frame_size == 160 * 160);
  CHECK (info.errors == 0x03ff);
  CHECK (info.template_size == 5156 && info.template_max == 5 &&
         info.template_valid == 1 && info.template_dirty == 1 &&
         info.template_version == 4);
  g_clear_error (&error);

  /* v1 is fixed size. */
  EXPECT_PROTO (1, NULL, 0);
  EXPECT_PROTO (1, buf, 36);
  EXPECT_PROTO (1, buf, 47);
  EXPECT_PROTO (1, buf, 49);
  put16 (buf + 38, 6);
  EXPECT_PROTO (1, buf, 48);

  /* Versions this driver cannot decode. */
  EXPECT_PROTO (0, buf, 48);
  EXPECT_PROTO (4, buf, 216);
}

static void
test_from_transfer (void)
{
  /* Callbacks decode only the bytes the controller actually returned, not
   * the zero-filled tail of the reply allocation. */
  guint8 buf[300];
  CrfpMocFpInfo info;
  GError *error = NULL;
  FpiDeviceCrfpMoc self = { .fp_info_version = 3 };
  CrfpMocEcTransfer *t = crfpmoc_fp_info_transfer_new ((FpDevice *) &self);

  build_v2v3 (buf, sizeof (buf), 3, 6, 6);
  memcpy (t->indata, buf, 216);
  t->received = 36 + 6 * 20;
  CHECK (crfpmoc_fp_info_from_transfer (t, 3, &info, &error));
  CHECK (info.template_max == 5 && info.num_capture_types == 6);
  g_clear_error (&error);

  memset (t->indata, 0, t->insize);
  t->received = 20;
  CHECK (!crfpmoc_fp_info_from_transfer (t, 3, &info, &error));
  CHECK (g_error_matches (error, FP_DEVICE_ERROR, FP_DEVICE_ERROR_PROTO));
  g_clear_error (&error);

  t->received = -1;
  CHECK (!crfpmoc_fp_info_from_transfer (t, 3, &info, &error));
  CHECK (g_error_matches (error, FP_DEVICE_ERROR, FP_DEVICE_ERROR_PROTO));
  g_clear_error (&error);

  t->received = t->insize + 1;
  CHECK (!crfpmoc_fp_info_from_transfer (t, 3, &info, &error));
  CHECK (g_error_matches (error, FP_DEVICE_ERROR, FP_DEVICE_ERROR_PROTO));
  g_clear_error (&error);
  transfer_free (t);
}

int
main (void)
{
  test_negotiation ();
  test_v3 ();
  test_v2 ();
  test_v1 ();
  test_from_transfer ();
  CHECK (crfpmoc_fp_info_reply_capacity (0) == 0);
  CHECK (crfpmoc_fp_info_reply_capacity (4) == 0);
  return failures ? 1 : 0;
}
'''

with tempfile.TemporaryDirectory(prefix="cfp-reference-fp-info-") as directory:
    test_c = pathlib.Path(directory) / "fp_info.c"
    binary = pathlib.Path(directory) / "fp_info"
    test_c.write_text(harness)
    flags = shlex.split(subprocess.check_output(
        ["pkg-config", "--cflags", "--libs", "gio-2.0"], text=True))
    subprocess.run(["cc", "-std=gnu11", "-Wall", "-Wextra", "-Werror",
                    *shlex.split(os.environ.get("CFLAGS", "")),
                    str(test_c), "-o", str(binary), *flags], check=True)
    subprocess.run([str(binary)], check=True)
print("reference FP_INFO versions: PASS")
