#include "display.h"
#include "user.h"
#include "sort.h"
#include "term.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <time.h>

static int g_batch = 1;
static char g_message[256];

int display_init(int batch)
{
	g_batch = batch;
	g_message[0] = '\0';
	return term_init(batch);
}

void display_shutdown(void)
{
	term_shutdown();
}

void display_resize(void)
{
	/* term_size() refreshes via ioctl on every call */
}

void display_message(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(g_message, sizeof(g_message), fmt, ap);
	va_end(ap);
}

static int pct_color(double v, double yellow, double red)
{
	if (v >= red)    return T_FG_RED;
	if (v >= yellow) return T_FG_YELLOW;
	return 0;
}

static void fmt_uptime(unsigned long sec, char *out, int cap)
{
	unsigned long days = sec / 86400;
	unsigned long rem  = sec % 86400;
	unsigned long hh   = rem / 3600;
	unsigned long mm   = (rem % 3600) / 60;
	if (days > 0)
		snprintf(out, cap, "up %lu days, %2lu:%02lu", days, hh, mm);
	else
		snprintf(out, cap, "up %2lu:%02lu", hh, mm);
}

static void fmt_kb(unsigned long kb, char *out, int cap)
{
	if (kb < 100000UL)
		snprintf(out, cap, "%7lu", kb);
	else if (kb < 100UL * 1024UL * 1024UL)
		snprintf(out, cap, "%6.1fm", (double)kb / 1024.0);
	else
		snprintf(out, cap, "%6.1fg", (double)kb / (1024.0 * 1024.0));
}

/* Format a byte/sec rate into a fixed 5-char field. */
static void fmt_rate(double bps, char *out, int cap)
{
	if (bps < 1.0) {
		snprintf(out, cap, "    -");
	} else if (bps < 1024.0) {
		snprintf(out, cap, "%4.0fB", bps);
	} else if (bps < 1024.0 * 1024.0) {
		double k = bps / 1024.0;
		if (k < 10)       snprintf(out, cap, " %3.1fK", k);
		else if (k < 100) snprintf(out, cap, " %3.0fK", k);
		else              snprintf(out, cap, "%4.0fK", k);
	} else if (bps < 1024.0 * 1024.0 * 1024.0) {
		double m = bps / (1024.0 * 1024.0);
		if (m < 10)       snprintf(out, cap, " %3.1fM", m);
		else if (m < 100) snprintf(out, cap, " %3.0fM", m);
		else              snprintf(out, cap, "%4.0fM", m);
	} else {
		double g = bps / (1024.0 * 1024.0 * 1024.0);
		snprintf(out, cap, " %3.1fG", g);
	}
}

static void fmt_time_plus(unsigned long total_jiffies, long hertz,
			  char *out, int cap)
{
	if (hertz <= 0)
		hertz = 100;
	unsigned long total_cs = total_jiffies * 100UL / (unsigned long)hertz;
	unsigned long secs = total_cs / 100;
	unsigned long cs   = total_cs % 100;
	unsigned long mins = secs / 60;
	unsigned long ss   = secs % 60;
	if (mins < 1000)
		snprintf(out, cap, "%4lu:%02lu.%02lu", mins, ss, cs);
	else
		snprintf(out, cap, "%6luh", mins / 60);
}

void rows_from_procs(row_t *rows, const process_t *procs, int n,
		     unsigned long mem_total_kb, long hertz, int show_cmdline)
{
	(void)hertz;
	for (int i = 0; i < n; i++) {
		const process_t *p = &procs[i];
		row_t *r = &rows[i];
		memset(r, 0, sizeof(*r));
		r->kind     = ROW_PROC;
		r->key_id   = (unsigned long)p->pid;
		r->uid      = p->uid;
		strncpy(r->user, user_name(p->uid), sizeof(r->user) - 1);
		r->pri      = p->pri;
		r->nice     = p->nice;
		r->vsize_kb = p->vsize_kb;
		r->rss_kb   = p->rss_kb;
		r->state    = p->state;
		r->cpu_pct  = p->cpu_pct;
		r->mem_pct  = mem_total_kb ? (double)p->rss_kb * 100.0 / (double)mem_total_kb : 0.0;
		r->read_bps  = p->read_bps;
		r->write_bps = p->write_bps;
		r->time_total_jiffies = p->utime + p->stime;
		const char *src = (show_cmdline && p->cmdline[0]) ? p->cmdline : p->comm;
		strncpy(r->name, src, sizeof(r->name) - 1);
		r->p = p;
	}
}

