#!/usr/bin/env python3
"""Offline regressions for crfpmoc fixes needed before a first live run.

1. Enroll result codes from MKBP events are stored regardless of log level.
2. FP_SEED is sent exactly when FP_ENC_STATUS reports the seed bit clear.
3. Enroll idles the sensor (FP_MODE 0) before FP_CONTEXT resets it.

Compiles the driver's actual helpers against fakes with synthetic bytes. Needs
only the reference checkout and GLib headers, never a reader.

usage: test_reference_live_fixes.py <crfpmoc-driver-directory>
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


def function(text, signature):
    """Return a top-level C function starting at signature, up to its '\n}\n'."""
    start = text.index(signature)
    end = text.index("\n}\n", start) + 3
    return text[start:end]


# --- Source-order checks -------------------------------------------------
read_bytes = function(source, "static gboolean\ncrfpmoc_read_bytes (")
store_at = read_bytes.find("crfpmoc_mkbp_store_enroll_result (")
drop_at = read_bytes.find("g_log_writer_default_would_drop")
if store_at < 0 or (drop_at >= 0 and store_at > drop_at):
    sys.exit("FAIL: crfpmoc_read_bytes must store the enroll result before "
             "any debug-level early return")
if len(re.findall(r"enroll_error_code\s*=", source)) != 1:
    sys.exit("FAIL: enroll_error_code must be assigned only by the store helper")

enc_cb = function(source, "static void\ncrfpmoc_keys_enc_status_cb (")
if "crfpmoc_seed_needed (" not in enc_cb or re.search(r"->status\s*==\s*0", enc_cb):
    sys.exit("FAIL: FP_ENC_STATUS callback must decide via the seed bit helper")

enroll_enum = header[header.index("/* Enroll state machine states"):]
enroll_enum = enroll_enum[:enroll_enum.index("ENROLL_STATES")]
states = re.findall(r"^\s*(ENROLL_[A-Z_]+)\s*,", enroll_enum, re.M)
if not states or states[0] != "ENROLL_SENSOR_IDLE" or \
        states.index("ENROLL_SENSOR_IDLE") > states.index("ENROLL_ENSURE_KEYS"):
    sys.exit(f"FAIL: enroll must start by idling the sensor, states are {states}")
run_state = function(source, "static void\ncrfpmoc_enroll_run_state (")
if not re.search(r"case ENROLL_SENSOR_IDLE:\s*handle_enroll_reset \(ssm\);", run_state):
    sys.exit("FAIL: ENROLL_SENSOR_IDLE must send FP_MODE 0 via handle_enroll_reset")

# --- Behaviour ------------------------------------------------------------
h_start = header.index("/* Note: used in crfpmoc_ec_response_get_next_data_v1 */")
h_end = header.index("struct crfpmoc_ec_response_get_next_event_v1", h_start)
h_end = header.index("} __attribute__((packed));", h_end) + len("} __attribute__((packed));")
types_start = source.index("/* State machine private data.")
types_end = source.index("#define CRFPMOC_ENROLL_DATA_MAGIC 0x4352454e")
types_end = types_end + len("#define CRFPMOC_ENROLL_DATA_MAGIC 0x4352454e")

harness = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <gio/gio.h>
#define BIT(nr) (1UL << (nr))
#define FP_DEVICE_ERROR G_IO_ERROR
#define FP_DEVICE_ERROR_PROTO G_IO_ERROR_INVALID_DATA
#define CRFPMOC_FP_ENC_STATUS_SEED_SET BIT (0)
#define CRFPMOC_EC_CMD_FP_MODE 0x0402
typedef struct _FpiSsm FpiSsm;
typedef struct { int unused; } FpDevice;
struct crfpmoc_ec_params_fp_mode { guint32 mode; } __attribute__((packed));
struct crfpmoc_ec_response_fp_mode { guint32 mode; } __attribute__((packed));
static FpDevice fake_device;
static FpDevice *fpi_ssm_get_device (FpiSsm *ssm) { (void) ssm; return &fake_device; }
typedef struct { int command, version, outsize, insize; guint8 out[16]; } CrfpMocEcTransfer;
static CrfpMocEcTransfer last;
static int submitted;
static CrfpMocEcTransfer *
crfpmoc_ec_transfer_new_cmd (FpDevice *device, int command, int version,
                             gconstpointer outdata, int outsize, int insize)
{
  assert (device == &fake_device && outsize <= 16);
  memset (&last, 0, sizeof (last));
  last.command = command; last.version = version;
  last.outsize = outsize; last.insize = insize;
  memcpy (last.out, outdata, outsize);
  return &last;
}
static void crfpmoc_ssm_ec_transfer_cb (void) {}
static void
fake_submit (CrfpMocEcTransfer *t, GCancellable *c, void (*cb) (void), FpiSsm *ssm)
{
  (void) c; (void) ssm;
  assert (t == &last && cb == crfpmoc_ssm_ec_transfer_cb);
  ++submitted;
}
#define crfpmoc_ec_transfer_submit_cmd fake_submit
'''
harness += header[h_start:h_end] + "\n"
harness += source[types_start:types_end] + "\n"
harness += function(source, "static void\ncrfpmoc_mkbp_store_enroll_result (")
harness += function(source, "static gboolean\ncrfpmoc_seed_needed (")
harness += function(source, "static void\nhandle_enroll_reset (")
harness += r'''
static int failures;
#define CHECK(c) do { if (!(c)) { fprintf (stderr, "FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)

static struct crfpmoc_ec_response_get_next_event_v1
event (guint8 type, guint32 fp_events)
{
  struct crfpmoc_ec_response_get_next_event_v1 e;
  memset (&e, 0, sizeof (e));
  e.event_type = type;
  e.data.fp_events = fp_events;
  return e;
}

static void
test_enroll_result (void)
{
  EnrollData enroll = { .magic = CRFPMOC_ENROLL_DATA_MAGIC };
  SubSsmData data = { .sub_data = &enroll };

  for (guint code = 0; code < 16; code++)
    {
      struct crfpmoc_ec_response_get_next_event_v1 e =
        event (CRFPMOC_EC_MKBP_EVENT_FINGERPRINT, CRFPMOC_EC_MKBP_FP_ENROLL | (3 << 4) | code);
      enroll.enroll_error_code = -1;
      crfpmoc_mkbp_store_enroll_result (&data, &e);
      CHECK (enroll.enroll_error_code == (int) code);

      e.event_type |= CRFPMOC_EC_MKBP_HAS_MORE_EVENTS;
      enroll.enroll_error_code = -1;
      crfpmoc_mkbp_store_enroll_result (&data, &e);
      CHECK (enroll.enroll_error_code == (int) code);
    }

  /* Events that are not enroll results leave the stored code alone. */
  struct crfpmoc_ec_response_get_next_event_v1 others[] = {
    event (CRFPMOC_EC_MKBP_EVENT_FINGERPRINT, CRFPMOC_EC_MKBP_FP_MATCH | 1),
    event (CRFPMOC_EC_MKBP_EVENT_FINGERPRINT, CRFPMOC_EC_MKBP_FP_FINGER_UP),
    event (CRFPMOC_EC_MKBP_EVENT_HOST_EVENT, CRFPMOC_EC_MKBP_FP_ENROLL | 1),
  };
  for (gsize i = 0; i < G_N_ELEMENTS (others); i++)
    {
      enroll.enroll_error_code = 42;
      crfpmoc_mkbp_store_enroll_result (&data, &others[i]);
      CHECK (enroll.enroll_error_code == 42);
    }

  /* Non-enroll state machines carry no EnrollData. */
  struct crfpmoc_ec_response_get_next_event_v1 e =
    event (CRFPMOC_EC_MKBP_EVENT_FINGERPRINT, CRFPMOC_EC_MKBP_FP_ENROLL | 1);
  SubSsmData empty = { 0 };
  crfpmoc_mkbp_store_enroll_result (&empty, &e);
  guint32 other_payload[4] = { 0x12345678, 0, 0, 0 };
  SubSsmData foreign = { .sub_data = other_payload };
  crfpmoc_mkbp_store_enroll_result (&foreign, &e);
  CHECK (other_payload[0] == 0x12345678 && other_payload[1] == 0 &&
         other_payload[2] == 0 && other_payload[3] == 0);
}

static void
test_seed_needed (void)
{
  static const struct { guint32 valid, status; int needed; } cases[] = {
    { 1, 0, 1 },
    { 1, 1, 0 },
    { 1, 1u << 3, 1 },              /* user id set, seed not set */
    { 1, (1u << 1) | (1u << 2), 1 },/* session bits, seed not set */
    { 1, 0xfffffffeu, 1 },
    { 1, (1u << 3) | 1, 0 },
    { 0xffffffffu, 1, 0 },
    { 0xffffffffu, 8, 1 },
  };
  for (gsize i = 0; i < G_N_ELEMENTS (cases); i++)
    {
      GError *error = NULL;
      gboolean needed = 77;
      CHECK (crfpmoc_seed_needed (cases[i].valid, cases[i].status, &needed, &error));
      CHECK (error == NULL && needed == cases[i].needed);
      g_clear_error (&error);
    }
  /* Seed status not reported: do not guess (FP_SEED cannot be undone). */
  static const guint32 invalid[] = { 0, 0xfffffffeu, 1u << 3 };
  for (gsize i = 0; i < G_N_ELEMENTS (invalid); i++)
    {
      GError *error = NULL;
      gboolean needed = 77;
      CHECK (!crfpmoc_seed_needed (invalid[i], 0, &needed, &error));
      CHECK (needed == 77);
      CHECK (g_error_matches (error, FP_DEVICE_ERROR, FP_DEVICE_ERROR_PROTO));
      g_clear_error (&error);
    }
}

static void
test_idle (void)
{
  submitted = 0;
  handle_enroll_reset ((FpiSsm *) &fake_device);
  CHECK (submitted == 1);
  CHECK (last.command == CRFPMOC_EC_CMD_FP_MODE && last.version == 0);
  CHECK (last.outsize == 4 && memcmp (last.out, "\0\0\0\0", 4) == 0);
  CHECK (last.insize == 4);
}

int
main (void)
{
  test_enroll_result ();
  test_seed_needed ();
  test_idle ();
  return failures ? 1 : 0;
}
'''
with tempfile.TemporaryDirectory(prefix="cfp-reference-live-") as directory:
    test_c = pathlib.Path(directory) / "live.c"
    binary = pathlib.Path(directory) / "live"
    test_c.write_text(harness)
    flags = shlex.split(subprocess.check_output(
        ["pkg-config", "--cflags", "--libs", "gio-2.0"], text=True))
    subprocess.run(["cc", "-std=gnu11", "-Wall", "-Wextra", "-Werror",
                    *shlex.split(os.environ.get("CFLAGS", "")),
                    str(test_c), "-o", str(binary), *flags], check=True)
    subprocess.run([str(binary)], check=True)
print("reference live fixes: PASS")
