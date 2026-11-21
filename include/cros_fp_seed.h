/* SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef CROS_FP_SEED_H
#define CROS_FP_SEED_H
#include <stddef.h>
#include <sys/types.h>
enum cfp_store_status {
    CFP_STORE_OK, CFP_STORE_MISSING, CFP_STORE_UNSAFE, CFP_STORE_CORRUPT,
    CFP_STORE_EXISTS, CFP_STORE_BUSY, CFP_STORE_IO, CFP_STORE_RANDOM,
    CFP_STORE_RECOVERY
};
/* Trusted injection seam. NULL ops selects OS calls. Short randomness is an
 * error; short writes are completed. Callbacks follow syscall return rules. */
struct cfp_seed_ops {
    ssize_t (*random_bytes)(void *, size_t);
    ssize_t (*write_bytes)(int, const void *, size_t);
    int (*sync_fd)(int);
    int (*publish)(int, const char *, int, const char *, int);
};
/* Caller supplies an already securely opened private directory and expected
 * owner (0 in deployment). Ancestor traversal/creation is the caller's job.
 * Require mode 0700, matching owner. Each call takes an independent flock.
 * No device access. Creation requires an empty directory, never replaces state.
 * Outputs remain unchanged on failure. Caller must erase successful outputs.
 * Failed writes may leave .seed.pending for explicit recovery; never auto-delete.
 */
enum cfp_store_status cfp_seed_create(int directory, uid_t owner,
    const struct cfp_seed_ops *ops, unsigned char seed[32]);
enum cfp_store_status cfp_seed_load(int directory, uid_t owner,
    const struct cfp_seed_ops *ops, unsigned char seed[32]);
/* Same storage guarantees for a context ID in its own empty directory.
 * Context records have a distinct filename and format tag; they are not seeds.
 */
enum cfp_store_status cfp_context_id_create(int directory, uid_t owner,
    const struct cfp_seed_ops *ops, unsigned char context[32]);
enum cfp_store_status cfp_context_id_load(int directory, uid_t owner,
    const struct cfp_seed_ops *ops, unsigned char context[32]);
void cfp_seed_erase(void *data, size_t size);
#endif
