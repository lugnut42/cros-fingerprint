/* SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef CROS_FP_PROVISION_H
#define CROS_FP_PROVISION_H
#include "cros_fp_seed.h"
/* A separate record format; never adopts legacy experiment records. */
struct cfp_provision { unsigned char generation[32], seed[32]; };
/* Same directory, locking, durability, output, and cleanup contract as seed.c.
 * Explicit create stores independent random generation and seed together.
 * These are host provisioning IDs, not proof of physical reader identity.
 */
enum cfp_store_status cfp_provision_create(int directory, uid_t owner,
    const struct cfp_seed_ops *ops, struct cfp_provision *out);
enum cfp_store_status cfp_provision_load(int directory, uid_t owner,
    const struct cfp_seed_ops *ops, struct cfp_provision *out);
#endif
