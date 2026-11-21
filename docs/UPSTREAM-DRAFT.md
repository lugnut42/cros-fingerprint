# Draft: comment for libfprint merge request !512

Status: draft, not posted. To be posted by the repo owner from their own
gitlab.freedesktop.org account once the branches are public. Read the existing
review thread on the merge request first (it could not be read while preparing
this) and adjust anything it already covers.

---

**Subject: crfpmoc fixes from testing on an HP Elite Dragonfly Chromebook (redrix): FP_INFO v2/v3 and others**

Hi Marco,

Thanks for carrying this driver. I have it working day to day on an HP Elite
Dragonfly Chromebook (board redrix, FPMCU bloonchipper, RW firmware
`bloonchipper-v2.0.30778-e07b67c`) with fprintd 1.94.5: enroll, verify,
no-match, across reboot and suspend, for sudo, polkit and a lock screen.

Getting there needed a few fixes. I have put the ones that apply to your branch
on top of the current MR head (5644259) as small independent commits:

https://github.com/lugnut42/libfprint/commits/crfpmoc-upstream-fixes

1. **Negotiate the FP_INFO command version.** This firmware advertises FP_INFO
   v2 and v3 only, so the v1 request fails and the device cannot be opened. The
   commit asks `EC_CMD_GET_CMD_VERSIONS` at open, uses the highest of v3/v2/v1,
   and decodes the variable-length v2/v3 reply with bounds checks (reply length,
   advertised capture-format count, `template_valid <= template_max`). An
   `errors` value of 0x03ff (dead-pixel count unknown) is not treated as a fault.
2. **Reject EC replies with an unexpected length.** The ioctl helper accepted
   short successful replies (leaving zero-filled tails to be parsed) and
   truncated long ones.
3. **Do not hex dump EC command request payloads.** With transfer debugging on,
   the FP_SEED and FP_CONTEXT requests were dumped to the log.
4. **Decide whether to send FP_SEED from the seed bit only.** The check was
   `status == 0`; a status with other bits set but no seed skipped FP_SEED and
   later failed with UNAVAILABLE.
5. **Idle the sensor before setting up the enroll context.** FP_CONTEXT's reset
   is rejected with INVALID_PARAM if a mode bit is left set by an interrupted
   operation. This commit also makes that one FP_MODE request ask for the 4-byte
   response, because the 0-byte request is rejected with EMSGSIZE on this
   machine's SPI transport.
6. **build: enable the udev helper for crfpmoc**, so `-Ddrivers=crfpmoc` alone
   configures.

What was and was not tested: the code in these commits is what runs on my
machine, but there it sits on top of three further downstream commits from
acd407's fork (FP_MODE response size / poll callback signature, enroll
interaction, and persistent keys). Your MR head with only the six commits above
builds without warnings and passes the unit tests, but I have not run it on
hardware as is, because prints do not survive with per-session random keys.

Two things I would like your view on before doing more:

- **Tests.** `tests/crfpmoc/custom.ioctl` was recorded with FP_INFO v1 and
  without the version query, so it no longer matches. I can re-record on redrix
  (v3 firmware). Would you rather have a second variant for newer firmware
  alongside the existing recording, or a replacement?
- **Key persistence.** With random per-session seed and context, prints stored
  by fprintd cannot be decrypted after the reader or the daemon restarts. On my
  machine I use a random key generated once and stored root-only, created only
  while the reader holds no seed (so a stale seed leads to a "reboot" error
  rather than prints that break later), with a decrypt failure on template
  upload reported as `FP_DEVICE_ERROR_DATA_INVALID`. That lives on the
  `crfpmoc` branch of the same repository (`crfpmoc-keys.c`). I realise a
  driver-owned file under fprintd's state directory may not be what you want
  upstream; I am happy to rework it toward whatever shape you prefer (a
  client-supplied key, a path from the state directory, something else).

Others have hit the FP_INFO problem independently (for example jbettcher's
`crfpmoc-mtl-fixes` branch and the redrix-linux project), so I am glad to
coordinate with them rather than duplicate work.

I can open these as a merge request against your branch or send them however
is easiest for you.