void rows_from_groups(row_t *rows, const group_t *grps, int n,
		      unsigned long mem_total_kb, long hertz)
{
	(void)hertz;
	for (int i = 0; i < n; i++) {
		const group_t *g = &grps[i];
		row_t *r = &rows[i];
		memset(r, 0, sizeof(*r));
		r->kind     = ROW_GROUP;
		r->key_id   = (unsigned long)g->count;
		r->uid      = g->uid_oldest;
		strncpy(r->user, user_name(g->uid_oldest), sizeof(r->user) - 1);
		r->pri      = g->pri_max;
		r->nice     = g->nice_min;
		r->vsize_kb = g->sum_vsize_kb;
		r->rss_kb   = g->sum_rss_kb;
		r->state    = g->state_dominant;
		r->cpu_pct  = g->sum_cpu_pct;
		r->mem_pct  = mem_total_kb ? (double)g->sum_rss_kb * 100.0 / (double)mem_total_kb : 0.0;
		r->read_bps  = g->sum_read_bps;
		r->write_bps = g->sum_write_bps;
		r->time_total_jiffies = g->sum_utime + g->sum_stime;
		strncpy(r->name, g->key, sizeof(r->name) - 1);
		r->g = g;
	}
}

static void emit_line(int line, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	if (g_batch) {
		vfprintf(stdout, fmt, ap);
		fputc('\n', stdout);
	} else {
		term_move(line, 0);
		vfprintf(stdout, fmt, ap);
		term_clrtoeol();
	}
	va_end(ap);
}

