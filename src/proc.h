#ifndef __VTOP_PROC_H__
#define __VTOP_PROC_H__

#include "vtop.h"

typedef struct proc_list {
	process_t *items;
	int count;
	int cap;
} proc_list_t;

void proc_list_init(proc_list_t *l);
void proc_list_free(proc_list_t *l);
void proc_list_reset(proc_list_t *l);

int  proc_scan(proc_list_t *out, const opts_t *opts, long page_kb);

void proc_compute_cpu(proc_list_t *cur, const proc_list_t *prev,
		      unsigned long delta_total_jiffies, int ncpu);

void proc_compute_io(proc_list_t *cur, const proc_list_t *prev,
		     double elapsed_sec);

const process_t *proc_find_pid(const proc_list_t *l, pid_t pid);

#endif
