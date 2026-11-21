/* SPDX-License-Identifier: LGPL-2.1-or-later */
#include "cros_fp_binding.h"
#include <string.h>
static int nonzero(const uint8_t id[32])
{
    unsigned value=0;
    for(unsigned i=0;i<32;i++) value|=id[i];
    return value!=0;
}
static int valid_binding(const struct cfp_binding *b)
{
    return b->schema==1 && b->template_format==4 &&
        nonzero(b->reader_generation) && nonzero(b->account_id) &&
        nonzero(b->context_id);
}
enum cfp_status cfp_validate_gallery(const struct cfp_binding *expected,
    const struct cfp_bound_print *prints, size_t count,
    size_t max_template_size, size_t capacity)
{
    if(!expected || !prints || !count || !max_template_size || !capacity)
        return CFP_INVALID_ARGUMENT;
    /* One print per finger bounds validation even with erroneous capacity. */
    if(count>capacity || count>10) return CFP_LIMIT_EXCEEDED;
    if(!valid_binding(expected)) return CFP_COMMAND_DENIED;
    unsigned fingers=0;
    for(size_t i=0;i<count;i++) {
        const struct cfp_bound_print *p=&prints[i];
        const struct cfp_binding *b=&p->binding;
        if(!valid_binding(b) ||
            memcmp(b->reader_generation,expected->reader_generation,32) ||
            memcmp(b->account_id,expected->account_id,32) ||
            memcmp(b->context_id,expected->context_id,32))
            return CFP_COMMAND_DENIED;
        if(p->finger<1 || p->finger>10) return CFP_INVALID_ARGUMENT;
        unsigned bit=1u<<p->finger;
        if(fingers&bit) return CFP_COMMAND_DENIED;
        fingers|=bit;
        if(!p->blob || !p->blob_size) return CFP_INVALID_ARGUMENT;
        if(p->blob_size>max_template_size) return CFP_LIMIT_EXCEEDED;
    }
    return CFP_OK;
}
