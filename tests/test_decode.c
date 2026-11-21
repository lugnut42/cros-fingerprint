/* SPDX-License-Identifier: LGPL-2.1-or-later */
#include "cros_fp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%d: %s\n", __LINE__, #x); exit(1); } } while (0)
static void put16(uint8_t *p, uint16_t v) { p[0]=v; p[1]=v>>8; }
static void put32(uint8_t *p, uint32_t v) { put16(p,v); put16(p+2,v>>16); }
static void fixture(uint8_t *p, unsigned version, unsigned count)
{
    memset(p, 0, 256);
    put32(p,541282374); put32(p+4,9); put32(p+8,543); put32(p+12,1);
    put16(p+16,count); put16(p+18,0x03ff); put32(p+20,5156);
    put16(p+24,5); put16(p+26,2); put32(p+28,3); put32(p+32,4);
    for (unsigned i=0;i<count;i++) {
        uint8_t *q=p+36+i*(version==3?20:16);
        put32(q,25600);
        if(version==3) { put32(q+4,400); q+=4; }
        put32(q+4,0x59455247); put16(q+8,160); put16(q+10,160);
        put16(q+12,8); q[14]=(uint8_t)i;
    }
}
static void info_tests(void)
{
    uint8_t bytes[256];
    struct cfp_sensor_info out, before;
    memset(&before,0xa5,sizeof(before));
    for(unsigned v=2;v<=3;v++) {
        fixture(bytes,v,6);
        CHECK(cfp_decode_info(v,bytes,sizeof(bytes),&out)==CFP_OK);
        CHECK(out.vendor==541282374 && out.product==9 && out.model==543);
        CHECK(out.sensor_version==1 && out.num_capture_types==6);
        CHECK(out.template_size==5156 && out.template_max==5 && out.template_valid==2);
        CHECK(out.template_dirty==3 && out.template_version==4);
        CHECK(out.errors==0x03ff && !out.dead_pixels_known && out.fault_flags==0);
        for(unsigned i=0;i<6;i++) {
            CHECK(out.formats[i].frame_size==25600 && out.formats[i].pixel_format==0x59455247);
            CHECK(out.formats[i].width==160 && out.formats[i].height==160 && out.formats[i].bpp==8);
            CHECK(out.formats[i].capture_type==i);
            CHECK(out.formats[i].has_image_data_offset==(v==3));
            CHECK(out.formats[i].image_data_offset==(v==3?400u:0u));
        }
        CHECK(out.formats[6].frame_size==0); /* padding isn't a seventh format */
        size_t required=36+6*(v==3?20:16);
        for(size_t n=0;n<required;n++) {
            out=before;
            CHECK(cfp_decode_info(v,bytes,n,&out)==CFP_MALFORMED_RESPONSE);
            CHECK(memcmp(&out,&before,sizeof(out))==0);
        }
        CHECK(cfp_decode_info(v,bytes,required,&out)==CFP_OK);
    }
    fixture(bytes,3,0); put16(bytes+18,0xfc07);
    CHECK(cfp_decode_info(3,bytes,36,&out)==CFP_OK);
    CHECK(out.dead_pixels_known && out.dead_pixels==7 && out.fault_flags==0xf000 && out.errors==0xfc07);
    out=before;
    put16(bytes+16,65535);
    CHECK(cfp_decode_info(3,bytes,sizeof(bytes),&out)==CFP_LIMIT_EXCEEDED);
    CHECK(memcmp(&out,&before,sizeof(out))==0);
    fixture(bytes,3,0); put16(bytes+26,6);
    CHECK(cfp_decode_info(3,bytes,36,&out)==CFP_MALFORMED_RESPONSE);
    CHECK(memcmp(&out,&before,sizeof(out))==0);
    CHECK(cfp_decode_info(1,bytes,36,&out)==CFP_UNSUPPORTED_VERSION);
    CHECK(cfp_decode_info(3,NULL,36,&out)==CFP_INVALID_ARGUMENT);
    CHECK(cfp_decode_info(3,bytes,36,NULL)==CFP_INVALID_ARGUMENT);
}
static void encryption_tests(void)
{
    uint8_t bytes[9]={0};
    struct cfp_encryption_status out, before;
    memset(&before,0xa5,sizeof(before));
    put32(bytes,1);
    CHECK(cfp_decode_encryption(bytes,8,&out)==CFP_OK);
    CHECK(out.seed_status_known && !out.seed_set);
    put32(bytes+4,1);
    CHECK(cfp_decode_encryption(bytes,8,&out)==CFP_OK && out.seed_set);
    put32(bytes,0x80000000); put32(bytes+4,0x80000001);
    CHECK(cfp_decode_encryption(bytes,8,&out)==CFP_OK);
    CHECK(!out.seed_status_known && !out.seed_set);
    CHECK(out.valid_flags==0x80000000 && out.status==0x80000001);
    for(size_t n=0;n<=9;n++) {
        if(n==8) continue;
        out=before;
        CHECK(cfp_decode_encryption(bytes,n,&out)==CFP_MALFORMED_RESPONSE);
        CHECK(memcmp(&out,&before,sizeof(out))==0);
    }
    CHECK(cfp_decode_encryption(NULL,8,&out)==CFP_INVALID_ARGUMENT);
    CHECK(cfp_decode_encryption(bytes,8,NULL)==CFP_INVALID_ARGUMENT);
}
int main(int argc,char **argv)
{
    if(argc==1 || strcmp(argv[1],"info")==0) info_tests();
    if(argc==1 || strcmp(argv[1],"encryption")==0) encryption_tests();
    puts("Decoder checks passed"); return 0;
}
