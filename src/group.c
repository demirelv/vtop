#include "group.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <errno.h>

void group_list_init(group_list_t *l)
{
	l->items = NULL;
	l->count = 0;
	l->cap   = 0;
}

void group_list_free(group_list_t *l)
{
	for (int i = 0; i < l->cap; i++)
		free(l->items[i].pids);
	free(l->items);
	l->items = NULL;
	l->count = l->cap = 0;
}

void group_list_reset(group_list_t *l)
{
	for (int i = 0; i < l->count; i++) {
		l->items[i].count = 0;
		l->items[i].key[0] = '\0';
		l->items[i].sum_utime = 0;
		l->items[i].sum_stime = 0;
		l->items[i].sum_vsize_kb = 0;
		l->items[i].sum_rss_kb = 0;
		l->items[i].sum_cpu_pct = 0.0;
		l->items[i].sum_read_bps = 0.0;
		l->items[i].sum_write_bps = 0.0;
		l->items[i].thread_count = 0;
		l->items[i].starttime_oldest = 0;
		l->items[i].state_dominant = 'S';
		l->items[i].pri_max = -100;
		l->items[i].nice_min = 100;
	}
	l->count = 0;
}

static group_t *list_push(group_list_t *l)
{
	if (l->count >= l->cap) {
		int n = l->cap ? l->cap * 2 : 64;
		group_t *items = realloc(l->items, n * sizeof(*items));
		if (!items)
			return NULL;
		for (int i = l->cap; i < n; i++) {
			items[i].pids = NULL;
			items[i].pids_cap = 0;
		}
		l->items = items;
		l->cap = n;
	}
	group_t *g = &l->items[l->count++];
	g->count = 0;
	g->key[0] = '\0';
	g->sum_utime = 0;
	g->sum_stime = 0;
	g->sum_vsize_kb = 0;
	g->sum_rss_kb = 0;
	g->sum_cpu_pct = 0.0;
	g->sum_read_bps = 0.0;
	g->sum_write_bps = 0.0;
	g->thread_count = 0;
	g->starttime_oldest = 0;
	g->uid_oldest = 0;
	g->state_dominant = 'S';
	g->pri_max = -100;
	g->nice_min = 100;
	return g;
}

static int group_add_pid(group_t *g, pid_t pid)
{
	if (g->count >= g->pids_cap) {
		int n = g->pids_cap ? g->pids_cap * 2 : 8;
		pid_t *pids = realloc(g->pids, n * sizeof(pid_t));
		if (!pids)
			return -1;
		g->pids = pids;
		g->pids_cap = n;
	}
	g->pids[g->count] = pid;
	return 0;
}

static int state_priority(char s)
{
	switch (s) {
	case 'R': return 5;
	case 'D': return 4;
	case 'T': case 't': return 3;
	case 'Z': return 2;
	case 'S': case 'I': return 1;
	default:  return 0;
	}
}

int group_build(group_list_t *out, const proc_list_t *procs)
{
	group_list_reset(out);

	for (int i = 0; i < procs->count; i++) {
		const process_t *p = &procs->items[i];
		group_t *g = NULL;
		for (int j = 0; j < out->count; j++) {
			if (strcmp(out->items[j].key, p->exe_base) == 0) {
				g = &out->items[j];
				break;
			}
		}
		if (!g) {
			g = list_push(out);
			if (!g)
				return -1;
			size_t k = 0;
			while (k + 1 < sizeof(g->key) && p->exe_base[k])
				g->key[k] = p->exe_base[k], k++;
			g->key[k] = '\0';
			g->starttime_oldest = p->starttime;
			g->uid_oldest = p->uid;
			g->state_dominant = p->state;
			g->pri_max = p->pri;
			g->nice_min = p->nice;
		} else {
			if (p->starttime && (g->starttime_oldest == 0 ||
					     p->starttime < g->starttime_oldest)) {
				g->starttime_oldest = p->starttime;
				g->uid_oldest = p->uid;
			}
			if (state_priority(p->state) > state_priority(g->state_dominant))
				g->state_dominant = p->state;
			if (p->pri > g->pri_max)
				g->pri_max = p->pri;
			if (p->nice < g->nice_min)
				g->nice_min = p->nice;
		}

		group_add_pid(g, p->pid);
		g->count++;
		g->sum_utime     += p->utime;
		g->sum_stime     += p->stime;
		g->sum_vsize_kb  += p->vsize_kb;
		g->sum_rss_kb    += p->rss_kb;
		g->sum_cpu_pct   += p->cpu_pct;
		g->sum_read_bps  += p->read_bps;
		g->sum_write_bps += p->write_bps;
		g->thread_count  += p->num_threads;
	}
	return out->count;
}

int group_kill(const group_t *g, int sig)
{
	int ok = 0;
	for (int i = 0; i < g->count; i++) {
		if (kill(g->pids[i], sig) == 0)
			ok++;
	}
	return ok;
}

int group_renice(const group_t *g, int nice)
{
	int ok = 0;
	for (int i = 0; i < g->count; i++) {
		if (setpriority(PRIO_PROCESS, g->pids[i], nice) == 0)
			ok++;
	}
	return ok;
}
