/* SPDX-License-Identifier: LGPL-2.1-or-later */
#include "cros_fp.h"

enum cfp_status cfp_readonly_exchange(const struct cfp_transport *transport,
    uint16_t command, uint8_t version, const uint8_t *request,
    size_t request_size, uint8_t *response, size_t capacity,
    size_t *response_size, uint32_t *controller_result)
{
    if (!transport || !transport->exchange || !response_size ||
        !controller_result || (!request && request_size) ||
        (!response && capacity))
        return CFP_INVALID_ARGUMENT;
    int allowed =
        (command == 0x0402 && version == 0 && request_size == 4 &&
         request[0] == 0 && request[1] == 0 && request[2] == 0 && request[3] == 0x80) ||
        (command == 0x0008 && version == 1 && request_size == 2) ||
        (command == 0x0403 && (version == 2 || version == 3) && request_size == 0) ||
        (command == 0x0409 && version == 0 && request_size == 0);
    if (!allowed)
        return CFP_COMMAND_DENIED;
    return transport->exchange(transport->context, command, version,
        request, request_size, response, capacity, response_size,
        controller_result);
}
