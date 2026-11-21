# Target hardware

Observed on the one tested machine. Other Chromebooks are untested.

| Item | Observation |
| --- | --- |
| Machine | HP Elite Dragonfly Chromebook, Redrix |
| DMI family | Google_Brya |
| Distribution | Omarchy, Arch-based |
| Kernel drivers present | in-tree, no out-of-tree module needed |
| Transport | SPI; driver `cros-ec-spi` |
| Userspace device | `/dev/cros_fp`, root-owned, mode 0600 |
| Relevant loaded modules | `cros_ec_spi`, `cros_ec_dev`, `cros_ec_chardev` |
| Controller | ST STM32F412CX, firmware board `bloonchipper` |
| RO firmware | `bloonchipper_v2.0.5938-197506c1` |
| RW firmware | `bloonchipper-v2.0.30778-e07b67c` |
| Active firmware | RW |
| Sensor identifiers | vendor 541282374; product 9; model 543; version 1 |
| Reported image dimensions | 160 × 160, 8 bits per pixel |
| Advertised capture formats | 6 |
| Maximum loaded templates | 5 |
| Currently loaded templates | 0 |
| Maximum template size | 5156 bytes |
| Template format version | 4 |
| Encryption seed status | Not set after a cold boot; the driver sets it when needed |

`/dev/cros_fp` exists without extra kernel work. Sandboxed tools may hide device
nodes; check from a normal shell before concluding it is missing.

## Advertised command versions

These were read with `EC_CMD_GET_CMD_VERSIONS` version 1. Querying support did
not execute the commands being queried.

| Command | ID | Versions |
| --- | --- | --- |
| FP_MODE | 0x0402 | 0, 1 |
| FP_INFO | 0x0403 | 2, 3 |
| FP_FRAME | 0x0404 | 0, 1 |
| FP_TEMPLATE | 0x0405 | 0, 1 |
| FP_CONTEXT | 0x0406 | 1 |
| FP_STATS | 0x0407 | 0 |
| FP_SEED | 0x0408 | 0 |
| FP_ENC_STATUS | 0x0409 | 0 |

Sensor details were read using FP_INFO version 3. FP_ENC_STATUS returned
`valid_flags=1`, `status=0`. Sensor `errors=1023` (`0x03ff`) means the dead-pixel
count is unknown; the separate no-IRQ, SPI communication, bad hardware-ID, and
initialization-failure bits were not set. This does not constitute a capture
test or prove the entire sensor is healthy.

## Protocol compatibility finding

The installed Linux header described FP_INFO versions 0/1, while this firmware
advertises only versions 2/3. The current ChromeOS header documents these newer
layouts. Capability negotiation is a requirement; the driver patches handle this.

## Behaviour worth knowing

- The seed is held in the reader's RAM only: it is cleared by a reboot and must
  be set again by the host. The driver sets it when needed.
- Capability queries must not run the commands being queried.
- Sensor `errors=1023` (`0x03ff`) means the dead-pixel count is unknown; it is
  not a fault by itself.
- Reader firmware cannot be updated from Linux with this setup; what ships is
  what you get.

Biometric quality, false accept/reject rates and spoof resistance have not been
measured.
