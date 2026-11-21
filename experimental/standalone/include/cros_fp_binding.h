/* SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef CROS_FP_BINDING_H
#define CROS_FP_BINDING_H
#include "cros_fp.h"
/* In-memory metadata, NOT an on-disk or wire encoding. IDs must come from a
 * trusted persistent registry, never from client-selected print metadata.
 * reader_generation identifies an installation/reader provisioning epoch;
 * account_id identifies an account incarnation, not a reusable UID/name.
 */
struct cfp_binding {
    uint32_t schema;
    uint8_t reader_generation[32], account_id[32], context_id[32];
    uint32_t template_format;
};
struct cfp_bound_print {
    struct cfp_binding binding;
    unsigned finger; /* 1..10; zero/unknown is forbidden. */
    const uint8_t *blob;
    size_t blob_size;
};
/* Pure preflight: no I/O, allocation, blob reads, or authentication claim.
 * expected must be selected from a current, nonretired registry entry by the
 * trusted caller. Caller owns authorization, lifetime, exclusive locking, and
 * immutable metadata through upload. Validate the entire gallery before I/O.
 * max_template_size/capacity must come from validated reader metadata.
 * Only format 4 is supported. Blob crypto/structure is NOT validated here.
 */
enum cfp_status cfp_validate_gallery(const struct cfp_binding *expected,
    const struct cfp_bound_print *prints, size_t count,
    size_t max_template_size, size_t capacity);
#endif
