#include "vtop.h"
#include "proc.h"
#include "system.h"
#include "group.h"
#include "sort.h"
#include "display.h"
#include "input.h"
#include "user.h"
#include "term.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <getopt.h>
#include <errno.h>
#include <ctype.h>
#include <poll.h>
#include <time.h>

static volatile sig_atomic_t g_quit   = 0;
static volatile sig_atomic_t g_resize = 0;

static void on_signal(int sig)
{
	if (sig == SIGWINCH)
		g_resize = 1;
	else
		g_quit = 1;
}

static void usage(FILE *f)
{
	fprintf(f,
"Usage: vtop [OPTIONS]\n"
"  -b           batch mode (non-interactive)\n"
"  -n N         iterate N times then exit\n"
"  -d S         delay between updates (seconds, default 3)\n"
"  -p PID,...   filter by PID list (max %d)\n"
"  -u USER      filter by user name or UID\n"
"  -c           show command line instead of comm\n"
"  -o FIELD     initial sort field: %%CPU|%%MEM|DISKR|DISKW|PID|TIME+|NAME\n"
"  -w COLS      output width (batch)\n"
"  -G           enable aggregation by exe basename (default)\n"
"  +G           disable aggregation (top-like view)\n"
"  -i           start with idle processes hidden\n"
"  -H           threads mode (placeholder, not implemented yet)\n"
"  -h           help\n"
"  -v           version\n",
		MAX_PID_FILTER);
}

static int parse_sort(const char *s, enum sort_field *f)
{
	if (!strcasecmp(s, "%CPU") || !strcasecmp(s, "CPU")) { *f = SORT_CPU; return 0; }
	if (!strcasecmp(s, "%MEM") || !strcasecmp(s, "MEM")) { *f = SORT_MEM; return 0; }
	if (!strcasecmp(s, "DISKR") || !strcasecmp(s, "READ"))  { *f = SORT_DREAD; return 0; }
	if (!strcasecmp(s, "DISKW") || !strcasecmp(s, "WRITE")) { *f = SORT_DWRITE; return 0; }
	if (!strcasecmp(s, "PID"))                          { *f = SORT_PID; return 0; }
	if (!strcasecmp(s, "TIME+") || !strcasecmp(s, "TIME")) { *f = SORT_TIME; return 0; }
	if (!strcasecmp(s, "NAME") || !strcasecmp(s, "COMMAND")) { *f = SORT_NAME; return 0; }
	return -1;
}

static int parse_pids(const char *s, opts_t *o)
{
	const char *p = s;
	while (*p && o->pid_filter_n < MAX_PID_FILTER) {
		while (*p == ',' || *p == ' ')
			p++;
		if (!*p)
			break;
		char *end;
		long v = strtol(p, &end, 10);
		if (end == p)
			return -1;
		if (v > 0)
			o->pid_filter[o->pid_filter_n++] = (pid_t)v;
		p = end;
	}
	return 0;
}

/* Manually strip +G before getopt sees it. */
static void strip_plus_g(int *argc, char **argv, opts_t *o)
{
	int out = 1;
	for (int i = 1; i < *argc; i++) {
		if (strcmp(argv[i], "+G") == 0) {
			o->group_mode = 0;
			continue;
		}
		argv[out++] = argv[i];
	}
	*argc = out;
	argv[out] = NULL;
}

