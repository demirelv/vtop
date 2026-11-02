#include "proc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <libgen.h>
#include <sys/types.h>
#include <sys/stat.h>

void proc_list_init(proc_list_t *l)
{
	l->items = NULL;
	l->count = 0;
	l->cap   = 0;
}

void proc_list_free(proc_list_t *l)
{
	free(l->items);
	l->items = NULL;
	l->count = l->cap = 0;
}

void proc_list_reset(proc_list_t *l)
{
	l->count = 0;
}

static process_t *proc_list_push(proc_list_t *l)
{
	if (l->count >= l->cap) {
		int n = l->cap ? l->cap * 2 : 128;
		process_t *p = realloc(l->items, n * sizeof(*p));
		if (!p)
			return NULL;
		l->items = p;
		l->cap   = n;
	}
	process_t *p = &l->items[l->count++];
	memset(p, 0, sizeof(*p));
	return p;
}

static int read_small_file(const char *path, char *buf, size_t cap)
{
	int fd = open(path, O_RDONLY);
	if (fd < 0)
		return -1;
	ssize_t total = 0;
	while ((size_t)total < cap - 1) {
		ssize_t r = read(fd, buf + total, cap - 1 - total);
		if (r < 0) {
			if (errno == EINTR)
				continue;
			close(fd);
			return -1;
		}
		if (r == 0)
			break;
		total += r;
	}
	close(fd);
	buf[total] = '\0';
	return (int)total;
}

static int parse_stat(const char *buf, process_t *p)
{
	const char *lp = strchr(buf, '(');
	const char *rp = strrchr(buf, ')');
	if (!lp || !rp || rp <= lp)
		return -1;

	int clen = (int)(rp - lp - 1);
	if (clen > (int)sizeof(p->comm) - 1)
		clen = sizeof(p->comm) - 1;
	memcpy(p->comm, lp + 1, clen);
	p->comm[clen] = '\0';

	const char *rest = rp + 1;
	while (*rest == ' ')
		rest++;

	char state;
	int ppid;
	int pgrp, session, tty, tpgid;
	unsigned flags;
	unsigned long minflt, cminflt, majflt, cmajflt;
	unsigned long utime, stime;
	long cutime, cstime;
	long priority, nice;
	long num_threads;
	long itrealvalue;
	unsigned long long starttime;
	unsigned long vsize;
	long rss;

	int n = sscanf(rest,
		"%c %d %d %d %d %d %u "
		"%lu %lu %lu %lu "
		"%lu %lu %ld %ld "
		"%ld %ld %ld %ld "
		"%llu %lu %ld",
		&state, &ppid, &pgrp, &session, &tty, &tpgid, &flags,
		&minflt, &cminflt, &majflt, &cmajflt,
		&utime, &stime, &cutime, &cstime,
		&priority, &nice, &num_threads, &itrealvalue,
		&starttime, &vsize, &rss);

	if (n < 22)
		return -1;

	p->state       = state;
	p->ppid        = ppid;
	p->utime       = utime;
	p->stime       = stime;
	p->pri         = priority;
	p->nice        = nice;
	p->num_threads = (int)num_threads;
	p->starttime   = (unsigned long)starttime;
	p->vsize_kb    = vsize / 1024UL;
	p->rss_kb      = rss; /* pages, converted in caller */
	return 0;
}

static int read_status_uid(pid_t pid, process_t *p)
{
	char path[64], buf[4096];
	snprintf(path, sizeof(path), "/proc/%d/status", pid);
	int n = read_small_file(path, buf, sizeof(buf));
	if (n <= 0)
		return -1;
	const char *u = strstr(buf, "\nUid:");
	if (!u)
		return -1;
	u += 5;
	while (*u == ' ' || *u == '\t')
		u++;
	p->uid = (uid_t)strtoul(u, NULL, 10);
	return 0;
}

