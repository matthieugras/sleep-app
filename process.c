#define _GNU_SOURCE
#include "process.h"


int sa_process_parse_state_file(char *path, char *p_name, size_t len){
    int ret_code, uid;
    char tmp_rd_buf[60];
    FILE *fp;

    fp = fopen(path, "r");
    if(!fp){
        fprintf(stderr, "Error fopen() (1) for file %s: %s\n", path, strerror(errno));
        return -1;
    }
    
    ret_code = fscanf(fp, "%*s\t%59s\n", tmp_rd_buf);
    if(ret_code != 1){
        fprintf(stderr, "Error fscanf for file %s: %s\n", path, strerror(errno));
        goto fail_parse_file;
    }

    if(strncmp(p_name, tmp_rd_buf, len) != 0)
        goto fail_parse_file;

    for(int i = 0; i < 7; ++i){
        ret_code = fscanf(fp, "%*[^\n]\n");
        if(ret_code == -1){
            fprintf(stderr, "Error fscanf() for file %s: %s\n", path, strerror(errno));
            goto fail_parse_file;
        }
    }

    ret_code = fscanf(fp, "%*s\t%d\t%*d\t%*d\t%*d\n", &uid);
    if(ret_code != 1){
        fprintf(stderr, "Error fscanf() for file %s: %s\n", path, strerror(errno));
        goto fail_parse_file;
    }
    fclose(fp);
    return uid;

    fail_parse_file:
    fclose(fp);
    return -1;
}

int sa_process_string_is_num(char *s){
    while(*s != '\0'){
        if(!((*s) >= '0' && (*s) <= '9')){
            return 0;
        }
        ++s;
    }
    return 1;
}

void sa_process_get_uid(char *p_name, process_info_t *proc){
    DIR *d;
    char tmp_path_init[290] = "/proc/";
    void *tmp_path = &tmp_path_init;
    struct dirent *dent;
    size_t len;
    int uid;

    if((len = strlen(p_name)) > 59){
        fprintf(stderr, "Error sa_process_get_uid(): Process name too long\n");
        proc->valid = 0;
        return;
    }

    d = opendir("/proc");
    if(!d){
        fprintf(stderr, "Error opendir(): %s\n", strerror(errno));
        proc->valid = 0;
        return;
    }

    errno = 0;
    while((dent = readdir(d))){
        if((dent->d_type == DT_DIR) && sa_process_string_is_num(dent->d_name)){
            memset((void *) (tmp_path + 6), '\0', 290);
            strcat(tmp_path, (char *) &dent->d_name);
            strcat(tmp_path, "/status");
            if((uid = sa_process_parse_state_file(tmp_path, p_name, len)) != -1){
                proc->valid = 1;
                proc->uid = uid;
                proc->pid = atoi(dent->d_name);
                fprintf(stdout, "sa_process_get_uid(): Did find process with uid=%u, pid=%d\n", proc->uid, proc->pid);
                fflush(stdout);
                closedir(d);
                return;
            }
        }

        errno = 0;
    }

    if(errno){
        fprintf(stderr, "Error readdir(): %s\n", strerror(errno));
    }
    closedir(d);
    fprintf(stdout, "sa_process_get_uid(): Didn't find process\n");
    fflush(stdout);
    proc->valid = 0;
}

void *sa_process_monitor(void *userdata){
    int child_ret;
    mut_trip_t *trip = (mut_trip_t *) userdata;
    waitpid(trip->pid, &child_ret, 0);

    //Lock mutex and signal the condition
    pthread_mutex_lock(trip->lk);
    *(trip->flag) = 1;
    pthread_cond_broadcast(trip->cond);
    pthread_mutex_unlock(trip->lk);
    return NULL;
}

void print_dings(char **envp){
    int i = 0;
    char *s;
    while((s = envp[i]) != NULL){
        printf("%s\n", s);
        ++i;
    }
}

