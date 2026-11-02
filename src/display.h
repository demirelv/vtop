#ifndef __VTOP_DISPLAY_H__
#define __VTOP_DISPLAY_H__

#include "vtop.h"

int  display_init(int batch);
void display_shutdown(void);
void display_resize(void);

int  display_render(const sysstat_t *sys, const row_t *rows, int nrows,
		    const opts_t *opts, const ui_state_t *ui,
		    const char *status);

int  display_prompt(const char *prompt, char *out, int cap);
void display_message(const char *fmt, ...);

void rows_from_procs(row_t *rows, const process_t *procs, int n,
		     unsigned long mem_total_kb, long hertz,
		     int show_cmdline);
void rows_from_groups(row_t *rows, const group_t *grps, int n,
		      unsigned long mem_total_kb, long hertz);

#endif