static int parse_args(int argc, char **argv, opts_t *o)
{
	memset(o, 0, sizeof(*o));
	o->delay       = 3.0;
	o->iterations  = 0;
	o->group_mode  = 1;
	o->sort_field  = SORT_CPU;
	o->show_idle   = 1;
	o->uid_filter  = -1;

	strip_plus_g(&argc, argv, o);

	int opt;
	while ((opt = getopt(argc, argv, "bn:d:p:u:cHo:w:Gihv")) != -1) {
		switch (opt) {
		case 'b': o->batch = 1; break;
		case 'n': o->iterations = atoi(optarg); break;
		case 'd': o->delay = strtod(optarg, NULL); break;
		case 'p':
			if (parse_pids(optarg, o) < 0) {
				fprintf(stderr, "vtop: invalid -p list\n");
				return -1;
			}
			break;
		case 'u': {
			uid_t uid;
			if (isdigit((unsigned char)optarg[0]))
				o->uid_filter = atoi(optarg);
			else if (user_lookup(optarg, &uid) == 0)
				o->uid_filter = (int)uid;
			else {
				fprintf(stderr, "vtop: unknown user %s\n", optarg);
				return -1;
			}
			break;
		}
		case 'c': o->show_cmdline = 1; break;
		case 'H': o->thread_mode = 1; break;
		case 'o':
			if (parse_sort(optarg, &o->sort_field) < 0) {
				fprintf(stderr, "vtop: bad -o field '%s'\n", optarg);
				return -1;
			}
			break;
		case 'w': o->width = atoi(optarg); break;
		case 'G': o->group_mode = 1; break;
		case 'i': o->show_idle = 0; break;
		case 'h': o->show_help = 1; break;
		case 'v': o->show_version = 1; break;
		default:
			usage(stderr);
			return -1;
		}
	}
	if (o->delay <= 0)
		o->delay = 3.0;
	return 0;
}

static void sleep_seconds(double sec)
{
	struct timespec ts;
	ts.tv_sec  = (time_t)sec;
	ts.tv_nsec = (long)((sec - (double)ts.tv_sec) * 1e9);
	nanosleep(&ts, NULL);
}

static int collect_rows(const proc_list_t *procs, const group_list_t *grps,
			const opts_t *opts, const sysstat_t *sys,
			row_t **rows_out, int *cap_out)
{
	int need = opts->group_mode ? grps->count : procs->count;
	if (need > *cap_out) {
		int n = *cap_out ? *cap_out * 2 : 256;
		while (n < need) n *= 2;
		row_t *r = realloc(*rows_out, n * sizeof(row_t));
		if (!r)
			return -1;
		*rows_out = r;
		*cap_out  = n;
	}
	if (opts->group_mode)
		rows_from_groups(*rows_out, grps->items, grps->count,
				 sys->mem_total_kb, sys->hertz);
	else
		rows_from_procs(*rows_out, procs->items, procs->count,
				sys->mem_total_kb, sys->hertz, opts->show_cmdline);
	return need;
}

static int apply_idle_filter(row_t *rows, int n, int show_idle)
{
	if (show_idle)
		return n;
	int w = 0;
	for (int i = 0; i < n; i++) {
		if (rows[i].cpu_pct > 0.05 || rows[i].state == 'R')
			rows[w++] = rows[i];
	}
	return w;
}

