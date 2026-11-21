/* SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef CROS_FP_LINUX_H
#define CROS_FP_LINUX_H
#include "cros_fp.h"
#include <sys/ioctl.h>
/* Linux host ABI (native endian), unlike EC wire payloads. */
struct cfp_linux_command {
    uint32_t version, command, outsize, insize, result;
    uint8_t data[];
};
#define CFP_IOCTL_COMMAND _IOWR(0xec, 0, struct cfp_linux_command)
#define CFP_RESPONSE_CAPACITY (36 + 20 * CFP_MAX_CAPTURE_FORMATS)
struct cfp_linux {
    int fd;
    int error_number;
    int (*ioctl_command)(int fd, unsigned long request, void *argument);
};
/* Low-level blocking adapter: use only inside a supervised worker.
 * fd is borrowed. The caller owns opening and closing it. No retries.
 */
enum cfp_status cfp_linux_exchange(void *context, uint16_t command,
    uint8_t version, const uint8_t *request, size_t request_size,
    uint8_t *response, size_t capacity, size_t *response_size,
    uint32_t *controller_result);
int cfp_system_ioctl(int fd, unsigned long request, void *argument);
#endif
