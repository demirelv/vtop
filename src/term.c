#include "term.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <poll.h>
#include <errno.h>

static int   g_batch = 1;
static int   g_active = 0;
static int   g_rows = 24, g_cols = 80;
static struct termios g_saved_tio;

static void update_size(void)
{
	struct winsize ws;
	if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_row > 0) {
		g_rows = ws.ws_row;
		g_cols = ws.ws_col;
	}
}

int term_init(int batch)
{
	g_batch = batch;
	if (batch) {
		update_size();
		return 0;
	}
	if (!isatty(STDOUT_FILENO) || !isatty(STDIN_FILENO))
		return -1;

	if (tcgetattr(STDIN_FILENO, &g_saved_tio) < 0)
		return -1;

	struct termios raw = g_saved_tio;
	raw.c_lflag &= ~(ECHO | ICANON | ISIG | IEXTEN);
	raw.c_iflag &= ~(IXON | ICRNL | BRKINT | INPCK | ISTRIP);
	raw.c_oflag &= ~(OPOST);
	raw.c_cc[VMIN]  = 0;
	raw.c_cc[VTIME] = 0;
	if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) < 0)
		return -1;

	update_size();
	fputs("\033[?25l", stdout); /* hide cursor */
	fputs("\033[2J\033[H", stdout); /* clear */
	fflush(stdout);
	g_active = 1;
	return 0;
}

void term_shutdown(void)
{
	if (!g_active)
		return;
	fputs("\033[0m\033[?25h\033[2J\033[H", stdout);
	fflush(stdout);
	tcsetattr(STDIN_FILENO, TCSANOW, &g_saved_tio);
	g_active = 0;
}

void term_size(int *rows, int *cols)
{
	update_size();
	if (rows) *rows = g_rows;
	if (cols) *cols = g_cols;
}

int term_rows(void) { update_size(); return g_rows; }
int term_cols(void) { update_size(); return g_cols; }

void term_begin_frame(void)
{
	if (g_batch)
		return;
	fputs("\033[H", stdout); /* home */
}

void term_end_frame(void)
{
	if (g_batch)
		return;
	fflush(stdout);
}

void term_move(int row, int col)
{
	if (g_batch)
		return;
	printf("\033[%d;%dH", row + 1, col + 1);
}

void term_clrtoeol(void)
{
	if (g_batch)
		return;
	fputs("\033[K", stdout);
}

void term_clear(void)
{
	if (g_batch)
		return;
	fputs("\033[2J\033[H", stdout);
}

void term_write(const char *s)
{
	fputs(s, stdout);
}

void term_printf(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vfprintf(stdout, fmt, ap);
	va_end(ap);
}

void term_mvprintf(int row, int col, const char *fmt, ...)
{
	if (!g_batch)
		printf("\033[%d;%dH", row + 1, col + 1);
	va_list ap;
	va_start(ap, fmt);
	vfprintf(stdout, fmt, ap);
	va_end(ap);
}

void term_reverse(int on)
{
	if (g_batch)
		return;
	fputs(on ? "\033[7m" : "\033[27m", stdout);
}

void term_bold(int on)
{
	if (g_batch)
		return;
	fputs(on ? "\033[1m" : "\033[22m", stdout);
}

void term_fg(int code)
{
	if (g_batch)
		return;
	printf("\033[%dm", code);
}

void term_reset(void)
{
	if (g_batch)
		return;
	fputs("\033[0m", stdout);
}

void term_cursor(int show)
{
	if (g_batch)
		return;
	fputs(show ? "\033[?25h" : "\033[?25l", stdout);
	fflush(stdout);
}

int term_read_key(int timeout_ms)
{
	if (g_batch)
		return TKEY_NONE;

	struct pollfd pfd = { .fd = STDIN_FILENO, .events = POLLIN };
	if (timeout_ms < 0)
		timeout_ms = 0;
	int pr = poll(&pfd, 1, timeout_ms);
	if (pr <= 0)
		return TKEY_NONE;

	unsigned char c;
	ssize_t n = read(STDIN_FILENO, &c, 1);
	if (n != 1)
		return TKEY_NONE;

	if (c == TKEY_ESC) {
		unsigned char b;
		struct pollfd p2 = { .fd = STDIN_FILENO, .events = POLLIN };
		if (poll(&p2, 1, 30) <= 0)
			return TKEY_ESC;
		if (read(STDIN_FILENO, &b, 1) != 1)
			return TKEY_ESC;

		if (b == 'O') {
			if (poll(&p2, 1, 30) <= 0)
				return TKEY_ESC;
			if (read(STDIN_FILENO, &b, 1) != 1)
				return TKEY_ESC;
			switch (b) {
			case 'H': return TKEY_HOME;
			case 'F': return TKEY_END;
			default:  return TKEY_ESC;
			}
		}
		if (b != '[')
			return TKEY_ESC;

		/* CSI: read until final byte (0x40-0x7E) or buffer full */
		char seq[16];
		int sn = 0;
		while (sn < (int)sizeof(seq) - 1) {
			if (poll(&p2, 1, 30) <= 0)
				break;
			if (read(STDIN_FILENO, &b, 1) != 1)
				break;
			seq[sn++] = (char)b;
			if (b >= 0x40 && b <= 0x7E)
				break;
		}
		seq[sn] = '\0';

		if (sn == 1) {
			switch (seq[0]) {
			case 'A': return TKEY_UP;
			case 'B': return TKEY_DOWN;
			case 'C': return TKEY_RIGHT;
			case 'D': return TKEY_LEFT;
			case 'H': return TKEY_HOME;
			case 'F': return TKEY_END;
			default:  return TKEY_ESC;
			}
		}
		if (sn >= 2 && seq[sn - 1] == '~') {
			int num = atoi(seq);
			switch (num) {
			case 1: case 7: return TKEY_HOME;
			case 4: case 8: return TKEY_END;
			case 5: return TKEY_PGUP;
			case 6: return TKEY_PGDN;
			default: return TKEY_ESC;
			}
		}
		return TKEY_ESC;
	}
	if (c == '\r')
		return TKEY_ENTER;
	return (int)c;
}

int term_read_line(const char *prompt, char *out, int cap)
{
	if (g_batch || !g_active) {
		if (cap > 0) out[0] = '\0';
		return -1;
	}
	update_size();
	int row = g_rows - 1;

	/* Switch to cooked mode temporarily. */
	struct termios cur;
	tcgetattr(STDIN_FILENO, &cur);
	tcsetattr(STDIN_FILENO, TCSANOW, &g_saved_tio);

	fputs("\033[?25h", stdout);
	printf("\033[%d;1H\033[K%s", row + 1, prompt);
	fflush(stdout);

	int rc = -1;
	if (fgets(out, cap, stdin)) {
		size_t len = strlen(out);
		while (len > 0 && (out[len - 1] == '\n' || out[len - 1] == '\r'))
			out[--len] = '\0';
		rc = 0;
	} else {
		if (cap > 0) out[0] = '\0';
	}

	tcsetattr(STDIN_FILENO, TCSANOW, &cur);
	fputs("\033[?25l", stdout);
	printf("\033[%d;1H\033[K", row + 1);
	fflush(stdout);

	return rc;
}
