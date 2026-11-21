/* SPDX-License-Identifier: LGPL-2.1-or-later */
#include "cros_fp.h"

static uint16_t le16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | (uint16_t)p[1] << 8);
}
static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)le16(p) | (uint32_t)le16(p + 2) << 16;
}

enum cfp_status cfp_decode_info(uint8_t version, const uint8_t *data,
    size_t size, struct cfp_sensor_info *info)
{
    if (!data || !info)
        return CFP_INVALID_ARGUMENT;
    if (version != 2 && version != 3)
        return CFP_UNSUPPORTED_VERSION;
    if (size < 36)
        return CFP_MALFORMED_RESPONSE;
    uint16_t count = le16(data + 16);
    if (count > CFP_MAX_CAPTURE_FORMATS)
        return CFP_LIMIT_EXCEEDED;
    size_t stride = version == 3 ? 20 : 16;
    /* Count is bounded before arithmetic and indexing. Ignore trailing padding. */
    if (size < 36 + (size_t)count * stride)
        return CFP_MALFORMED_RESPONSE;
    struct cfp_sensor_info decoded = {0};
    decoded.vendor = le32(data);
    decoded.product = le32(data + 4);
    decoded.model = le32(data + 8);
    decoded.sensor_version = le32(data + 12);
    decoded.num_capture_types = count;
    decoded.errors = le16(data + 18);
    decoded.dead_pixels = decoded.errors & 0x03ff;
    decoded.dead_pixels_known = decoded.dead_pixels != 0x03ff;
    decoded.fault_flags = decoded.errors & 0xf000;
    decoded.template_size = le32(data + 20);
    decoded.template_max = le16(data + 24);
    decoded.template_valid = le16(data + 26);
    decoded.template_dirty = le32(data + 28);
    decoded.template_version = le32(data + 32);
    if (decoded.template_valid > decoded.template_max)
        return CFP_MALFORMED_RESPONSE;
    for (size_t i = 0; i < count; i++) {
        const uint8_t *p = data + 36 + i * stride;
        struct cfp_capture_format *format = &decoded.formats[i];
        format->frame_size = le32(p);
        if (version == 3) {
            format->has_image_data_offset = 1;
            format->image_data_offset = le32(p + 4);
            p += 4;
        }
        format->pixel_format = le32(p + 4);
        format->width = le16(p + 8);
        format->height = le16(p + 10);
        format->bpp = le16(p + 12);
        format->capture_type = p[14];
    }
    *info = decoded;
    return CFP_OK;
}

enum cfp_status cfp_decode_encryption(const uint8_t *data, size_t size,
    struct cfp_encryption_status *status)
{
    if (!data || !status)
        return CFP_INVALID_ARGUMENT;
    if (size != 8)
        return CFP_MALFORMED_RESPONSE;
    struct cfp_encryption_status decoded = {0};
    decoded.valid_flags = le32(data);
    decoded.status = le32(data + 4);
    decoded.seed_status_known = (decoded.valid_flags & 1) != 0;
    decoded.seed_set = decoded.seed_status_known && (decoded.status & 1);
    *status = decoded;
    return CFP_OK;
}
