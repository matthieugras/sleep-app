#define _GNU_SOURCE
#include "main.h"

static void sa_main_set_err(sa_main_thread_ret_val_t *thread_cntx, char *err1, char *err2, pthread_t other){
	int ret_code;
	thread_cntx->err_code = EXIT_FAILURE;
	if(thread_cntx->err_str != NULL)
		free(thread_cntx->err_str);

	ret_code = asprintf(&thread_cntx->err_str, "Error %s: %s", err1, err2);
	if(ret_code == -1){
		char err_msg[] = "Error: Error occured, but could not create error message";
		thread_cntx->err_str = malloc(sizeof(err_msg));
		memcpy(thread_cntx->err_str, err_msg, sizeof(err_msg));
	}
	fprintf(stdout, "Canceling thread %lu\n", other);
	pthread_cancel(other);
}

/*void sa_main_cleanup_mainloop(void *arg){
	//sa_util_print_info("Cancel callback of mainloop thread called");
}

void sa_main_cleanup_control_listen(void *arg){
	//sa_util_print_info("Cancel callback of control_listen thread called");
}*/

int sa_main_w_play_sock(sa_main_mainloop_context_t *mainloop_cntx, char cmd){
	struct sockaddr_un remote_sock_addr;
	int fd_conn, fd_data, ret_code;
	char buff = cmd;
	sa_main_shared_context_t *shared_cntx = mainloop_cntx->shared_cntx;
	memset(&remote_sock_addr, 0, sizeof(struct sockaddr_un));

	fd_conn = socket(AF_UNIX, SOCK_STREAM, 0);
	if(fd_conn < 0){
		sa_main_set_err(&mainloop_cntx->thread_err, "socket()", strerror(errno), shared_cntx->control_listen);
		return -1;
	}

	remote_sock_addr.sun_family = AF_UNIX;
	strncpy(remote_sock_addr.sun_path, PLAYBACK_SOCK_NAME, sizeof(remote_sock_addr.sun_path)-1);

	fd_data = connect(fd_conn, (struct sockaddr*) &remote_sock_addr,
		strlen(remote_sock_addr.sun_path) + sizeof(remote_sock_addr.sun_family)
	);

	if(fd_data < 0){
		//Signal the connection failed
		ret_code = close(fd_conn);
		if(ret_code == -1){
			sa_main_set_err(&mainloop_cntx->thread_err, "close()", strerror(errno), shared_cntx->control_listen);
			return -1;
		}

		return 0;
	}
	ret_code = send(fd_conn, &buff, PLAYBACK_BUF_SIZE, 0); 
	if(ret_code < 0){
		sa_main_set_err(&mainloop_cntx->thread_err, "send()", strerror(errno), shared_cntx->control_listen);
		return -1;
	}
	ret_code = recv(fd_conn, &buff, PLAYBACK_BUF_SIZE, 0);
	if(ret_code < 0){
		sa_main_set_err(&mainloop_cntx->thread_err, "recv()", strerror(errno), shared_cntx->control_listen);
		return -1;
	}
	if(buff != 'a'){
		sa_main_set_err(&mainloop_cntx->thread_err, "wr_to_playback_sock()", "No ACK from sever", shared_cntx->control_listen);
		return -1;
	}
	sa_util_print_info(&shared_cntx->stdout_lk, "wr_to_playback_sock(): Received ACK\n");
	
	ret_code = close(fd_conn);
	if(ret_code == -1){
		sa_main_set_err(&mainloop_cntx->thread_err, "close()", strerror(errno), shared_cntx->control_listen);
		return -1;
	}
	return 1;
}

