/* SPDX-License-Identifier: LGPL-2.1-or-later */
#include "cros_fp_json.h"
int main(void)
{
    struct cfp_report r={.status=CFP_OK,.info_version=3};
    r.info.model=543; r.info.num_capture_types=1;
    r.info.formats[0].width=160; r.info.formats[0].height=160;
    r.info.formats[0].has_image_data_offset=1;
    r.info.formats[0].image_data_offset=400;
    return cfp_print_json(stdout,&r);
}
