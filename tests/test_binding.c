/* SPDX-License-Identifier: LGPL-2.1-or-later */
#include "cros_fp_binding.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"%d: %s\n",__LINE__,#x); exit(1); } } while(0)
static const uint8_t synthetic_blob[64]={0x42};
static struct cfp_binding owner;
static struct cfp_bound_print prints[2];
static void setup(void)
{
    memset(&owner,0,sizeof(owner));
    owner.schema=1; owner.template_format=4;
    memset(owner.reader_generation,0x11,32);
    memset(owner.account_id,0x22,32);
    memset(owner.context_id,0x33,32);
    for(unsigned i=0;i<2;i++) {
        prints[i].binding=owner; prints[i].finger=i+1;
        prints[i].blob=synthetic_blob; prints[i].blob_size=sizeof(synthetic_blob);
    }
}
static enum cfp_status validate(void)
{ return cfp_validate_gallery(&owner,prints,2,64,5); }
int main(void)
{
    setup();
    CHECK(cfp_validate_gallery(&owner,prints,0,64,5)!=CFP_OK);
    CHECK(validate()==CFP_OK);
    CHECK(cfp_validate_gallery(NULL,prints,2,64,5)!=CFP_OK);
    CHECK(cfp_validate_gallery(&owner,NULL,2,64,5)!=CFP_OK);
    CHECK(cfp_validate_gallery(&owner,prints,2,0,5)!=CFP_OK);
    CHECK(cfp_validate_gallery(&owner,prints,2,64,0)!=CFP_OK);
    CHECK(cfp_validate_gallery(&owner,prints,2,64,1)!=CFP_OK);
    CHECK(cfp_validate_gallery(&owner,prints,SIZE_MAX,64,5)!=CFP_OK);
    /* Every byte of all three identities matters, including late entries. */
    for(unsigned field=0;field<3;field++) for(unsigned byte=0;byte<32;byte++) {
        setup();
        uint8_t *ids[]={prints[1].binding.reader_generation,
            prints[1].binding.account_id,prints[1].binding.context_id};
        ids[field][byte]^=1;
        CHECK(validate()!=CFP_OK);
    }
    /* Even matching metadata cannot legitimize zero identities or versions. */
    for(unsigned field=0;field<3;field++) {
        setup();
        uint8_t *ids[]={owner.reader_generation,owner.account_id,owner.context_id};
        memset(ids[field],0,32);
        prints[0].binding=prints[1].binding=owner;
        CHECK(validate()!=CFP_OK);
    }
    const uint32_t schemas[]={0,2,UINT32_MAX};
    for(unsigned i=0;i<3;i++) {
        setup(); owner.schema=schemas[i];
        prints[0].binding=prints[1].binding=owner;
        CHECK(validate()!=CFP_OK);
        setup(); prints[1].binding.schema=schemas[i]; CHECK(validate()!=CFP_OK);
    }
    const uint32_t formats[]={0,3,5,UINT32_MAX};
    for(unsigned i=0;i<4;i++) {
        setup(); owner.template_format=formats[i];
        prints[0].binding=prints[1].binding=owner;
        CHECK(validate()!=CFP_OK);
        setup(); prints[1].binding.template_format=formats[i]; CHECK(validate()!=CFP_OK);
    }
    setup(); prints[1].finger=0; CHECK(validate()!=CFP_OK);
    setup(); prints[1].finger=11; CHECK(validate()!=CFP_OK);
    setup(); prints[1].finger=prints[0].finger; CHECK(validate()!=CFP_OK);
    setup(); prints[1].finger=10; CHECK(validate()==CFP_OK);
    setup(); prints[1].blob=NULL; CHECK(validate()!=CFP_OK);
    setup(); prints[1].blob_size=0; CHECK(validate()!=CFP_OK);
    setup(); prints[1].blob_size=65; CHECK(validate()!=CFP_OK);
    setup(); prints[1].blob_size=SIZE_MAX; CHECK(validate()!=CFP_OK);
    /* A recreated account cannot inherit old prints, even on the same reader. */
    setup(); owner.account_id[0]^=1; owner.context_id[0]^=1;
    CHECK(validate()!=CFP_OK);
    puts("Account/reader gallery binding checks passed");
    return 0;
}
