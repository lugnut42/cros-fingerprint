/* SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef CROS_FP_H
#define CROS_FP_H

#include <stddef.h>
#include <stdint.h>

enum cfp_status {
    CFP_OK,
    CFP_INVALID_ARGUMENT,
    CFP_TRANSPORT_ERROR,
    CFP_CONTROLLER_ERROR,
    CFP_MALFORMED_RESPONSE,
    CFP_UNSUPPORTED_VERSION,
    CFP_LIMIT_EXCEEDED,
    CFP_COMMAND_DENIED,
    CFP_PENDING,
    CFP_TIMEOUT,
    CFP_CANCELLED,
    CFP_STORAGE_ERROR,
    CFP_RECOVERY_REQUIRED
};

/* No implicit retries. Production callers must bound execution, e.g. by
 * supervising blocking Linux exchanges in a separate worker process.
 * On CFP_OK, set response_size and controller_result. response_size is the
 * number of valid bytes, not the allocated buffer size. Never exceed capacity.
 * Return CFP_TRANSPORT_ERROR for transport failures (e.g. timeout or access).
 * Buffers are borrowed for the duration of this synchronous call only.
 */
typedef enum cfp_status (*cfp_exchange_fn)(
    void *context, uint16_t command, uint8_t version,
    const uint8_t *request, size_t request_size,
    uint8_t *response, size_t capacity,
    size_t *response_size, uint32_t *controller_result);

struct cfp_transport {
    cfp_exchange_fn exchange;
    void *context;
};

/* Query FP_INFO capabilities and select the newest version understood here
 * (3 or 2). Does not issue FP_INFO or mutate the device. Output is unchanged
 * on failure. Caller owns transport and output; neither may be NULL.
 */
enum cfp_status cfp_negotiate_info(const struct cfp_transport *transport,
                                   uint8_t *version);

/* Application resource bound, not a protocol limit. */
#define CFP_MAX_CAPTURE_FORMATS 32
struct cfp_capture_format {
    uint32_t frame_size, image_data_offset, pixel_format;
    uint16_t width, height, bpp;
    uint8_t capture_type, has_image_data_offset;
};
struct cfp_sensor_info {
    uint32_t vendor, product, model, sensor_version;
    uint32_t template_size, template_dirty, template_version;
    uint16_t template_max, template_valid, num_capture_types;
    uint16_t errors, dead_pixels, fault_flags;
    uint8_t dead_pixels_known;
    struct cfp_capture_format formats[CFP_MAX_CAPTURE_FORMATS];
};
struct cfp_encryption_status {
    uint32_t valid_flags, status;
    uint8_t seed_status_known, seed_set;
};
/* Negotiate FP_MODE v0 and issue only DONT_CHANGE. Output unchanged on failure.
 * The returned configuration is not an acknowledgement of task completion. */
enum cfp_status cfp_query_mode(const struct cfp_transport *transport,uint32_t *mode);

/* Decode little-endian wire data. Outputs are unchanged on failure.
 * FP_INFO accepts trailing buffer padding, bounded by advertised count.
 * Encryption status requires exactly eight bytes. Unknown flags are retained.
 */
enum cfp_status cfp_decode_info(uint8_t version, const uint8_t *data,
    size_t size, struct cfp_sensor_info *info);
enum cfp_status cfp_decode_encryption(const uint8_t *data, size_t size,
    struct cfp_encryption_status *status);
/* Allow only capability queries, FP_INFO v2/v3, FP_ENC_STATUS v0, and
 * the exact FP_MODE v0 DONT_CHANGE query (no additional bits).
 * This is the diagnostic transport entry point; denied requests never reach
 * the backend. Backend response-buffer ownership follows cfp_exchange_fn.
 */
enum cfp_status cfp_readonly_exchange(const struct cfp_transport *transport,
    uint16_t command, uint8_t version, const uint8_t *request,
    size_t request_size, uint8_t *response, size_t capacity,
    size_t *response_size, uint32_t *controller_result);

#endif