static int is_version_like(const char *s)
{
	if (!s || !*s)
		return 0;
	/* "x123", "x123_456" — snap revision style */
	if ((s[0] == 'x' || s[0] == 'r') && s[1] >= '0' && s[1] <= '9') {
		for (const char *p = s + 1; *p; p++)
			if (!((*p >= '0' && *p <= '9') || *p == '_' || *p == '.'))
				return 0;
		return 1;
	}
	/* Purely digits with optional . - _ separators */
	int has_digit = 0;
	for (const char *p = s; *p; p++) {
		if (*p >= '0' && *p <= '9') {
			has_digit = 1;
			continue;
		}
		if (*p == '.' || *p == '-' || *p == '_')
			continue;
		return 0;
	}
	return has_digit;
}

/* Generic intermediate directory names that almost never identify an
 * application by themselves. Walked past in read_exe_base to reach the
 * real application/package name. */
static int is_generic_dir(const char *s)
{
	static const char *skip[] = {
		"bin", "sbin", "usr", "opt", "var",
		"current", "stable", "latest", "default",
		"versions", "version", "releases", "release",
		"dist", "build", "snap", "share",
		"app", "apps", "helper", "helpers",
		"x86_64-linux-gnu", "linux-x86_64",
		NULL
	};
	for (int i = 0; skip[i]; i++)
		if (strcmp(s, skip[i]) == 0)
			return 1;
	return 0;
}

static int read_exe_base(pid_t pid, char *out, size_t cap)
{
	char path[64], buf[1024];
	snprintf(path, sizeof(path), "/proc/%d/exe", pid);
	ssize_t n = readlink(path, buf, sizeof(buf) - 1);
	if (n <= 0)
		return -1;
	buf[n] = '\0';

	/* readlink target may include " (deleted)" suffix */
	char *del = strstr(buf, " (deleted)");
	if (del)
		*del = '\0';

	/* Walk up while the candidate looks like a version ("2.1.140", "x140")
	 * or is a generic intermediate dir ("versions", "current", "bin"). Cap
	 * at 4 hops so we never wander off into "opt"/"usr"/"snap". */
	char *slash = strrchr(buf, '/');
	char *bn = slash ? slash + 1 : buf;
	for (int hops = 0; hops < 4 && slash; hops++) {
		if (!is_version_like(bn) && !is_generic_dir(bn))
			break;
		*slash = '\0';
		slash = strrchr(buf, '/');
		bn = slash ? slash + 1 : buf;
	}

	size_t i = 0;
	while (i + 1 < cap && bn[i])
		out[i] = bn[i], i++;
	out[i] = '\0';
	return 0;
}

static int read_io_bytes(pid_t pid, process_t *p)
{
	char path[64], buf[1024];
	snprintf(path, sizeof(path), "/proc/%d/io", pid);
	int n = read_small_file(path, buf, sizeof(buf));
	if (n <= 0)
		return -1; /* permission denied or no io file */
	const char *r = strstr(buf, "read_bytes:");
	const char *w = strstr(buf, "write_bytes:");
	if (!r || !w)
		return -1;
	p->read_bytes  = strtoull(r + 11, NULL, 10);
	p->write_bytes = strtoull(w + 12, NULL, 10);
	p->io_valid = 1;
	return 0;
}

static void read_cmdline(pid_t pid, char *out, size_t cap)
{
	char path[64];
	snprintf(path, sizeof(path), "/proc/%d/cmdline", pid);
	int fd = open(path, O_RDONLY);
	if (fd < 0) {
		out[0] = '\0';
		return;
	}
	ssize_t n = read(fd, out, cap - 1);
	close(fd);
	if (n <= 0) {
		out[0] = '\0';
		return;
	}
	out[n] = '\0';
	for (ssize_t i = 0; i < n - 1; i++) {
		if (out[i] == '\0')
			out[i] = ' ';
	}
	/* trim trailing space */
	while (n > 0 && (out[n - 1] == ' ' || out[n - 1] == '\0')) {
		out[n - 1] = '\0';
		n--;
	}
}

