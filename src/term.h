#ifndef __VTOP_TERM_H__
#define __VTOP_TERM_H__

#include <stdarg.h>

#define TKEY_NONE  (-1)
#define TKEY_ENTER 10
#define TKEY_ESC   27
#define TKEY_LEFT  1001
#define TKEY_RIGHT 1002
#define TKEY_UP    1003
#define TKEY_DOWN  1004
#define TKEY_HOME  1005
#define TKEY_END   1006
#define TKEY_PGUP  1007
#define TKEY_PGDN  1008

int  term_init(int batch);
void term_shutdown(void);
void term_size(int *rows, int *cols);
int  term_rows(void);
int  term_cols(void);

void term_begin_frame(void);
void term_end_frame(void);

void term_move(int row, int col);
void term_clrtoeol(void);
void term_clear(void);
void term_write(const char *s);
void term_printf(const char *fmt, ...);
void term_mvprintf(int row, int col, const char *fmt, ...);
void term_reverse(int on);
void term_bold(int on);
void term_cursor(int show);

/* SGR foreground colors. Pass 0 to reset everything (incl. bold/reverse). */
#define T_FG_DEFAULT 39
#define T_FG_RED     31
#define T_FG_GREEN   32
#define T_FG_YELLOW  33
#define T_FG_BLUE    34
#define T_FG_MAGENTA 35
#define T_FG_CYAN    36
void term_fg(int code);
void term_reset(void);

/* Wait up to timeout_ms; return key code or TKEY_NONE. Negative timeout = no wait. */
int  term_read_key(int timeout_ms);

/* Echo a prompt at the bottom line and read a line (canonical). Returns 0 on success. */
int  term_read_line(const char *prompt, char *out, int cap);

#endif
