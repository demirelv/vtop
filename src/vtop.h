#ifndef __VTOP_H__
#define __VTOP_H__

#include <sys/types.h>
#include <stddef.h>
#include <time.h>

#define VTOP_VERSION "0.1.0"

#define MAX_NAME       64
#define MAX_CMDLINE    256
#define MAX_USERNAME   32
#define MAX_PID_FILTER 64

enum sort_field {
	SORT_CPU = 0,
	SORT_MEM,
	SORT_DREAD,
	SORT_DWRITE,
	SORT_PID,
	SORT_TIME,
	SORT_NAME,
	SORT_NFIELDS
};

extern const char *sort_field_label[SORT_NFIELDS];

typedef struct process {
	pid_t pid;
	pid_t ppid;
	uid_t uid;
	char  comm[20];
	char  exe_base[MAX_NAME];
	char  cmdline[MAX_CMDLINE];
	char  state;
	long  pri;
	long  nice;
	int   num_threads;
	unsigned long utime;
	unsigned long stime;
	unsigned long starttime;
	unsigned long vsize_kb;
	long  rss_kb;
	double cpu_pct;
	int    io_valid;
	unsigned long long read_bytes;
	unsigned long long write_bytes;
	double read_bps;
	double write_bps;
} process_t;

typedef struct group {
	char  key[MAX_NAME];
	int   count;
	uid_t uid_oldest;
	unsigned long starttime_oldest;
	unsigned long sum_utime;
	unsigned long sum_stime;
	unsigned long sum_vsize_kb;
	long  sum_rss_kb;
	int   thread_count;
	double sum_cpu_pct;
	double sum_read_bps;
	double sum_write_bps;
	char  state_dominant;
	long  pri_max;
	long  nice_min;
	pid_t *pids;
	int   pids_cap;
} group_t;

#define CPU_NCAT 10  /* user, nice, system, idle, iowait, irq, softirq, steal, guest, guest_nice */

typedef struct sysstat {
	unsigned long cat[CPU_NCAT];
	unsigned long prev_cat[CPU_NCAT];
	unsigned long total_jiffies;
	unsigned long idle_jiffies;
	unsigned long prev_total;
	unsigned long prev_idle;
	int  have_prev;
	double cpu_us, cpu_sy, cpu_ni, cpu_id, cpu_wa, cpu_hi, cpu_si, cpu_st;
	unsigned long mem_total_kb;
	unsigned long mem_free_kb;
	unsigned long mem_avail_kb;
	unsigned long buffers_kb;
	unsigned long cached_kb;
	unsigned long swap_total_kb;
	unsigned long swap_free_kb;
	double loadavg[3];
	double psi_cpu;     /* some avg10, percent (0..100) */
	double psi_mem;     /* full avg10 */
	double psi_io;      /* full avg10 */
	int    psi_ok;      /* 1 if /proc/pressure is available */
	unsigned long uptime_sec;
	double elapsed_sec;     /* wall-clock seconds since previous sample */
	struct timespec ts;     /* monotonic timestamp of latest sample */
	int   have_ts;
	int   ncpu;
	long  hertz;
	long  page_kb;
	int   nr_total;
	int   nr_running;
	int   nr_sleeping;
	int   nr_stopped;
	int   nr_zombie;
} sysstat_t;

typedef enum { ROW_PROC, ROW_GROUP } row_kind_t;

typedef struct row {
	row_kind_t kind;
	unsigned long key_id;
	uid_t uid;
	char  user[MAX_USERNAME];
	long  pri;
	long  nice;
	unsigned long vsize_kb;
	long  rss_kb;
	char  state;
	double cpu_pct;
	double mem_pct;
	double read_bps;
	double write_bps;
	unsigned long time_total_jiffies;
	char  name[MAX_CMDLINE];
	const process_t *p;
	const group_t   *g;
} row_t;

typedef struct ui_state {
	int  focus_idx;
	int  view_offset;
	int  in_drill;
	char drill_key[MAX_NAME];
	char search[64];
	int  search_dirty;       /* set when search just changed; reposition focus on next render */
} ui_state_t;

typedef struct opts {
	int  batch;
	int  iterations;
	double delay;
	int  group_mode;
	int  thread_mode;
	int  show_cmdline;
	int  show_idle;
	enum sort_field sort_field;
	int  sort_reverse;
	int  width;
	int  uid_filter;
	pid_t pid_filter[MAX_PID_FILTER];
	int  pid_filter_n;
	int  show_help;
	int  show_version;
} opts_t;

#endif /* __VTOP_H__ */
