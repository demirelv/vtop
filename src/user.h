#ifndef __VTOP_USER_H__
#define __VTOP_USER_H__

#include <sys/types.h>

const char *user_name(uid_t uid);
int  user_lookup(const char *name, uid_t *out);

#endif
