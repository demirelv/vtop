#ifndef __VTOP_SYSTEM_H__
#define __VTOP_SYSTEM_H__

#include "vtop.h"

int  sysstat_init(sysstat_t *s);
int  sys_sample(sysstat_t *s);
void sys_count_task_states(sysstat_t *s, const process_t *list, int n);

#endif
