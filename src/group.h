#ifndef __VTOP_GROUP_H__
#define __VTOP_GROUP_H__

#include "vtop.h"
#include "proc.h"

typedef struct group_list {
	group_t *items;
	int count;
	int cap;
} group_list_t;

void group_list_init(group_list_t *l);
void group_list_free(group_list_t *l);
void group_list_reset(group_list_t *l);

int  group_build(group_list_t *out, const proc_list_t *procs);

int  group_kill(const group_t *g, int sig);
int  group_renice(const group_t *g, int nice);

#endif