int sa_main_decode_control_buf(sa_main_control_listen_context_t *control_cntx){
	sa_main_shared_context_t *shared_cntx = control_cntx->shared_cntx;
	int ret_code;
	int tmp_sleep_time;
	int h_tmp, min_tmp;
	char tmp_fname[CONTROL_BUF_SIZE - 1];
	FILE *test_exists;

	/*
		(00)hhhhmmmm(00)..(00) SET TIME
		(01)(00)..(00) Activate alarm
		(02)(00)..(00) Deactivate alarm
		(03)ssss(00)..(00) Set intervals
		(04)(00)..(00) Disarm the alarm
		(05)s..s(XX)..(XX) set playback filename
		(06)(00)..(00) Kill - For mem leak testing
		(FF)(FF)..(FF) EOF
	*/
	if(sa_main_mem_is_pat((void *) control_cntx->control_buf, CONTROL_BUF_SIZE, 0xFF))
		return -1;

	ret_code = pthread_mutex_lock(shared_cntx->mut_trip.lk);
	if(ret_code != 0){
		sa_main_set_err(&control_cntx->thread_err, "pthread_mutex_lock()", strerror(ret_code), shared_cntx->main_loop);
		return 0;
	}

	switch(control_cntx->control_buf[0]){
		case 0:
			memcpy(&h_tmp, (void *) (control_cntx->control_buf + 1), sizeof(int));
			memcpy(&min_tmp, (void *) (control_cntx->control_buf + 5), sizeof(int));
			if(h_tmp > 23 || h_tmp < 0){
				sa_main_set_err(&control_cntx->thread_err, "sa_main_decode_control_buf()", "Hour is not valid", shared_cntx->main_loop);
				goto failure;
			}

			if(min_tmp < 0 || min_tmp > 59){
				sa_main_set_err(&control_cntx->thread_err, "sa_main_decode_control_buf()", "Minute is not valid.", shared_cntx->main_loop);
				goto failure;
			}

			if(!sa_main_mem_is_pat((void *) (control_cntx->control_buf + 9), CONTROL_BUF_SIZE - 9, 0)){
				sa_main_set_err(&control_cntx->thread_err, "sa_main_decode_control_buf():", "Expected buffer to be terminated by zeroes.", shared_cntx->main_loop);
				goto failure;
			}

			shared_cntx->tm_hour = h_tmp;
			shared_cntx->tm_min = min_tmp;

			sa_util_print_info(&shared_cntx->stdout_lk, "sa_main_decode_control_buf(): Set time for alarm\n");
			goto success;
			
		case 1:
			if(!sa_main_mem_is_pat((void *) (control_cntx->control_buf + 1), CONTROL_BUF_SIZE - 1, 0)){
				sa_main_set_err(&control_cntx->thread_err, "sa_main_decode_control_buf()", "Expected buffer to be terminated by zeroes.", shared_cntx->main_loop);
				goto failure;
			}
			sa_util_print_info(&shared_cntx->stdout_lk, "sa_main_decode_control_buf(): Activated alarm\n");
			shared_cntx->act_flag = 1;
			goto success;

		case 2:
			if(!sa_main_mem_is_pat((void *) (control_cntx->control_buf + 1), CONTROL_BUF_SIZE - 1, 0)){
				sa_main_set_err(&control_cntx->thread_err, "sa_main_decode_control_buf()", "Expected buffer to be terminated by zeroes.", shared_cntx->main_loop);
				goto failure;
			}
			
			shared_cntx->act_flag = 0;
			sa_util_print_info(&shared_cntx->stdout_lk, "sa_main_decode_control_buf(): Deactivated alarm\n");
			goto success;
		
		case 3:
			memcpy((void *) &tmp_sleep_time, (void *) (control_cntx->control_buf + 1), sizeof(int));
			if(tmp_sleep_time < 0){
				sa_main_set_err(&control_cntx->thread_err, "sa_main_decode_control_buf()", "Sleep time should not be negative.", shared_cntx->main_loop);
				goto failure;
			}
			
			if(!sa_main_mem_is_pat((void *) (control_cntx->control_buf + 1 + sizeof(int)), CONTROL_BUF_SIZE - 5, 0x00)){
				sa_main_set_err(&control_cntx->thread_err, "sa_main_decode_control_buf()", "Expected buffer to be terminated by zeroes", shared_cntx->main_loop);
				goto failure;
			}
			shared_cntx->sleep_time = tmp_sleep_time;
			goto success;
		
		case 4:
			if(!sa_main_mem_is_pat((void *)(control_cntx->control_buf + 1), CONTROL_BUF_SIZE - 1, 0x00)){
				sa_main_set_err(&control_cntx->thread_err, "sa_main_decode_control_buf()", "Expected buffer to be terminated by zeroes", shared_cntx->main_loop);
				goto failure;
			}
			shared_cntx->req_stop_flag = 1;
			ret_code = pthread_cond_broadcast(shared_cntx->mut_trip.cond);
			if(ret_code != 0){
				sa_main_set_err(&control_cntx->thread_err, "pthread_cond_broadcast()", strerror(ret_code), shared_cntx->main_loop);
				goto failure;
			}
			goto success;

		case 5:
			strncpy(tmp_fname, (void *) (control_cntx->control_buf + 1), CONTROL_BUF_SIZE - 1);
			tmp_fname[CONTROL_BUF_SIZE - 2] = '\0';
			test_exists = fopen(tmp_fname, "r");
			if(test_exists){
				fclose(test_exists);
				memcpy((void *) shared_cntx->fname, tmp_fname, CONTROL_BUF_SIZE - 1);
			} else {
				sa_main_set_err(&control_cntx->thread_err, "fopen()", "Could not open file", shared_cntx->main_loop);
				goto failure;
			}

			memset(tmp_fname, 0, CONTROL_BUF_SIZE - 1);
			sa_util_print_info(&shared_cntx->stdout_lk, "sa_main_decode_control_buf(): Setting filename to %s\n", shared_cntx->fname);
			goto success;

		case 6:
			sa_main_set_err(&control_cntx->thread_err, "sa_main_decode_control_buf()", "User requested killing", shared_cntx->main_loop);
			goto failure;

		default:
			sa_main_set_err(&control_cntx->thread_err, "sa_main_decode_control_buf", "Unknown command", shared_cntx->main_loop);
			goto failure;
	}

	//Not handling return values of unlock because it could mask the actual problem
	failure:
		pthread_mutex_unlock(shared_cntx->mut_trip.lk);
		return 0;
	success:
		pthread_mutex_unlock(shared_cntx->mut_trip.lk);
		return 1;
}


