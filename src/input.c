#include "input.h"
#include "display.h"
#include "term.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <ctype.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <errno.h>

static void cycle_sort(opts_t *o, int dir)
{
	int v = (int)o->sort_field + dir;
	if (v < 0) v = SORT_NFIELDS - 1;
	if (v >= SORT_NFIELDS) v = 0;
	o->sort_field = (enum sort_field)v;
}

static void prompt_kill(opts_t *o, const proc_list_t *procs,
			const group_list_t *groups)
{
	char target[64];
	if (display_prompt("PID/group to signal: ", target, sizeof(target)) < 0)
		return;
	if (target[0] == '\0')
		return;

	char sigbuf[32];
	if (display_prompt("Signal [15]: ", sigbuf, sizeof(sigbuf)) < 0)
		return;
	int sig = SIGTERM;
	if (sigbuf[0])
		sig = atoi(sigbuf);
	if (sig <= 0)
		sig = SIGTERM;

	if (o->group_mode) {
		for (int i = 0; i < groups->count; i++) {
			if (strcmp(groups->items[i].key, target) == 0) {
				int ok = group_kill(&groups->items[i], sig);
				display_message("Signaled %d of %d processes in %s",
						ok, groups->items[i].count, target);
				return;
			}
		}
	}
	int pid = atoi(target);
	if (pid > 0) {
		if (kill(pid, sig) == 0)
			display_message("Sent signal %d to PID %d", sig, pid);
		else
			display_message("kill(%d): %s", pid, strerror(errno));
	} else {
		(void)procs;
		display_message("Unknown target: %s", target);
	}
}

static void prompt_renice(opts_t *o, const proc_list_t *procs,
			  const group_list_t *groups)
{
	char target[64];
	if (display_prompt("PID/group to renice: ", target, sizeof(target)) < 0)
		return;
	if (target[0] == '\0')
		return;

	char nicebuf[16];
	if (display_prompt("Renice value: ", nicebuf, sizeof(nicebuf)) < 0)
		return;
	int nice = atoi(nicebuf);

	if (o->group_mode) {
		for (int i = 0; i < groups->count; i++) {
			if (strcmp(groups->items[i].key, target) == 0) {
				int ok = group_renice(&groups->items[i], nice);
				display_message("Reniced %d of %d processes in %s to %d",
						ok, groups->items[i].count, target, nice);
				return;
			}
		}
	}
	int pid = atoi(target);
	if (pid > 0) {
		if (setpriority(PRIO_PROCESS, pid, nice) == 0)
			display_message("PID %d renice -> %d", pid, nice);
		else
			display_message("renice(%d): %s", pid, strerror(errno));
	} else {
		(void)procs;
		display_message("Unknown target: %s", target);
	}
}

static void prompt_delay(opts_t *o)
{
	char buf[32];
	if (display_prompt("Change delay (s): ", buf, sizeof(buf)) < 0)
		return;
	if (buf[0] == '\0')
		return;
	double v = strtod(buf, NULL);
	if (v > 0.0 && v < 3600.0) {
		o->delay = v;
		display_message("Delay set to %.2fs", v);
	} else {
		display_message("Invalid delay");
	}
}

void input_show_help(void)
{
	term_clear();
	int row = 0;
	term_mvprintf(row++, 0, "vtop %s — Help", VTOP_VERSION);
	row++;
	term_mvprintf(row++, 0, "  q, Q          quit");
	term_mvprintf(row++, 0, "  h, ?          this help");
	term_mvprintf(row++, 0, "  Up / Down     move focused row (auto-scrolls)");
	term_mvprintf(row++, 0, "  PgUp / PgDn   page up/down");
	term_mvprintf(row++, 0, "  Home / End    jump to top/bottom");
	term_mvprintf(row++, 0, "  Enter         drill into focused group (group mode)");
	term_mvprintf(row++, 0, "  Esc, b        back from drill-down");
	term_mvprintf(row++, 0, "  Space         refresh now");
	term_mvprintf(row++, 0, "  d, s          change delay");
	term_mvprintf(row++, 0, "  k             signal a PID or group");
	term_mvprintf(row++, 0, "  r             renice a PID or group");
	term_mvprintf(row++, 0, "  G             toggle aggregation (group mode)");
	term_mvprintf(row++, 0, "  <, >          shift sort column");
	term_mvprintf(row++, 0, "  R             reverse sort");
	term_mvprintf(row++, 0, "  c             toggle COMMAND (cmdline vs comm)");
	term_mvprintf(row++, 0, "  i             toggle idle processes");
	term_mvprintf(row++, 0, "  P / M / N / T sort by CPU / MEM / PID / TIME");
	term_mvprintf(row++, 0, "  /             search by name (substring, case-insensitive)");
	term_mvprintf(row++, 0, "  n             jump to next match");
	row++;
	term_mvprintf(row++, 0, "Press any key to return...");
	term_end_frame();
	while (term_read_key(1000) == TKEY_NONE) {
		/* block until a key arrives */
	}
}

