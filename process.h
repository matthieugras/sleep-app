#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <dirent.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/wait.h>

typedef struct{
    pthread_mutex_t *lk;
    pthread_cond_t *cond;
    volatile int *flag;
    pid_t pid;
} mut_trip_t;

typedef struct{
    pid_t pid;
    uid_t uid;
    int valid;
} process_info_t;

int sa_process_parse_state_file(char *path, char *p_name, size_t len);
int sa_process_string_is_num(char *s);
void sa_process_get_uid(char *p_name, process_info_t *proc);
void *sa_process_monitor(void *userdata);
int sa_process_create_subprocess(mut_trip_t *trip, char *exe, process_info_t *proc, char **argv, char **err);