void *sa_main_listen_on_control(void *arg){
	pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);
	//pthread_cleanup_push(sa_main_cleanup_control_listen, arg);

	sa_main_control_listen_context_t *control_cntx = arg;
	sa_main_shared_context_t *shared_cntx = control_cntx->shared_cntx;
	sa_main_thread_ret_val_t *ret_val;
    int fd_conn, fd_data, ret_code, nbytes;
    struct sockaddr_un sock_name;
    unsigned int sockaddr_s = sizeof(sock_name);

    unlink(CONTROL_SOCK_NAME);

    if((fd_conn = socket(AF_UNIX, SOCK_STREAM, 0)) == -1){
        sa_main_set_err(&control_cntx->thread_err, "socket()", strerror(errno), shared_cntx->main_loop);
        goto failure;
    }

    memset(&sock_name, 0, sizeof(sock_name));
    sock_name.sun_family = AF_UNIX;
    strncpy(sock_name.sun_path, CONTROL_SOCK_NAME, sizeof(sock_name.sun_path) - 1);
    ret_code = bind(fd_conn, (struct sockaddr *) &sock_name,
        strlen(sock_name.sun_path) + sizeof(sock_name.sun_family)
    ); 
    if(ret_code < 0){
    	sa_main_set_err(&control_cntx->thread_err, "bind", strerror(errno), shared_cntx->main_loop);
    	goto failure;
    }

    ret_code = listen(fd_conn, 20);
    if(ret_code == -1){
    	sa_main_set_err(&control_cntx->thread_err, "listen()", strerror(errno), shared_cntx->main_loop);
        goto failure;
    }

    while(1)
	{
        fd_data = accept(fd_conn, NULL, NULL);
        if(fd_data < 0){
        	sa_main_set_err(&control_cntx->thread_err, "accept", strerror(errno), shared_cntx->main_loop);
            goto failure;
        }

        sa_util_print_info(&shared_cntx->stdout_lk, "sa_main_listen_on_control(): Control socket accepted connection\n");
        do{
			memset((void *) control_cntx->control_buf, 0, CONTROL_BUF_SIZE);
            nbytes = recv(fd_data, control_cntx->control_buf, CONTROL_BUF_SIZE, 0);
            if(nbytes != CONTROL_BUF_SIZE)
			{
				sa_main_set_err(&control_cntx->thread_err, "recv()", strerror(errno), shared_cntx->main_loop);
                goto failure;
            }
            ret_code = sa_main_decode_control_buf(control_cntx);
            if(!ret_code)
            	goto failure;

            if(ret_code == -1)
            	break;

        }while(nbytes > 0);
        
        close(fd_data);
    }

    failure:
    close(fd_conn);
    unlink(CONTROL_SOCK_NAME);
    ret_val = malloc(sizeof(sa_main_thread_ret_val_t));
    memcpy(ret_val, &control_cntx->thread_err, sizeof(sa_main_thread_ret_val_t));
    return ret_val;
    //pthread_cleanup_pop(0);
} 