input_result_t input_handle(int ch, opts_t *opts, ui_state_t *ui,
			    const row_t *rows, int nrows,
			    const proc_list_t *procs,
			    const group_list_t *groups)
{
	switch (ch) {
	case TKEY_NONE:
		return IN_NONE;
	case 'q': case 'Q':
		return IN_QUIT;
	case 'h': case '?':
		input_show_help();
		return IN_REDRAW;
	case TKEY_UP:
		if (ui->focus_idx > 0) ui->focus_idx--;
		if (ui->focus_idx < ui->view_offset) ui->view_offset = ui->focus_idx;
		return IN_REDRAW;
	case TKEY_DOWN: {
		if (ui->focus_idx + 1 < nrows) ui->focus_idx++;
		int rh = term_rows() - 8; /* rough visible row count: terminal - header(7) - msg(1) */
		if (rh < 1) rh = 1;
		if (ui->focus_idx >= ui->view_offset + rh)
			ui->view_offset = ui->focus_idx - rh + 1;
		return IN_REDRAW;
	}
	case TKEY_PGUP: {
		int rh = term_rows() - 8;
		if (rh < 1) rh = 1;
		ui->focus_idx -= rh;
		ui->view_offset -= rh;
		if (ui->focus_idx < 0)  ui->focus_idx = 0;
		if (ui->view_offset < 0) ui->view_offset = 0;
		return IN_REDRAW;
	}
	case TKEY_PGDN: {
		int rh = term_rows() - 8;
		if (rh < 1) rh = 1;
		ui->focus_idx += rh;
		ui->view_offset += rh;
		if (ui->focus_idx >= nrows) ui->focus_idx = nrows - 1;
		if (ui->focus_idx < 0)      ui->focus_idx = 0;
		if (ui->view_offset > nrows - 1) ui->view_offset = nrows - 1;
		if (ui->view_offset < 0)         ui->view_offset = 0;
		return IN_REDRAW;
	}
	case TKEY_HOME:
		ui->focus_idx = 0;
		ui->view_offset = 0;
		return IN_REDRAW;
	case TKEY_END:
		ui->focus_idx = nrows > 0 ? nrows - 1 : 0;
		{
			int rh = term_rows() - 8;
			if (rh < 1) rh = 1;
			ui->view_offset = ui->focus_idx - rh + 1;
			if (ui->view_offset < 0) ui->view_offset = 0;
		}
		return IN_REDRAW;
	case TKEY_ENTER:
		/* Drill into the focused group row */
		if (opts->group_mode && !ui->in_drill &&
		    ui->focus_idx >= 0 && ui->focus_idx < nrows &&
		    rows[ui->focus_idx].kind == ROW_GROUP) {
			const char *nm = rows[ui->focus_idx].name;
			size_t k = 0;
			while (k + 1 < sizeof(ui->drill_key) && nm[k])
				ui->drill_key[k] = nm[k], k++;
			ui->drill_key[k] = '\0';
			ui->in_drill = 1;
			ui->focus_idx = 0;
			display_message("Drilled into '%s'", ui->drill_key);
			return IN_REDRAW;
		}
		return IN_REFRESH;
	case TKEY_ESC:
		if (ui->search[0]) {
			ui->search[0] = '\0';
			display_message("Search cleared");
			return IN_REDRAW;
		}
		if (ui->in_drill) {
			ui->in_drill = 0;
			ui->drill_key[0] = '\0';
			ui->focus_idx = 0;
			display_message("Back to group view");
			return IN_REDRAW;
		}
		return IN_NONE;
	case 'b':
		if (ui->in_drill) {
			ui->in_drill = 0;
			ui->drill_key[0] = '\0';
			ui->focus_idx = 0;
			display_message("Back to group view");
			return IN_REDRAW;
		}
		return IN_NONE;
	case '/': {
		char q[64];
		if (display_prompt("Search: ", q, sizeof(q)) == 0) {
			size_t k = 0;
			while (k + 1 < sizeof(ui->search) && q[k])
				ui->search[k] = q[k], k++;
			ui->search[k] = '\0';
			ui->search_dirty = 1;
			if (ui->search[0])
				display_message("Search: %s (n=next, Esc=clear)", ui->search);
		}
		return IN_REDRAW;
	}
	case 'n': {
		if (!ui->search[0])
			return IN_NONE;
		int start = ui->focus_idx + 1;
		for (int i = 0; i < nrows; i++) {
			int idx = (start + i) % nrows;
			if (strcasestr(rows[idx].name, ui->search)) {
				ui->focus_idx = idx;
				return IN_REDRAW;
			}
		}
		display_message("No more matches for '%s'", ui->search);
		return IN_REDRAW;
	}
	case ' ':
		return IN_REFRESH;
	case 'd': case 's':
		prompt_delay(opts);
		return IN_REDRAW;
	case 'k':
		prompt_kill(opts, procs, groups);
		return IN_REDRAW;
	case 'r':
		prompt_renice(opts, procs, groups);
		return IN_REDRAW;
	case 'G':
		opts->group_mode = !opts->group_mode;
		display_message("Group mode %s", opts->group_mode ? "ON" : "OFF");
		return IN_REDRAW;
	case '<':
		cycle_sort(opts, -1);
		return IN_REDRAW;
	case '>':
		cycle_sort(opts, +1);
		return IN_REDRAW;
	case 'R':
		opts->sort_reverse = !opts->sort_reverse;
		return IN_REDRAW;
	case 'c':
		opts->show_cmdline = !opts->show_cmdline;
		return IN_REDRAW;
	case 'i':
		opts->show_idle = !opts->show_idle;
		return IN_REDRAW;
	case 'P':
		opts->sort_field = SORT_CPU;
		return IN_REDRAW;
	case 'M':
		opts->sort_field = SORT_MEM;
		return IN_REDRAW;
	case 'N':
		opts->sort_field = SORT_PID;
		return IN_REDRAW;
	case 'T':
		opts->sort_field = SORT_TIME;
		return IN_REDRAW;
	default:
		return IN_NONE;
	}
}
