#include <unistd.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <X11/Xlib.h>
#include <X11/extensions/scrnsaver.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <arpa/inet.h>
#include <pthread.h>
#include "sleep.h"
#include "process.h"
#include "sa_util.h"

typedef struct{
	volatile int act_flag, req_stop_flag;
	volatile int tm_hour, tm_min, sleep_time;
	volatile long double usage_cutoff;
	volatile int kill_flag;
	char fname[CONTROL_BUF_SIZE - 1];
	pthread_t main_loop;
	pthread_t control_listen;
	mut_trip_t mut_trip;
	pthread_mutex_t stderr_lk;
	pthread_mutex_t stdout_lk;
} sa_main_shared_context_t;

typedef struct{
	int err_code;
	char *err_str;
} sa_main_thread_ret_val_t;


typedef struct mainloop{
	sa_main_shared_context_t *shared_cntx;
	sa_main_thread_ret_val_t thread_err;
} sa_main_mainloop_context_t;

typedef struct control_listen{
	sa_main_shared_context_t *shared_cntx;
	sa_main_thread_ret_val_t thread_err;
	char control_buf[CONTROL_BUF_SIZE];
} sa_main_control_listen_context_t;

int sa_main_w_play_sock(sa_main_mainloop_context_t *mainloop_cntx, char cmd);