static void print_header(const sysstat_t *sys, const opts_t *opts)
{
	time_t t = time(NULL);
	struct tm tm;
	localtime_r(&t, &tm);
	char tbuf[16];
	strftime(tbuf, sizeof(tbuf), "%H:%M:%S", &tm);

	char up[64];
	fmt_uptime(sys->uptime_sec, up, sizeof(up));

	const char *prog = "vtop";

	unsigned long mem_used_kb = 0;
	if (sys->mem_total_kb > sys->mem_free_kb + sys->buffers_kb + sys->cached_kb)
		mem_used_kb = sys->mem_total_kb - sys->mem_free_kb -
			      sys->buffers_kb - sys->cached_kb;
	unsigned long mem_alloc_kb = 0;
	if (sys->mem_total_kb > sys->mem_free_kb)
		mem_alloc_kb = sys->mem_total_kb - sys->mem_free_kb;
	double mem_used = mem_used_kb / 1024.0;
	double swap_used = 0.0;
	if (sys->swap_total_kb > sys->swap_free_kb)
		swap_used = (double)(sys->swap_total_kb - sys->swap_free_kb) / 1024.0;

	double cpu_total_pct = 100.0 - sys->cpu_id;
	if (cpu_total_pct < 0) cpu_total_pct = 0;
	double mem_used_pct  = sys->mem_total_kb
			       ? (double)mem_used_kb  * 100.0 / (double)sys->mem_total_kb : 0;
	double mem_alloc_pct = sys->mem_total_kb
			       ? (double)mem_alloc_kb * 100.0 / (double)sys->mem_total_kb : 0;
	double swap_used_pct = sys->swap_total_kb
			       ? (sys->swap_total_kb - sys->swap_free_kb) * 100.0
				 / (double)sys->swap_total_kb : 0;

	emit_line(0,
		  "%s - %s %s,  load average: %.2f, %.2f, %.2f  [%s]",
		  prog, tbuf, up,
		  sys->loadavg[0], sys->loadavg[1], sys->loadavg[2],
		  opts->group_mode ? "group" : "proc");
	emit_line(1,
		  "Tasks: %4d total, %4d running, %4d sleeping, %4d stopped, %4d zombie",
		  sys->nr_total, sys->nr_running, sys->nr_sleeping,
		  sys->nr_stopped, sys->nr_zombie);
	/* %Cpu line — plain in batch; colorize "Total X.X%" in interactive */
	if (g_batch) {
		printf("%%Cpu(s): %5.1f us, %5.1f sy, %5.1f ni, %5.1f id, %5.1f wa, %5.1f hi, %5.1f si, %5.1f st  | Total %5.1f%%\n",
		       sys->cpu_us, sys->cpu_sy, sys->cpu_ni, sys->cpu_id,
		       sys->cpu_wa, sys->cpu_hi, sys->cpu_si, sys->cpu_st, cpu_total_pct);
		printf("MiB Mem : %9.1f total, %9.1f free, %9.1f used, %9.1f buff/cache | Used %5.1f%% (w/buf %5.1f%%)\n",
		       sys->mem_total_kb / 1024.0, sys->mem_free_kb / 1024.0, mem_used,
		       (sys->buffers_kb + sys->cached_kb) / 1024.0, mem_used_pct, mem_alloc_pct);
		printf("MiB Swap: %9.1f total, %9.1f free, %9.1f used. %9.1f avail Mem | Swap %5.1f%%\n",
		       sys->swap_total_kb / 1024.0, sys->swap_free_kb / 1024.0,
		       swap_used, sys->mem_avail_kb / 1024.0, swap_used_pct);
	} else {
		term_move(2, 0);
		printf("%%Cpu(s): %5.1f us, %5.1f sy, %5.1f ni, %5.1f id, %5.1f wa, %5.1f hi, %5.1f si, %5.1f st  | Total ",
		       sys->cpu_us, sys->cpu_sy, sys->cpu_ni, sys->cpu_id,
		       sys->cpu_wa, sys->cpu_hi, sys->cpu_si, sys->cpu_st);
		int c = pct_color(cpu_total_pct, 70.0, 90.0);
		if (c) term_fg(c);
		term_bold(1);
		printf("%5.1f%%", cpu_total_pct);
		term_bold(0);
		if (c) term_fg(T_FG_DEFAULT);
		term_clrtoeol();

		term_move(3, 0);
		printf("MiB Mem : %9.1f total, %9.1f free, %9.1f used, %9.1f buff/cache | Used ",
		       sys->mem_total_kb / 1024.0, sys->mem_free_kb / 1024.0, mem_used,
		       (sys->buffers_kb + sys->cached_kb) / 1024.0);
		c = pct_color(mem_used_pct, 70.0, 90.0);
		if (c) term_fg(c);
		term_bold(1);
		printf("%5.1f%%", mem_used_pct);
		term_bold(0);
		if (c) term_fg(T_FG_DEFAULT);
		printf(" (w/buf ");
		c = pct_color(mem_alloc_pct, 85.0, 95.0);
		if (c) term_fg(c);
		printf("%5.1f%%", mem_alloc_pct);
		if (c) term_fg(T_FG_DEFAULT);
		printf(")");
		term_clrtoeol();

		term_move(4, 0);
		printf("MiB Swap: %9.1f total, %9.1f free, %9.1f used. %9.1f avail Mem | Swap ",
		       sys->swap_total_kb / 1024.0, sys->swap_free_kb / 1024.0,
		       swap_used, sys->mem_avail_kb / 1024.0);
		c = pct_color(swap_used_pct, 30.0, 70.0);
		if (c) term_fg(c);
		printf("%5.1f%%", swap_used_pct);
		if (c) term_fg(T_FG_DEFAULT);
		term_clrtoeol();
	}
}

