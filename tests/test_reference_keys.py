#!/usr/bin/env python3
"""Offline checks for the crfpmoc machine key (seed + user context).

1. crfpmoc-keys.c: the key file is created once (0600, versioned, exact size,
   non-zero random seed and context) and reloaded unchanged; an invalid file
   (size, magic, version, zero seed/context, permissions, symlink, directory)
   is an error and is never replaced; an unwritable parent is an error and
   leaves nothing behind; a leftover temp file does not break creation.
2. The stale-seed decision: a key that is new to the reader must not be used
   while the reader already holds a seed (FP_SEED cannot replace it).
3. FP_TEMPLATE commit failing with EC_RES_UNAVAILABLE (key derivation or
   decryption failed) is reported as DATA_INVALID with a re-enroll hint.
4. Opening the device never touches the key file (keys load lazily).

Compiles the driver's real helpers against small fakes, works in a temporary
directory with synthetic data only, never touches a reader or /var/lib.

usage: test_reference_keys.py <crfpmoc-driver-directory>
"""
import os
import pathlib
import re
import shlex
import subprocess
import sys
import tempfile

driver = pathlib.Path(sys.argv[1])
keys_c = driver / "crfpmoc-keys.c"
keys_h = driver / "crfpmoc-keys.h"
if not keys_c.exists() or not keys_h.exists():
    sys.exit(f"FAIL: {driver} has no crfpmoc-keys.c/.h key helper")
source = (driver / "crfpmoc.c").read_text()


def function(text, signature):
    """Return a top-level C function starting at signature, up to its '\n}\n'."""
    start = text.index(signature)
    end = text.index("\n}\n", start) + 3
    return text[start:end]


# --- Lazy loading ------------------------------------------------------------
# Listing or opening the device must not create the key file.
for name in ("static void\ncrfpmoc_open (", "static void\ncrfpmoc_init_keys ("):
    if "crfpmoc_keys_load_or_create" in function(source, name):
        sys.exit(f"FAIL: {name.split(chr(10))[1]} must not load or create the key file")
if "crfpmoc_keys_load_or_create (" not in function(
        source, "static void\ncrfpmoc_keys_enc_status_cb ("):
    sys.exit("FAIL: the key file must be loaded in the ensure-keys path")

