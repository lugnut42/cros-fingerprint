/* SPDX-License-Identifier: LGPL-2.1-or-later */
#include "cros_fp.h"

enum cfp_status cfp_negotiate_info(const struct cfp_transport *transport,
                                   uint8_t *version)
{
    if (!transport || !transport->exchange || !version)
        return CFP_INVALID_ARGUMENT;
    const uint8_t request[] = {0x03, 0x04};
    uint8_t response[4] = {0};
    size_t length = 0;
    uint32_t result = 0;
    enum cfp_status status = cfp_readonly_exchange(transport,
        0x0008, 1, request, sizeof(request), response, sizeof(response),
        &length, &result);
    if (status != CFP_OK)
        return status;
    if (result != 0)
        return CFP_CONTROLLER_ERROR;
    if (length != sizeof(response))
        return CFP_MALFORMED_RESPONSE;
    if (response[0] & (1u << 3)) {
        *version = 3;
        return CFP_OK;
    }
    if (response[0] & (1u << 2)) {
        *version = 2;
        return CFP_OK;
    }
    return CFP_UNSUPPORTED_VERSION;
}