static int filter_keep(const process_t *p, const opts_t *opts)
{
	if (opts->uid_filter != -1 && (int)p->uid != opts->uid_filter)
		return 0;
	if (opts->pid_filter_n > 0) {
		int hit = 0;
		for (int i = 0; i < opts->pid_filter_n; i++) {
			if (opts->pid_filter[i] == p->pid) {
				hit = 1;
				break;
			}
		}
		if (!hit)
			return 0;
	}
	return 1;
}

int proc_scan(proc_list_t *out, const opts_t *opts, long page_kb)
{
	proc_list_reset(out);

	DIR *d = opendir("/proc");
	if (!d)
		return -1;

	struct dirent *de;
	while ((de = readdir(d)) != NULL) {
		if (!isdigit((unsigned char)de->d_name[0]))
			continue;
		pid_t pid = (pid_t)atoi(de->d_name);
		if (pid <= 0)
			continue;

		char path[64], buf[4096];
		snprintf(path, sizeof(path), "/proc/%d/stat", pid);
		int n = read_small_file(path, buf, sizeof(buf));
		if (n <= 0)
			continue;

		process_t *p = proc_list_push(out);
		if (!p)
			break;
		p->pid = pid;

		if (parse_stat(buf, p) < 0) {
			out->count--;
			continue;
		}
		p->rss_kb *= page_kb;

		read_status_uid(pid, p);

		if (read_exe_base(pid, p->exe_base, sizeof(p->exe_base)) < 0) {
			strncpy(p->exe_base, p->comm, sizeof(p->exe_base) - 1);
			p->exe_base[sizeof(p->exe_base) - 1] = '\0';
		}
		read_cmdline(pid, p->cmdline, sizeof(p->cmdline));
		read_io_bytes(pid, p);

		if (!filter_keep(p, opts))
			out->count--;
	}
	closedir(d);
	return out->count;
}

void proc_compute_cpu(proc_list_t *cur, const proc_list_t *prev,
		      unsigned long delta_total_jiffies, int ncpu)
{
	if (delta_total_jiffies == 0)
		delta_total_jiffies = 1;
	double scale = 100.0 * (double)ncpu / (double)delta_total_jiffies;

	for (int i = 0; i < cur->count; i++) {
		process_t *c = &cur->items[i];
		c->cpu_pct = 0.0;
		if (!prev)
			continue;
		for (int j = 0; j < prev->count; j++) {
			const process_t *q = &prev->items[j];
			if (q->pid == c->pid && q->starttime == c->starttime) {
				unsigned long dc = (c->utime + c->stime) -
						   (q->utime + q->stime);
				c->cpu_pct = (double)dc * scale;
				break;
			}
		}
	}
}

void proc_compute_io(proc_list_t *cur, const proc_list_t *prev, double elapsed_sec)
{
	if (elapsed_sec <= 0.0)
		elapsed_sec = 1.0;
	for (int i = 0; i < cur->count; i++) {
		process_t *c = &cur->items[i];
		c->read_bps = c->write_bps = 0.0;
		if (!prev || !c->io_valid)
			continue;
		for (int j = 0; j < prev->count; j++) {
			const process_t *q = &prev->items[j];
			if (q->pid == c->pid && q->starttime == c->starttime && q->io_valid) {
				unsigned long long dr = (c->read_bytes  >= q->read_bytes)
							? c->read_bytes  - q->read_bytes  : 0;
				unsigned long long dw = (c->write_bytes >= q->write_bytes)
							? c->write_bytes - q->write_bytes : 0;
				c->read_bps  = (double)dr / elapsed_sec;
				c->write_bps = (double)dw / elapsed_sec;
				break;
			}
		}
	}
}

const process_t *proc_find_pid(const proc_list_t *l, pid_t pid)
{
	for (int i = 0; i < l->count; i++)
		if (l->items[i].pid == pid)
			return &l->items[i];
	return NULL;
}