int sa_main_get_x11_inactive_time(sa_main_mainloop_context_t *mainloop_cntx, unsigned long *idle_time){
	int ret_code, no_fucks1, no_fucks2;
	Display *d;
	XScreenSaverInfo *x_info;
	sa_main_shared_context_t *shared_cntx = mainloop_cntx->shared_cntx;

	//TODO: Find a way to get the default display
	putenv("XAUTHORITY=/home/grasm/.Xauthority");
	if(!(d = XOpenDisplay(":0"))){
		sa_main_set_err(&mainloop_cntx->thread_err, "XOpenDisplay", "Open display failed", shared_cntx->control_listen);
		return 1;
	}

	ret_code = XScreenSaverQueryExtension(d, &no_fucks1, &no_fucks2);
	if(!ret_code){
		sa_main_set_err(&mainloop_cntx->thread_err, "XScreenSaverQueryExtension()", "X11 extension not found", shared_cntx->control_listen);
		return 1;
	}

	if(!(x_info = XScreenSaverAllocInfo())){
		sa_main_set_err(&mainloop_cntx->thread_err, "XScreenSaverAllocInfo()", "Could not allocate space", shared_cntx->control_listen);
		return 1;
	}

	ret_code = XScreenSaverQueryInfo(d, DefaultRootWindow(d), x_info);
	if(!ret_code){
		sa_main_set_err(&mainloop_cntx->thread_err, "XScreenSaverQueryInfo()", "Could not read information", shared_cntx->control_listen);
		return 1;
	}
	*idle_time = x_info->idle/1000;
	
	XFree(x_info);
	XCloseDisplay(d);
	
	return 0;
}

int sa_main_get_cpu_usage(sa_main_mainloop_context_t *mainloop_cntx, long double *res){
	FILE *fp;
	if(!(fp = fopen("/proc/stat","r")))
		goto failure;
	if(fscanf(fp,"%*s %Lf %Lf %Lf %Lf",&res[0],&res[1],&res[2],&res[3]) != 4)
		goto failure;
    
	fclose(fp);
	return 0;
	
	failure:
		sa_main_set_err(&mainloop_cntx->thread_err, "sa_main_get_cpu_usage()", strerror(errno), mainloop_cntx->shared_cntx->control_listen);
		return 1;
}

int sa_main_is_inactive(sa_main_mainloop_context_t *mainloop_cntx){
	sa_main_shared_context_t *shared_cntx = mainloop_cntx->shared_cntx;
	long double a[4], b[4];
	unsigned long x11_idle;
	int ret_code;
	
	ret_code = sa_main_get_cpu_usage(mainloop_cntx, a);
	if(ret_code != 0)
		return -1;

	usleep(shared_cntx->sleep_time*1000000);
	
	ret_code = sa_main_get_cpu_usage(mainloop_cntx, b);
	if(ret_code != 0)
		return -1;

	ret_code = sa_main_get_x11_inactive_time(mainloop_cntx, &x11_idle);
	if(ret_code != 0){
		return -1;
	}
	
	//Don't forget to unlock after call
	ret_code = pthread_mutex_lock(shared_cntx->mut_trip.lk);
	if(ret_code != 0){
		sa_main_set_err(&mainloop_cntx->thread_err, "pthread_mutex_lock", strerror(ret_code), shared_cntx->control_listen);
		return -1;
	}

	long double load = ((b[0]+b[1]+b[2]) - (a[0]+a[1]+a[2])) / ((b[0]+b[1]+b[2]+b[3]) - (a[0]+a[1]+a[2]+a[3]));
	sa_util_print_info(&shared_cntx->stdout_lk, "sa_main_is_inactive(): Load: %Lf, Limit:%Lf\nsa_main_is_inactive(): Idle: %lu sec, Limit: %d sec\n", load, (long double) shared_cntx->usage_cutoff, x11_idle, shared_cntx->sleep_time);

	if((x11_idle > (unsigned long) shared_cntx->sleep_time) && (load < (long double) shared_cntx->usage_cutoff)){
		sa_util_print_info(&shared_cntx->stdout_lk, "sa_main_is_inactive(): Detected inactivity\n");
		return 1;
	}

	return 0;		
}