char** sa_process_fake_env(process_info_t *proc){
    FILE *fp;
    size_t last_pos = 0, curr_pos = 0, no_strings = 0;
    char **envp, file_path[100], c;
    
    sprintf(file_path, "/proc/%u/environ", proc->pid);

    fp = fopen(file_path, "rb");
    if(!fp){
        fprintf(stderr, "Error fopen() (2) environ: %s\n", strerror(errno));
        //No exit, bc the process could have gone offline
        return NULL;
    }

    while((c = fgetc(fp)) != -1)
        if(c == '\0')
            ++no_strings;

    envp = malloc((no_strings + 1) * sizeof(char *));

    for(size_t i = 0; i < no_strings; ++i){
        fseek(fp, last_pos, SEEK_SET);
        curr_pos = last_pos;
        while((c = fgetc(fp)) != '\0')
            ++curr_pos;

        size_t str_size = curr_pos - last_pos;
        envp[i] = malloc(str_size + 1);
        
        fseek(fp, last_pos, SEEK_SET);
        for(size_t j = 0; j < str_size; ++j)
            envp[i][j] = fgetc(fp);

        envp[i][str_size] = '\0';

        last_pos = curr_pos + 1;

    }
    envp[no_strings] = NULL;
    fclose(fp);

    return envp;
}

static void free_envp(char **envp){
    size_t i = 0;
    char *tmp_rm;
    while((tmp_rm = envp[i]) != NULL){
        free(tmp_rm);
        ++i;
    }
    free(envp);
}

int sa_process_create_subprocess(mut_trip_t *trip, char *exe, process_info_t *proc, char **argv, char **err){
    err[0] = NULL;
    int ret_code;
    pid_t pid = fork();
    if(pid == -1){
        asprintf(err, "Error fork(): %s\n", strerror(errno));
        return 1;
    }
    
    //Child process
    if(pid == 0){
        char** envp = sa_process_fake_env(proc);
        //print_dings(envp);

        if(envp == NULL){
            //Process changed pid
            asprintf(err, "Exiting because envp is NULL");
            fflush(stdout);
            return 1;
        }

        if(setregid(proc->uid, proc->uid) != 0){
            asprintf(err, "Error setregid(): %s\n", strerror(errno));
            free_envp(envp);
        }
        if(setreuid(proc->uid, proc->uid) != 0){
            asprintf(err, "Error setreuid(): %s\n", strerror(errno));
            free_envp(envp);
        }
        execve(exe, argv, envp);

        //Call should not return
        asprintf(err, "Error execv(): %s\n", strerror(errno));
        free_envp(envp);
        return 1;
    }

    /*
        Supervisor continues
    */

    //Start new thread that send a signal when the spawned process dies
    fprintf(stdout, "Forked process with pid %d\n", pid);
    trip->pid = pid;
    pthread_t monitor_thread;
    ret_code = pthread_create(&monitor_thread, NULL, sa_process_monitor, trip);
    if(ret_code != 0){
        asprintf(err, "Error pthread_create(): %s\n", strerror(ret_code));
        return 1;
    }
    ret_code = pthread_detach(monitor_thread);
    if(ret_code != 0){
        asprintf(err, "Error pthread_detach(): %s\n", strerror(ret_code));
        return 1;
    }
    return 0;
}

//Individual debugging code
/*int main(){
    process_info_t proc;
    sa_process_get_uid("firefox", &proc);
    if(!proc.valid){
        printf("nö\n");
        return 1;
    }
    fprintf(stdout, "%u %u %d\n", proc.pid, proc.uid, proc.valid);

    pthread_mutex_t mut;
    pthread_cond_t cond;
    volatile int flag = 0;

    pthread_mutex_init(&mut, NULL);
    pthread_cond_init(&cond, NULL);

    mut_trip_t trip;
    trip.cond = &cond;
    trip.lk = &mut;
    trip.flag = &flag;
    pthread_mutex_lock(&mut);
    sa_process_create_subprocess(&trip, "/usr/bin/mousepad", &proc);
    pthread_cond_wait(&cond, &mut);
    pthread_mutex_unlock(&mut);
    printf("released lol\n");
    pthread_mutex_destroy(&mut);
    pthread_cond_destroy(&cond);

    return 0;
}*/