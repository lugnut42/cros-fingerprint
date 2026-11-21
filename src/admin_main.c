/* SPDX-License-Identifier: LGPL-2.1-or-later */
#define _POSIX_C_SOURCE 200809L
#include "cros_fp_admin.h"
#include <pwd.h>
static int account_exists(uint64_t uid,const char *name)
{
    struct passwd *account=getpwnam(name);
    return account && (uint64_t)account->pw_uid==uid;
}
int main(int argc,char **argv)
{
    const struct cfp_admin_platform platform={
        .anchor="/",.relative_path=CFP_PROTOTYPE_STATE,.owner=0,
        .account_exists=account_exists,.timeout_ms=5000
    };
    return cfp_admin_cli(argc,argv,&platform);
}