static const char *col_hdr_proc  = "    PID USER       PR  NI    VIRT    RES S  %CPU  %MEM DISKR DISKW     TIME+ COMMAND";
static const char *col_hdr_group = "  COUNT USER       PR  NI    VIRT    RES S  %CPU  %MEM DISKR DISKW     TIME+ NAME";

/* Column [start, len) within the formatted row/header strings. */
static const struct { int x, w; } col_pos[SORT_NFIELDS] = {
	[SORT_CPU]    = { 44, 5 },
	[SORT_MEM]    = { 50, 5 },
	[SORT_DREAD]  = { 56, 5 },
	[SORT_DWRITE] = { 62, 5 },
	[SORT_PID]    = {  0, 7 },
	[SORT_TIME]   = { 68, 9 },
	[SORT_NAME]   = { 78, 0 },  /* to end of line */
};

static void print_col_header(int row, const opts_t *opts)
{
	const char *h = opts->group_mode ? col_hdr_group : col_hdr_proc;
	if (g_batch) {
		printf("%s\n", h);
		return;
	}
	int cols = term_cols();
	int hl = (int)strlen(h);
	if (hl > cols) hl = cols;

	int sx = col_pos[opts->sort_field].x;
	int sw = col_pos[opts->sort_field].w;
	if (sw == 0 || sx + sw > hl) sw = hl - sx;
	if (sx < 0) sx = 0;

	term_move(row, 0);
	term_reverse(1);
	for (int i = 0; i < hl; i++) {
		if (i == sx) term_bold(1);
		if (i == sx + sw) term_bold(0);
		fputc(h[i], stdout);
	}
	term_bold(0);
	for (int i = hl; i < cols; i++)
		fputc(' ', stdout);
	term_reverse(0);
}

static void print_row(int line, const row_t *r, long hertz, int focused)
{
	char virt[16], res[16], tplus[16], rrate[8], wrate[8];
	fmt_kb(r->vsize_kb, virt, sizeof(virt));
	fmt_kb((unsigned long)(r->rss_kb < 0 ? 0 : r->rss_kb), res, sizeof(res));
	fmt_time_plus(r->time_total_jiffies, hertz, tplus, sizeof(tplus));
	fmt_rate(r->read_bps,  rrate, sizeof(rrate));
	fmt_rate(r->write_bps, wrate, sizeof(wrate));

	char prbuf[24];
	if (r->pri < 0)
		snprintf(prbuf, sizeof(prbuf), "rt");
	else
		snprintf(prbuf, sizeof(prbuf), "%2ld", r->pri);

	if (g_batch) {
		printf("%7lu %-10.10s %2s %3ld %7s %7s %c %5.1f %5.1f %5s %5s %9s %s\n",
		       r->key_id, r->user, prbuf, r->nice,
		       virt, res, r->state ? r->state : ' ',
		       r->cpu_pct, r->mem_pct, rrate, wrate, tplus, r->name);
		return;
	}

	int cols = term_cols();
	term_move(line, 0);
	if (focused) term_reverse(1);

	int written = printf("%7lu %-10.10s %2s %3ld %7s %7s %c ",
			     r->key_id, r->user, prbuf, r->nice,
			     virt, res, r->state ? r->state : ' ');

	int cpu_color = pct_color(r->cpu_pct, 50.0, 90.0);
	if (cpu_color) term_fg(cpu_color);
	written += printf("%5.1f", r->cpu_pct);
	if (cpu_color) term_fg(T_FG_DEFAULT);
	written += printf(" ");

	int mem_color = pct_color(r->mem_pct, 50.0, 80.0);
	if (mem_color) term_fg(mem_color);
	written += printf("%5.1f", r->mem_pct);
	if (mem_color) term_fg(T_FG_DEFAULT);
	written += printf(" ");

	int rcolor = (r->read_bps  > 1024.0 * 1024.0) ? T_FG_CYAN : 0;
	if (rcolor) term_fg(rcolor);
	written += printf("%5s", rrate);
	if (rcolor) term_fg(T_FG_DEFAULT);
	written += printf(" ");

	int wcolor = (r->write_bps > 1024.0 * 1024.0) ? T_FG_MAGENTA : 0;
	if (wcolor) term_fg(wcolor);
	written += printf("%5s", wrate);
	if (wcolor) term_fg(T_FG_DEFAULT);
	written += printf(" %9s ", tplus);

	int avail = cols - written;
	if (avail < 0) avail = 0;
	printf("%.*s", avail, r->name);

	if (focused) term_reverse(0);
	term_clrtoeol();
}