int sa_main_req_sound_playback(sa_main_mainloop_context_t *mainloop_cntx){
	sa_main_shared_context_t *shared_cntx = mainloop_cntx->shared_cntx;
	int i, ret_code, saved_act_flag, fin_play = 0;
	process_info_t proc;

	ret_code = pthread_mutex_lock(shared_cntx->mut_trip.lk);
	if(ret_code != 0){
		sa_main_set_err(&mainloop_cntx->thread_err, "pthread_mutex_lock", strerror(ret_code), shared_cntx->control_listen);
		return 1;
	}
	
	restart_bc_playback_failed:
	
	//Find the UID under which pulse is run
	for(i = 0; i < 10; ++i){
		sa_process_get_uid("pulseaudio", &proc);

		if(proc.valid)
			break;
		
		usleep(500000);
	}
	if(i == 9){
		sa_main_set_err(&mainloop_cntx->thread_err, "sa_process_get_uid()", "Failed multiple times", shared_cntx->control_listen);
		return 1;
	}
	char *argv[] = {shared_cntx->fname, NULL};
	char *err[1];
	ret_code = sa_process_create_subprocess(&shared_cntx->mut_trip, "/usr/local/bin/sound", &proc, argv, err);
	if(ret_code && err[0]){
		sa_util_print_err(&shared_cntx->stderr_lk, "%s\n", err[0]);
		free(err[0]);
		goto restart_bc_playback_failed;
	}

	//Try to get a connection multiple times
	for(i = 0; i < 10; ++i){
		ret_code = sa_main_w_play_sock(mainloop_cntx, 'p');
		if(ret_code == -1)
			return 1;
		if(ret_code == 1)
			break;
		
		usleep(500000);
	}
	if(i == 9){
		sa_main_set_err(&mainloop_cntx->thread_err, "connect()", "Multiple failed connects", shared_cntx->control_listen);
		return 1;
	}

	while(1){
		ret_code = pthread_cond_wait(shared_cntx->mut_trip.cond, shared_cntx->mut_trip.lk);
		if(ret_code != 0){
			sa_main_set_err(&mainloop_cntx->thread_err, "pthread_cond_wait()", strerror(ret_code), shared_cntx->control_listen);
			return 1;
		}

		if(fin_play){
			sa_util_print_info(&shared_cntx->stdout_lk, "sa_main_req_sound_playback(): Catched process died cond trigger after normal termination\n");
			*(shared_cntx->mut_trip.flag) = 0;
			break;
		}

		if(*(shared_cntx->mut_trip.flag)){
			//Restore value 
			*(shared_cntx->mut_trip.flag) = 0;
			sa_util_print_info(&shared_cntx->stdout_lk, "sa_main_req_sound_playback(): Mutex cond was triggered bc sound proc died\n");
			//Retry
			goto restart_bc_playback_failed;
		}

		//Quit
		if(shared_cntx->req_stop_flag){
			sa_util_print_info(&shared_cntx->stdout_lk, "sa_main_req_sound_playback(): Mutex cond was triggered bc asked for stop\n");
			shared_cntx->req_stop_flag = 0;
			fin_play = 1;
			ret_code = sa_main_w_play_sock(mainloop_cntx, 's');
			if(ret_code == -1)
				return 1;
		}
	}

	ret_code = pthread_mutex_unlock(shared_cntx->mut_trip.lk);
	if(ret_code != 0){
		sa_main_set_err(&mainloop_cntx->thread_err, "pthread_mutex_unlock", strerror(ret_code), shared_cntx->control_listen);
		return 1;
	}

	return 0;
}

void *sa_main_loop(void *arg){
	pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);
	//pthread_cleanup_push(sa_main_cleanup_mainloop, arg);

	int ret_code;
	sa_main_mainloop_context_t *mainloop_cntx = arg;
	sa_main_shared_context_t *shared_cntx = mainloop_cntx->shared_cntx;
	sa_main_thread_ret_val_t *ret_val;

	while(1)
	{
		ret_code = sa_main_is_inactive(mainloop_cntx);
		if(ret_code < 0){
			//Don't check return val, going to crash anyways
			pthread_mutex_unlock(shared_cntx->mut_trip.lk);
			goto failure;
		}

		if(ret_code && (shared_cntx->act_flag == 1)){
			//Lock is not reentrant
			ret_code = pthread_mutex_unlock(shared_cntx->mut_trip.lk);
			if(ret_code != 0){
				sa_main_set_err(&mainloop_cntx->thread_err, "pthread_mutex_unlock", strerror(ret_code), shared_cntx->control_listen);
				goto failure;
			}

			put_to_wk_sleep(shared_cntx->tm_hour, shared_cntx->tm_min);
			ret_code = sa_main_req_sound_playback(mainloop_cntx);
			if(ret_code != 0){
				goto failure;
			}
		} else {
			ret_code = pthread_mutex_unlock(shared_cntx->mut_trip.lk);
			if(ret_code != 0){
				sa_main_set_err(&mainloop_cntx->thread_err, "pthread_mutex_unlock", strerror(ret_code), shared_cntx->control_listen);
				goto failure;
			}
		}
	}

	failure:
    ret_val = malloc(sizeof(sa_main_thread_ret_val_t));
    memcpy(ret_val, &mainloop_cntx->thread_err, sizeof(sa_main_thread_ret_val_t));
    return ret_val;
    //pthread_cleanup_pop(0);
}

