#include "sort.h"

#include <stdlib.h>
#include <string.h>

const char *sort_field_label[SORT_NFIELDS] = {
	"%CPU", "%MEM", "DISKR", "DISKW", "PID", "TIME+", "NAME"
};

static int g_reverse;

static int cmp_cpu(const void *a, const void *b)
{
	const row_t *x = a, *y = b;
	double d = y->cpu_pct - x->cpu_pct;
	int r = (d > 0) - (d < 0);
	return g_reverse ? -r : r;
}

static int cmp_mem(const void *a, const void *b)
{
	const row_t *x = a, *y = b;
	double d = y->mem_pct - x->mem_pct;
	int r = (d > 0) - (d < 0);
	return g_reverse ? -r : r;
}

static int cmp_pid(const void *a, const void *b)
{
	const row_t *x = a, *y = b;
	long d = (long)x->key_id - (long)y->key_id;
	int r = (d > 0) - (d < 0);
	return g_reverse ? -r : r;
}

static int cmp_time(const void *a, const void *b)
{
	const row_t *x = a, *y = b;
	long d = (long)y->time_total_jiffies - (long)x->time_total_jiffies;
	int r = (d > 0) - (d < 0);
	return g_reverse ? -r : r;
}

static int cmp_name(const void *a, const void *b)
{
	const row_t *x = a, *y = b;
	int r = strcmp(x->name, y->name);
	return g_reverse ? -r : r;
}

static int cmp_dread(const void *a, const void *b)
{
	const row_t *x = a, *y = b;
	double d = y->read_bps - x->read_bps;
	int r = (d > 0) - (d < 0);
	return g_reverse ? -r : r;
}

static int cmp_dwrite(const void *a, const void *b)
{
	const row_t *x = a, *y = b;
	double d = y->write_bps - x->write_bps;
	int r = (d > 0) - (d < 0);
	return g_reverse ? -r : r;
}

void sort_rows(row_t *rows, int n, enum sort_field f, int reverse)
{
	g_reverse = reverse;
	int (*cmp)(const void *, const void *) = cmp_cpu;
	switch (f) {
	case SORT_CPU:    cmp = cmp_cpu;    break;
	case SORT_MEM:    cmp = cmp_mem;    break;
	case SORT_DREAD:  cmp = cmp_dread;  break;
	case SORT_DWRITE: cmp = cmp_dwrite; break;
	case SORT_PID:    cmp = cmp_pid;    break;
	case SORT_TIME:   cmp = cmp_time;   break;
	case SORT_NAME:   cmp = cmp_name;   break;
	default: break;
	}
	qsort(rows, n, sizeof(row_t), cmp);
}

const char *sort_field_name(enum sort_field f)
{
	if (f < 0 || f >= SORT_NFIELDS)
		return "?";
	return sort_field_label[f];
}
