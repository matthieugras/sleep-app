#include <unistd.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <pthread.h>
#include <arpa/inet.h>

#define PLAYBACK_SOCK_NAME "/tmp/sa_playback.socket"
#define PLAYBACK_BUF_SIZE 1
#define CONTROL_SOCK_NAME "/tmp/sa_control.socket"
#define CONTROL_BUF_SIZE 100

int sa_util_w_play_sock(char cmd);
int sa_main_mem_is_pat(void *ptr, size_t n, char c);
void sa_util_print_info(pthread_mutex_t *stdout_lk, const char*  __format, ...);
void sa_util_print_err(pthread_mutex_t *stdout_lk, const char*  __format, ...);