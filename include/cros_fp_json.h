/* SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef CROS_FP_JSON_H
#define CROS_FP_JSON_H
#include "cros_fp_diagnostic.h"
#include <stdio.h>
int cfp_print_json(FILE *stream, const struct cfp_report *report);
#endif