# --- Behaviour ---------------------------------------------------------------
harness = r'''
#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <gio/gio.h>
#include "crfpmoc-keys.h"

#define FP_DEVICE_ERROR (g_quark_from_static_string ("fake-fp-device-error"))
enum { FP_DEVICE_ERROR_GENERAL, FP_DEVICE_ERROR_PROTO = 5, FP_DEVICE_ERROR_DATA_INVALID = 6 };
#define BIT(nr) (1UL << (nr))
#define CRFPMOC_FP_ENC_STATUS_SEED_SET BIT (0)
enum { EC_RES_SUCCESS = 0, EC_RES_INVALID_PARAM = 3, EC_RES_UNAVAILABLE = 9 };
#define fpi_device_error_new_msg(code, ...) g_error_new (FP_DEVICE_ERROR, code, __VA_ARGS__)
'''
harness += function(source, "static gboolean\ncrfpmoc_seed_needed (")
harness += function(source, "static GError *\ncrfpmoc_template_upload_error (")
harness += r'''
static int failures;
#define CHECK(c) do { if (!(c)) { fprintf (stderr, "FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)

#define SEED CRFPMOC_KEYS_SEED_BYTES
#define CTX CRFPMOC_KEYS_CONTEXT_BYTES
#define HDR (sizeof (CRFPMOC_KEYS_MAGIC) - 1 + 4)
#define SIZE (HDR + SEED + CTX)

static const char *base;

static char *
dir_new (const char *name, int mode)
{
  char *d = g_build_filename (base, name, NULL);
  assert (mkdir (d, mode) == 0);
  return d;
}

static gboolean
all_zero (const guint8 *p, gsize n)
{
  for (gsize i = 0; i < n; i++)
    if (p[i])
      return FALSE;
  return TRUE;
}

static int
entries (const char *dir)
{
  DIR *d = opendir (dir);
  struct dirent *e;
  int n = 0;

  if (!d)
    return -1;
  while ((e = readdir (d)))
    if (strcmp (e->d_name, ".") && strcmp (e->d_name, ".."))
      n++;
  closedir (d);
  return n;
}

static void
write_file (const char *path, const guint8 *data, gsize len, int mode)
{
  int fd = open (path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
  assert (fd >= 0);
  assert (write (fd, data, len) == (gssize) len);
  assert (fchmod (fd, mode) == 0);
  close (fd);
}

/* A well-formed key file with synthetic seed/context bytes. */
static void
valid_image (guint8 img[SIZE])
{
  memcpy (img, CRFPMOC_KEYS_MAGIC, HDR - 4);
  img[HDR - 4] = CRFPMOC_KEYS_VERSION; img[HDR - 3] = 0; img[HDR - 2] = 0; img[HDR - 1] = 0;
  for (gsize i = 0; i < SEED + CTX; i++)
    img[HDR + i] = (guint8) (0x11 + i);
}

static void
test_create_and_reload (void)
{
  g_autofree char *parent = dir_new ("create", 0700);
  g_autofree char *dir = g_build_filename (parent, "keydir", NULL);
  g_autofree char *path = g_build_filename (dir, "key", NULL);
  guint8 seed[SEED], ctx[CTX], seed2[SEED], ctx2[CTX];
  gboolean created = 77;
  GError *error = NULL;
  struct stat st;

  /* Missing and creation not allowed: NOT_FOUND, nothing created. */
  CHECK (!crfpmoc_keys_load_or_create (path, FALSE, seed, ctx, &created, &error));
  CHECK (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND));
  g_clear_error (&error);
  CHECK (entries (parent) == 0);

  /* Missing: create (including a 0700 parent directory). */
  CHECK (crfpmoc_keys_load_or_create (path, TRUE, seed, ctx, &created, &error));
  CHECK (error == NULL && created == TRUE);
  CHECK (!all_zero (seed, SEED) && !all_zero (ctx, CTX));
  CHECK (memcmp (seed, ctx, SEED) != 0);
  CHECK (lstat (dir, &st) == 0 && S_ISDIR (st.st_mode) && (st.st_mode & 07777) == 0700);
  CHECK (lstat (path, &st) == 0 && S_ISREG (st.st_mode));
  CHECK ((st.st_mode & 07777) == 0600 && st.st_size == (off_t) SIZE);
  CHECK (st.st_uid == geteuid ());
  CHECK (entries (dir) == 1); /* no temp file left behind */

  g_autofree char *contents = NULL;
  gsize len = 0;
  CHECK (g_file_get_contents (path, &contents, &len, NULL) && len == SIZE);
  CHECK (memcmp (contents, CRFPMOC_KEYS_MAGIC, HDR - 4) == 0);
  CHECK ((guint8) contents[HDR - 4] == CRFPMOC_KEYS_VERSION &&
         contents[HDR - 3] == 0 && contents[HDR - 2] == 0 && contents[HDR - 1] == 0);
  CHECK (memcmp (contents + HDR, seed, SEED) == 0);
  CHECK (memcmp (contents + HDR + SEED, ctx, CTX) == 0);

  /* Reload: identical bytes, not created, regardless of may_create. */
  for (int may_create = 0; may_create <= 1; may_create++)
    {
      created = 77;
      memset (seed2, 0, SEED);
      memset (ctx2, 0, CTX);
      CHECK (crfpmoc_keys_load_or_create (path, may_create, seed2, ctx2, &created, &error));
      CHECK (error == NULL && created == FALSE);
      CHECK (memcmp (seed, seed2, SEED) == 0 && memcmp (ctx, ctx2, CTX) == 0);
      g_clear_error (&error);
    }

  /* A second machine gets different keys. */
  g_autofree char *other = g_build_filename (parent, "other", NULL);
  CHECK (crfpmoc_keys_load_or_create (other, TRUE, seed2, ctx2, &created, &error));
  CHECK (error == NULL && created == TRUE);
  CHECK (memcmp (seed, seed2, SEED) != 0 && memcmp (ctx, ctx2, CTX) != 0);
}

/* Each invalid file is an error naming the file, and stays byte-for-byte. */
static void
check_rejected (const char *what, const char *path, gboolean is_link)
{
  guint8 seed[SEED], ctx[CTX];
  gboolean created = 77;
  GError *error = NULL;
  struct stat before, after;
  g_autofree char *dir = g_path_get_dirname (path);
  int n = entries (dir);
  g_autofree char *orig = NULL, *now = NULL;
  gsize olen = 0, nlen = 0;

  assert (lstat (path, &before) == 0);
  if (!S_ISDIR (before.st_mode) && !is_link)
    assert (g_file_get_contents (path, &orig, &olen, NULL));

  memset (seed, 0xa5, SEED);
  for (int may_create = 1; may_create >= 0; may_create--)
    {
      if (crfpmoc_keys_load_or_create (path, may_create, seed, ctx, &created, &error))
        {
          fprintf (stderr, "FAIL: %s accepted\n", what);
          ++failures;
        }
      else
        {
          CHECK (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND));
          CHECK (error && strstr (error->message, path) != NULL);
          CHECK (error && strstr (error->message, "enrolled") != NULL);
        }
      g_clear_error (&error);
    }
  CHECK (lstat (path, &after) == 0);
  CHECK (after.st_mode == before.st_mode && after.st_ino == before.st_ino &&
         after.st_size == before.st_size &&
         after.st_mtim.tv_sec == before.st_mtim.tv_sec &&
         after.st_mtim.tv_nsec == before.st_mtim.tv_nsec);
  if (orig)
    {
      CHECK (g_file_get_contents (path, &now, &nlen, NULL));
      CHECK (nlen == olen && memcmp (now, orig, olen) == 0);
    }
  CHECK (entries (dir) == n);
  if (failures)
    fprintf (stderr, "  (after case: %s)\n", what);
}

static void
test_invalid_files (void)
{
  g_autofree char *dir = dir_new ("invalid", 0700);
  guint8 img[SIZE + 1];
  int k = 0;

#define CASE(desc, mutate, len, mode) do {                                   \
    g_autofree char *p = g_strdup_printf ("%s/key%d", dir, k++);             \
    valid_image (img);                                                      \
    mutate;                                                                 \
    write_file (p, img, (len), (mode));                                     \
    check_rejected (desc, p, FALSE);                                        \
  } while (0)

  CASE ("short file", (void) 0, SIZE - 1, 0600);
  CASE ("long file", img[SIZE] = 0, SIZE + 1, 0600);
  CASE ("empty file", (void) 0, 0, 0600);
  CASE ("bad magic", img[0] ^= 0x20, SIZE, 0600);
  CASE ("wrong version", img[HDR - 4] = CRFPMOC_KEYS_VERSION + 1, SIZE, 0600);
  CASE ("version high bytes", img[HDR - 1] = 1, SIZE, 0600);
  CASE ("all-zero seed", memset (img + HDR, 0, SEED), SIZE, 0600);
  CASE ("all-zero context", memset (img + HDR + SEED, 0, CTX), SIZE, 0600);
  CASE ("group readable", (void) 0, SIZE, 0640);
  CASE ("other readable", (void) 0, SIZE, 0604);
  CASE ("group writable", (void) 0, SIZE, 0620);
  CASE ("executable", (void) 0, SIZE, 0700);

  /* A symlink to an otherwise valid key file is still rejected. */
  g_autofree char *target = g_strdup_printf ("%s/target", dir);
  g_autofree char *link = g_strdup_printf ("%s/link", dir);
  valid_image (img);
  write_file (target, img, SIZE, 0600);
  assert (symlink (target, link) == 0);
  check_rejected ("symlink", link, TRUE);

  /* A dangling symlink is not "missing": never create through it. */
  g_autofree char *dangling = g_strdup_printf ("%s/dangling", dir);
  g_autofree char *nowhere = g_strdup_printf ("%s/nowhere", dir);
  assert (symlink (nowhere, dangling) == 0);
  check_rejected ("dangling symlink", dangling, TRUE);
  CHECK (access (nowhere, F_OK) != 0);

  /* A directory in place of the file. */
  g_autofree char *asdir = g_strdup_printf ("%s/asdir", dir);
  assert (mkdir (asdir, 0700) == 0);
  check_rejected ("directory", asdir, FALSE);

  /* Sanity: the valid image itself is accepted. */
  guint8 seed[SEED], ctx[CTX];
  gboolean created = 77;
  GError *error = NULL;
  CHECK (crfpmoc_keys_load_or_create (target, FALSE, seed, ctx, &created, &error));
  CHECK (error == NULL && created == FALSE && memcmp (seed, img + HDR, SEED) == 0 &&
         memcmp (ctx, img + HDR + SEED, CTX) == 0);
  g_clear_error (&error);
}

static void
test_unwritable_parent (void)
{
  guint8 seed[SEED], ctx[CTX];
  gboolean created = 77;
  GError *error = NULL;

  if (geteuid () == 0)
    {
      fprintf (stderr, "note: running as root, unwritable-parent cases skipped\n");
      return;
    }

  /* Key directory exists but cannot be written. */
  g_autofree char *ro = dir_new ("ro", 0500);
  g_autofree char *p1 = g_build_filename (ro, "key", NULL);
  CHECK (!crfpmoc_keys_load_or_create (p1, TRUE, seed, ctx, &created, &error));
  CHECK (error && !g_error_matches (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND));
  CHECK (error && strstr (error->message, p1) != NULL);
  g_clear_error (&error);
  CHECK (entries (ro) == 0);

  /* Key directory missing and its parent cannot be written. */
  g_autofree char *ro2 = dir_new ("ro2", 0500);
  g_autofree char *p2 = g_build_filename (ro2, "keydir", "key", NULL);
  CHECK (!crfpmoc_keys_load_or_create (p2, TRUE, seed, ctx, &created, &error));
  CHECK (error && !g_error_matches (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND));
  g_clear_error (&error);
  CHECK (entries (ro2) == 0);

  /* A group/other-writable key directory is not trusted. */
  g_autofree char *gw = dir_new ("gw", 0700);
  assert (chmod (gw, 0770) == 0);
  g_autofree char *p3 = g_build_filename (gw, "key", NULL);
  CHECK (!crfpmoc_keys_load_or_create (p3, TRUE, seed, ctx, &created, &error));
  g_clear_error (&error);
  CHECK (entries (gw) == 0);

  chmod (ro, 0700);
  chmod (ro2, 0700);
}

static void
test_leftover_temp (void)
{
  g_autofree char *dir = dir_new ("leftover", 0700);
  g_autofree char *path = g_build_filename (dir, "key", NULL);
  static const char *junk[] = { "key.tmp", ".key.tmp", "key.tmp.000000", "key~" };
  guint8 seed[SEED], ctx[CTX];
  gboolean created = 77;
  GError *error = NULL;

  for (gsize i = 0; i < G_N_ELEMENTS (junk); i++)
    {
      g_autofree char *p = g_build_filename (dir, junk[i], NULL);
      write_file (p, (const guint8 *) "junk", 4, 0600);
    }
  CHECK (crfpmoc_keys_load_or_create (path, TRUE, seed, ctx, &created, &error));
  CHECK (error == NULL && created == TRUE);
  g_clear_error (&error);
  CHECK (entries (dir) == (int) G_N_ELEMENTS (junk) + 1);
  for (gsize i = 0; i < G_N_ELEMENTS (junk); i++)
    {
      g_autofree char *p = g_build_filename (dir, junk[i], NULL);
      g_autofree char *c = NULL;
      gsize len = 0;
      CHECK (g_file_get_contents (p, &c, &len, NULL) && len == 4 && memcmp (c, "junk", 4) == 0);
    }
}

static void
test_seed_action (void)
{
  /* (key new to the reader?, FP_ENC_STATUS status) -> send FP_SEED / error */
  static const struct { gboolean key_new; guint32 status; int ok, send; } cases[] = {
    { TRUE,  1, 0, 0 },          /* reader holds another seed: refuse */
    { FALSE, 1, 1, 0 },          /* seed set earlier from this key */
    { TRUE,  0, 1, 1 },
    { FALSE, 0, 1, 1 },
    { TRUE,  1u << 3, 1, 1 },    /* other bits do not count as the seed */
    { TRUE,  (1u << 3) | 1, 0, 0 },
  };
  for (gsize i = 0; i < G_N_ELEMENTS (cases); i++)
    {
      GError *error = NULL;
      gboolean needed = 77, send = 77;
      CHECK (crfpmoc_seed_needed (CRFPMOC_FP_ENC_STATUS_SEED_SET, cases[i].status, &needed, &error));
      CHECK (crfpmoc_keys_seed_action (cases[i].key_new, needed, &send, &error) == cases[i].ok);
      if (cases[i].ok)
        CHECK (error == NULL && send == cases[i].send);
      else
        CHECK (error && strstr (error->message, "reboot") != NULL && send == 77);
      g_clear_error (&error);
    }
}

static void
test_upload_error (void)
{
  GError *e;

  e = crfpmoc_template_upload_error (TRUE, EC_RES_UNAVAILABLE,
                                     g_error_new (FP_DEVICE_ERROR, FP_DEVICE_ERROR_PROTO, "x"));
  CHECK (g_error_matches (e, FP_DEVICE_ERROR, FP_DEVICE_ERROR_DATA_INVALID));
  CHECK (strstr (e->message, "decrypt") && strstr (e->message, "re-enroll"));
  g_clear_error (&e);

  /* Other results, and non-final chunks, keep the original error. */
  e = crfpmoc_template_upload_error (TRUE, EC_RES_INVALID_PARAM,
                                     g_error_new (FP_DEVICE_ERROR, FP_DEVICE_ERROR_PROTO, "x"));
  CHECK (g_error_matches (e, FP_DEVICE_ERROR, FP_DEVICE_ERROR_PROTO) && !strcmp (e->message, "x"));
  g_clear_error (&e);
  e = crfpmoc_template_upload_error (FALSE, EC_RES_UNAVAILABLE,
                                     g_error_new (FP_DEVICE_ERROR, FP_DEVICE_ERROR_PROTO, "x"));
  CHECK (g_error_matches (e, FP_DEVICE_ERROR, FP_DEVICE_ERROR_PROTO) && !strcmp (e->message, "x"));
  g_clear_error (&e);
}

int
main (int argc, char **argv)
{
  assert (argc == 2);
  base = argv[1];
  umask (022);
  test_create_and_reload ();
  test_invalid_files ();
  test_unwritable_parent ();
  test_leftover_temp ();
  test_seed_action ();
  test_upload_error ();
  return failures ? 1 : 0;
}
'''

with tempfile.TemporaryDirectory(prefix="cfp-reference-keys-") as directory:
    test_c = pathlib.Path(directory) / "keys.c"
    binary = pathlib.Path(directory) / "keys"
    work = pathlib.Path(directory) / "work"
    work.mkdir(mode=0o700)
    test_c.write_text(harness)
    flags = shlex.split(subprocess.check_output(
        ["pkg-config", "--cflags", "--libs", "gio-2.0"], text=True))
    subprocess.run(["cc", "-std=gnu11", "-Wall", "-Wextra", "-Werror",
                    *shlex.split(os.environ.get("CFLAGS", "")),
                    "-I", str(driver), str(test_c), str(keys_c),
                    "-o", str(binary), *flags], check=True)
    result = subprocess.run([str(binary), str(work)])
    # Restore permissions so the temporary directory can be removed.
    for root, dirs, _ in os.walk(work):
        for d in dirs:
            os.chmod(os.path.join(root, d), 0o700)
    if result.returncode != 0:
        sys.exit("FAIL: key helper checks failed")
print("reference machine keys: PASS")
