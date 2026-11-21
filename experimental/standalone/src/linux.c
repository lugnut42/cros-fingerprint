/* SPDX-License-Identifier: LGPL-2.1-or-later */
#include "cros_fp_linux.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>
_Static_assert(sizeof(struct cfp_linux_command) == 20, "Linux command ABI");
static void erase_packet(void *data,size_t size)
{
    volatile uint8_t *p=data;
    while(size--) *p++=0;
}
int cfp_system_ioctl(int fd, unsigned long request, void *argument)
{ return ioctl(fd, request, argument); }
enum cfp_status cfp_linux_exchange(void *context, uint16_t command,
    uint8_t version, const uint8_t *request, size_t request_size,
    uint8_t *response, size_t capacity, size_t *response_size,
    uint32_t *controller_result)
{
    struct cfp_linux *device = context;
    if (!device || device->fd < 0 || !device->ioctl_command ||
        !response_size || !controller_result || (!request && request_size) ||
        (!response && capacity))
        return CFP_INVALID_ARGUMENT;
    device->error_number = 0;
    if (request_size > CFP_RESPONSE_CAPACITY || capacity > CFP_RESPONSE_CAPACITY)
        return CFP_LIMIT_EXCEEDED;
    size_t payload_size = request_size > capacity ? request_size : capacity;
    struct cfp_linux_command *packet = calloc(1, sizeof(*packet) + payload_size);
    if (!packet) {
        device->error_number = ENOMEM;
        return CFP_TRANSPORT_ERROR;
    }
    packet->command = command;
    packet->version = version;
    packet->outsize = (uint32_t)request_size;
    packet->insize = (uint32_t)capacity;
    if (request_size)
        memcpy(packet->data, request, request_size);
    int received = device->ioctl_command(device->fd, CFP_IOCTL_COMMAND, packet);
    if (received < 0) {
        device->error_number = errno;
        erase_packet(packet,sizeof(*packet)+payload_size);
        free(packet);
        return CFP_TRANSPORT_ERROR;
    }
    if ((size_t)received > capacity) {
        erase_packet(packet,sizeof(*packet)+payload_size);
        free(packet);
        return CFP_MALFORMED_RESPONSE;
    }
    if (received)
        memcpy(response, packet->data, (size_t)received);
    *response_size = (size_t)received;
    *controller_result = packet->result;
    erase_packet(packet,sizeof(*packet)+payload_size);
    free(packet);
    return CFP_OK;
}
