#ifndef __VTOP_INPUT_H__
#define __VTOP_INPUT_H__

#include "vtop.h"
#include "proc.h"
#include "group.h"

typedef enum {
	IN_NONE = 0,
	IN_QUIT,
	IN_REFRESH,
	IN_REDRAW,
} input_result_t;

input_result_t input_handle(int ch, opts_t *opts, ui_state_t *ui,
			    const row_t *rows, int nrows,
			    const proc_list_t *procs,
			    const group_list_t *groups);

void input_show_help(void);

#endif