int main(){
	sa_main_shared_context_t shared_cntx;
	sa_main_mainloop_context_t mainloop_cntx;
	sa_main_control_listen_context_t control_cntx;
	sa_main_thread_ret_val_t *t1_ret[1] = {NULL}, *t2_ret[1] = {NULL};
	int ret_code;
	pthread_mutex_t stdout_lk, stderr_lk;
	pthread_mutex_t mut;
	pthread_cond_t cond;
	volatile int flag = 0;
	char default_file[] = "/home/grasm/ownCloud/sleep_app/sample.wav";

	if(getuid() != 0){
		sa_util_print_err(&stderr_lk, "You need to be root to use this program.\n");
		goto root_failure;
	}

	pthread_mutex_init(&stdout_lk, NULL);
	pthread_mutex_init(&stderr_lk, NULL);
	pthread_mutex_init(&mut, NULL);
	pthread_cond_init(&cond, NULL);

	shared_cntx.kill_flag = 0;
	shared_cntx.act_flag = 0;
	shared_cntx.tm_hour = 5;
	shared_cntx.tm_min = 30;
	shared_cntx.sleep_time = 10*60;
	shared_cntx.usage_cutoff = 0.03;
	shared_cntx.stdout_lk = stdout_lk;
	strcpy(shared_cntx.fname, default_file);

	shared_cntx.mut_trip.flag = &flag;
	shared_cntx.mut_trip.cond = &cond;
	shared_cntx.mut_trip.lk = &mut;

	control_cntx.shared_cntx = &shared_cntx;
	mainloop_cntx.shared_cntx = &shared_cntx;

	control_cntx.thread_err.err_code = 0;
	control_cntx.thread_err.err_str = NULL;

	mainloop_cntx.thread_err.err_code = 0;
	mainloop_cntx.thread_err.err_str = NULL;
	
	ret_code = pthread_create(&shared_cntx.main_loop, NULL, sa_main_loop, (void*) &mainloop_cntx);
	if(ret_code){
		sa_util_print_err(&stderr_lk, "Error pthread_create(): %s\n", strerror(errno));
		goto failure;
	}

	ret_code = pthread_create(&shared_cntx.control_listen, NULL, sa_main_listen_on_control, (void *) &control_cntx);
	if(ret_code){
		sa_util_print_err(&stderr_lk, "Error pthread_create(): %s\n", strerror(errno));
		goto failure;
	}

	ret_code = pthread_join(shared_cntx.control_listen, (void **) t2_ret);
	if(ret_code){
		sa_util_print_info(&stdout_lk, "Error pthread_join(): %s\n", strerror(ret_code));
		goto failure;
	}
	if(*t2_ret != PTHREAD_CANCELED && (**t2_ret).err_code != 0){
		sa_util_print_err(&stderr_lk, "%s\n", (**t2_ret).err_str);
		free((**t2_ret).err_str);
		free(*t2_ret);
		//Don't exit, wait for other thread cancel
	}

	ret_code = pthread_join(shared_cntx.main_loop, (void **) t1_ret);
	if(ret_code){
		sa_util_print_info(&stdout_lk, "Error pthread_join(): %s\n", strerror(ret_code));
		goto failure;
	}
	if(*t1_ret != PTHREAD_CANCELED && (**t1_ret).err_code != 0){
		sa_util_print_err(&stderr_lk, "%s\n", (**t1_ret).err_str);
		free((**t1_ret).err_str);
		free(*t1_ret);
		goto failure;
	}

	failure:
	pthread_mutex_destroy(&stderr_lk);
	pthread_mutex_destroy(&mut);
	pthread_cond_destroy(&cond);
	
	root_failure:

	return 1;
}
