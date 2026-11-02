#include "system.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <ctype.h>

int sysstat_init(sysstat_t *s)
{
	memset(s, 0, sizeof(*s));
	s->hertz   = sysconf(_SC_CLK_TCK);
	s->page_kb = sysconf(_SC_PAGESIZE) / 1024;
	s->ncpu    = sysconf(_SC_NPROCESSORS_ONLN);
	if (s->ncpu < 1)
		s->ncpu = 1;
	if (s->hertz < 1)
		s->hertz = 100;
	return 0;
}

static int read_cpu_line(sysstat_t *s)
{
	FILE *f = fopen("/proc/stat", "r");
	if (!f)
		return -1;

	char line[512];
	if (!fgets(line, sizeof(line), f)) {
		fclose(f);
		return -1;
	}
	fclose(f);

	unsigned long v[CPU_NCAT];
	memset(v, 0, sizeof(v));
	int n = sscanf(line, "cpu  %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu",
			&v[0], &v[1], &v[2], &v[3], &v[4],
			&v[5], &v[6], &v[7], &v[8], &v[9]);
	if (n < 4)
		return -1;

	memcpy(s->prev_cat, s->cat, sizeof(s->cat));
	memcpy(s->cat, v, sizeof(v));

	unsigned long total = 0;
	for (int i = 0; i < CPU_NCAT; i++)
		total += v[i];
	s->prev_total   = s->total_jiffies;
	s->prev_idle    = s->idle_jiffies;
	s->total_jiffies = total;
	s->idle_jiffies  = v[3] + v[4]; /* idle + iowait */

	if (!s->have_prev) {
		s->have_prev = 1;
		s->cpu_us = s->cpu_sy = s->cpu_ni = s->cpu_id = 0;
		s->cpu_wa = s->cpu_hi = s->cpu_si = s->cpu_st = 0;
		return 0;
	}

	unsigned long dt = (s->total_jiffies > s->prev_total)
			   ? s->total_jiffies - s->prev_total
			   : 1;
	double scale = 100.0 / (double)dt;
#define D(idx) ((double)(s->cat[idx] - s->prev_cat[idx]) * scale)
	s->cpu_us = D(0);
	s->cpu_ni = D(1);
	s->cpu_sy = D(2);
	s->cpu_id = D(3);
	s->cpu_wa = D(4);
	s->cpu_hi = D(5);
	s->cpu_si = D(6);
	s->cpu_st = D(7);
#undef D
	return 0;
}

static unsigned long meminfo_field(const char *buf, const char *key)
{
	size_t klen = strlen(key);
	const char *p = buf;
	while (p && *p) {
		if (strncmp(p, key, klen) == 0 && p[klen] == ':') {
			p += klen + 1;
			while (*p == ' ' || *p == '\t')
				p++;
			return strtoul(p, NULL, 10);
		}
		p = strchr(p, '\n');
		if (p)
			p++;
	}
	return 0;
}

static int read_meminfo(sysstat_t *s)
{
	FILE *f = fopen("/proc/meminfo", "r");
	if (!f)
		return -1;
	char buf[4096];
	size_t n = fread(buf, 1, sizeof(buf) - 1, f);
	fclose(f);
	buf[n] = '\0';

	s->mem_total_kb  = meminfo_field(buf, "MemTotal");
	s->mem_free_kb   = meminfo_field(buf, "MemFree");
	s->mem_avail_kb  = meminfo_field(buf, "MemAvailable");
	s->buffers_kb    = meminfo_field(buf, "Buffers");
	s->cached_kb     = meminfo_field(buf, "Cached");
	s->swap_total_kb = meminfo_field(buf, "SwapTotal");
	s->swap_free_kb  = meminfo_field(buf, "SwapFree");
	return 0;
}

static int read_loadavg(sysstat_t *s)
{
	FILE *f = fopen("/proc/loadavg", "r");
	if (!f)
		return -1;
	int n = fscanf(f, "%lf %lf %lf",
		       &s->loadavg[0], &s->loadavg[1], &s->loadavg[2]);
	fclose(f);
	return (n == 3) ? 0 : -1;
}

static int psi_avg10(const char *path, const char *prefix, double *out)
{
	FILE *f = fopen(path, "r");
	if (!f)
		return -1;
	char line[256];
	int found = 0;
	size_t plen = strlen(prefix);
	while (fgets(line, sizeof(line), f)) {
		if (strncmp(line, prefix, plen) != 0)
			continue;
		const char *p = strstr(line, "avg10=");
		if (p) {
			*out = strtod(p + 6, NULL);
			found = 1;
		}
		break;
	}
	fclose(f);
	return found ? 0 : -1;
}

static void read_pressure(sysstat_t *s)
{
	s->psi_ok = 0;
	double v;
	if (psi_avg10("/proc/pressure/cpu", "some", &v) == 0) {
		s->psi_cpu = v;
		s->psi_ok = 1;
	} else {
		s->psi_cpu = 0;
	}
	if (psi_avg10("/proc/pressure/memory", "full", &v) == 0) {
		s->psi_mem = v;
		s->psi_ok = 1;
	} else {
		s->psi_mem = 0;
	}
	if (psi_avg10("/proc/pressure/io", "full", &v) == 0) {
		s->psi_io = v;
		s->psi_ok = 1;
	} else {
		s->psi_io = 0;
	}
}

static int read_uptime(sysstat_t *s)
{
	FILE *f = fopen("/proc/uptime", "r");
	if (!f)
		return -1;
	double up = 0.0;
	int n = fscanf(f, "%lf", &up);
	fclose(f);
	if (n != 1)
		return -1;
	s->uptime_sec = (unsigned long)up;
	return 0;
}

int sys_sample(sysstat_t *s)
{
	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);
	if (s->have_ts) {
		s->elapsed_sec = (double)(now.tv_sec - s->ts.tv_sec) +
				 (double)(now.tv_nsec - s->ts.tv_nsec) / 1e9;
		if (s->elapsed_sec <= 0.0) s->elapsed_sec = 0.001;
	} else {
		s->elapsed_sec = 0.0;
		s->have_ts = 1;
	}
	s->ts = now;

	read_cpu_line(s);
	read_meminfo(s);
	read_loadavg(s);
	read_uptime(s);
	read_pressure(s);
	return 0;
}

void sys_count_task_states(sysstat_t *s, const process_t *list, int n)
{
	s->nr_total = n;
	s->nr_running = s->nr_sleeping = s->nr_stopped = s->nr_zombie = 0;
	for (int i = 0; i < n; i++) {
		switch (list[i].state) {
		case 'R': s->nr_running++; break;
		case 'S':
		case 'D':
		case 'I': s->nr_sleeping++; break;
		case 'T':
		case 't': s->nr_stopped++; break;
		case 'Z': s->nr_zombie++;  break;
		default:  s->nr_sleeping++; break;
		}
	}
}