int main(int argc, char **argv)
{
	opts_t opts;
	if (parse_args(argc, argv, &opts) < 0)
		return 2;

	if (opts.show_help) { usage(stdout); return 0; }
	if (opts.show_version) { printf("vtop %s\n", VTOP_VERSION); return 0; }

	sysstat_t sys;
	sysstat_init(&sys);

	proc_list_t a, b;
	proc_list_init(&a);
	proc_list_init(&b);
	proc_list_t *cur = &a, *prev = &b;

	group_list_t groups;
	group_list_init(&groups);

	row_t *rows = NULL;
	int rows_cap = 0;
	int last_nrows = 0;

	ui_state_t ui;
	memset(&ui, 0, sizeof(ui));

	proc_list_t drill;
	proc_list_init(&drill);

	signal(SIGINT,  on_signal);
	signal(SIGTERM, on_signal);
	signal(SIGWINCH, on_signal);

	if (display_init(opts.batch) < 0) {
		fprintf(stderr, "vtop: display init failed\n");
		return 1;
	}

	/* First sample to seed CPU delta. */
	sys_sample(&sys);
	proc_scan(prev, &opts, sys.page_kb);

	int iter = 0;
	while (!g_quit) {
		if (g_resize) {
			g_resize = 0;
			display_resize();
		}

		if (opts.batch) {
			sleep_seconds(opts.delay);
		} else {
			/* Poll stdin for input while waiting for next refresh. */
			double remaining = opts.delay;
			int got_key = 0;
			while (remaining > 0.0 && !g_quit && !g_resize) {
				double slice = remaining > 0.2 ? 0.2 : remaining;
				int key = term_read_key((int)(slice * 1000.0));
				if (key != TKEY_NONE) {
					input_result_t r = input_handle(key, &opts, &ui,
									rows, last_nrows,
									cur, &groups);
					if (r == IN_QUIT) { g_quit = 1; break; }
					if (r == IN_REFRESH) { got_key = 1; break; }
					if (r == IN_REDRAW) { got_key = 1; break; }
				}
				remaining -= slice;
			}
			(void)got_key;
		}
		if (g_quit) break;

		sys_sample(&sys);
		proc_scan(cur, &opts, sys.page_kb);

		unsigned long dt = (sys.total_jiffies > sys.prev_total)
				   ? sys.total_jiffies - sys.prev_total
				   : 1;
		proc_compute_cpu(cur, prev, dt, sys.ncpu);
		proc_compute_io(cur, prev, sys.elapsed_sec);
		sys_count_task_states(&sys, cur->items, cur->count);

		if (opts.group_mode)
			group_build(&groups, cur);

		/* If drilling, replace 'view' with only the processes of that group
		 * and render as proc rows regardless of group_mode. */
		const proc_list_t *view_procs = cur;
		const group_list_t *view_groups = &groups;
		opts_t view_opts = opts;
		if (ui.in_drill) {
			proc_list_reset(&drill);
			for (int i = 0; i < cur->count; i++) {
				if (strcmp(cur->items[i].exe_base, ui.drill_key) == 0) {
					if (drill.count >= drill.cap) {
						int nc = drill.cap ? drill.cap * 2 : 16;
						process_t *p = realloc(drill.items, nc * sizeof(*p));
						if (!p) break;
						drill.items = p;
						drill.cap   = nc;
					}
					drill.items[drill.count++] = cur->items[i];
				}
			}
			view_procs = &drill;
			view_opts.group_mode = 0;
			if (drill.count == 0) {
				/* Group disappeared (all processes ended). Exit drill. */
				ui.in_drill = 0;
				ui.drill_key[0] = '\0';
				view_procs = cur;
				view_opts = opts;
			}
		}

		int n = collect_rows(view_procs, view_groups, &view_opts, &sys, &rows, &rows_cap);
		if (n < 0) {
			display_message("OOM");
			break;
		}
		n = apply_idle_filter(rows, n, opts.show_idle);
		sort_rows(rows, n, view_opts.sort_field, view_opts.sort_reverse);

		if (ui.search_dirty && ui.search[0]) {
			for (int i = 0; i < n; i++) {
				if (strcasestr(rows[i].name, ui.search)) {
					ui.focus_idx = i;
					break;
				}
			}
			ui.search_dirty = 0;
		}

		if (ui.focus_idx >= n) ui.focus_idx = n - 1;
		if (ui.focus_idx < 0)  ui.focus_idx = 0;
		if (ui.view_offset > n - 1) ui.view_offset = n - 1;
		if (ui.view_offset < 0)     ui.view_offset = 0;

		/* Keep focus on screen after sort/scroll/group toggle */
		if (!opts.batch) {
			int rh = term_rows() - 8;
			if (rh < 1) rh = 1;
			if (ui.focus_idx < ui.view_offset)
				ui.view_offset = ui.focus_idx;
			else if (ui.focus_idx >= ui.view_offset + rh)
				ui.view_offset = ui.focus_idx - rh + 1;
		}

		display_render(&sys, rows, n, &view_opts, &ui, NULL);
		last_nrows = n;

		if (!opts.batch) {
			/* Drain any buffered keys without blocking. */
			int ch;
			while ((ch = term_read_key(0)) != TKEY_NONE) {
				input_result_t r = input_handle(ch, &opts, &ui,
								rows, last_nrows,
								cur, &groups);
				if (r == IN_QUIT) { g_quit = 1; break; }
			}
		}

		proc_list_t *tmp = prev;
		prev = cur;
		cur  = tmp;

		iter++;
		if (opts.iterations > 0 && iter >= opts.iterations)
			break;
	}

	display_shutdown();
	free(rows);
	group_list_free(&groups);
	proc_list_free(&a);
	proc_list_free(&b);
	proc_list_free(&drill);
	return 0;
}