int display_render(const sysstat_t *sys, const row_t *rows, int nrows,
		   const opts_t *opts, const ui_state_t *ui, const char *status)
{
	(void)status;
	term_begin_frame();
	print_header(sys, opts);

	/* Row 5: PSI + optional drill breadcrumb */
	if (g_batch) {
		if (sys->psi_ok)
			printf("PSI    : cpu %4.1f%%  mem %4.1f%%  io %4.1f%% (some/full avg10)%s%s\n",
			       sys->psi_cpu, sys->psi_mem, sys->psi_io,
			       (ui && ui->in_drill) ? " | drill: " : "",
			       (ui && ui->in_drill) ? ui->drill_key : "");
		else if (ui && ui->in_drill)
			printf("Drilling into group: %s\n", ui->drill_key);
	} else {
		term_move(5, 0);
		if (sys->psi_ok) {
			printf("PSI: cpu ");
			int c = pct_color(sys->psi_cpu, 5.0, 20.0);
			if (c) term_fg(c);
			printf("%4.1f%%", sys->psi_cpu);
			if (c) term_fg(T_FG_DEFAULT);
			printf("  mem ");
			c = pct_color(sys->psi_mem, 5.0, 20.0);
			if (c) term_fg(c);
			printf("%4.1f%%", sys->psi_mem);
			if (c) term_fg(T_FG_DEFAULT);
			printf("  io ");
			c = pct_color(sys->psi_io, 5.0, 20.0);
			if (c) term_fg(c);
			printf("%4.1f%%", sys->psi_io);
			if (c) term_fg(T_FG_DEFAULT);
			printf(" (avg10)");
		}
		if (ui && ui->in_drill) {
			if (sys->psi_ok) printf("  | ");
			term_bold(1);
			term_fg(T_FG_CYAN);
			printf("drill: %s (Esc/b to return)", ui->drill_key);
			term_reset();
		}
		term_clrtoeol();
	}

	int hdr_row = 6;
	print_col_header(hdr_row, opts);

	int start = hdr_row + 1;
	int avail;
	if (g_batch) {
		avail = nrows;
	} else {
		int rows_total = term_rows();
		avail = rows_total - start - (g_message[0] ? 1 : 0);
		if (avail < 0) avail = 0;
	}

	int offset = (ui && !g_batch) ? ui->view_offset : 0;
	if (offset < 0) offset = 0;
	if (offset > nrows - 1) offset = nrows - 1;
	if (offset < 0) offset = 0;

	int show = nrows - offset;
	if (show > avail) show = avail;

	int focus_line = -1;
	if (ui && !g_batch) {
		int f = ui->focus_idx - offset;
		if (f >= 0 && f < show) focus_line = f;
	}

	for (int i = 0; i < show; i++)
		print_row(start + i, &rows[offset + i], sys->hertz, i == focus_line);

	if (!g_batch) {
		int rows_total = term_rows();
		for (int i = show; i < avail; i++) {
			term_move(start + i, 0);
			term_clrtoeol();
		}
		if (g_message[0]) {
			term_move(rows_total - 1, 0);
			int cols = term_cols();
			printf("%.*s", cols - 1, g_message);
			term_clrtoeol();
		}
		term_end_frame();
	}
	return 0;
}

int display_prompt(const char *prompt, char *out, int cap)
{
	int rc = term_read_line(prompt, out, cap);
	return rc;
}
