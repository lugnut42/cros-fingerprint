/* SPDX-License-Identifier: LGPL-2.1-or-later */
#include "cros_fp_json.h"
#include <inttypes.h>
int cfp_print_json(FILE *stream,const struct cfp_report *r)
{
    if(!stream || !r || r->status!=CFP_OK || r->info.num_capture_types>CFP_MAX_CAPTURE_FORMATS)
        return 1;
    const struct cfp_sensor_info *s=&r->info;
    fprintf(stream,"{\"schema_version\":1,\"info_version\":%u,\"sensor\":{"
        "\"vendor\":%" PRIu32 ",\"product\":%" PRIu32 ",\"model\":%" PRIu32
        ",\"sensor_version\":%" PRIu32 ",\"errors\":%u,\"fault_flags\":%u,\"dead_pixels\":",
        (unsigned)r->info_version,s->vendor,s->product,s->model,s->sensor_version,
        (unsigned)s->errors,(unsigned)s->fault_flags);
    if(s->dead_pixels_known) fprintf(stream,"%u",(unsigned)s->dead_pixels);
    else fputs("null",stream);
    fprintf(stream,",\"template_size\":%" PRIu32 ",\"template_capacity\":%u,"
        "\"templates_loaded\":%u,\"template_dirty_bitmap\":%" PRIu32
        ",\"template_format_version\":%" PRIu32 ",\"formats\":[",
        s->template_size,(unsigned)s->template_max,(unsigned)s->template_valid,
        s->template_dirty,s->template_version);
    for(unsigned i=0;i<s->num_capture_types;i++) {
        const struct cfp_capture_format *f=&s->formats[i];
        fprintf(stream,"%s{\"frame_size\":%" PRIu32 ",\"pixel_format\":%" PRIu32
            ",\"width\":%u,\"height\":%u,\"bits_per_pixel\":%u,\"capture_type\":%u,"
            "\"image_data_offset\":",i?",":"",f->frame_size,f->pixel_format,
            (unsigned)f->width,(unsigned)f->height,(unsigned)f->bpp,(unsigned)f->capture_type);
        if(f->has_image_data_offset) fprintf(stream,"%" PRIu32,f->image_data_offset);
        else fputs("null",stream);
        fputs("}",stream);
    }
    fprintf(stream,"]},\"encryption\":{\"valid_flags\":%" PRIu32
        ",\"status\":%" PRIu32 ",\"seed_set\":",r->encryption.valid_flags,r->encryption.status);
    fputs(!r->encryption.seed_status_known?"null":r->encryption.seed_set?"true":"false",stream);
    fputs("}}\n",stream);
    return ferror(stream)?1:0;
}
