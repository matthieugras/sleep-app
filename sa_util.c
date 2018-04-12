#include "sa_util.h"

void sa_util_print_info(pthread_mutex_t *stdout_lk, const char *restrict __format, ...){
    va_list ap;
    va_start(ap, __format);
    pthread_mutex_lock(stdout_lk);
    vfprintf(stdout, __format, ap);
    fflush(stdout);
    pthread_mutex_unlock(stdout_lk);
    va_end(ap);
}

void sa_util_print_err(pthread_mutex_t *stdout_lk, const char *restrict __format, ...){
    va_list ap;
    va_start(ap, __format);
    pthread_mutex_lock(stdout_lk);
    vfprintf(stdout, __format, ap);
    fflush(stdout);
    pthread_mutex_unlock(stdout_lk);
    va_end(ap);
}

int sa_util_w_play_sock(char cmd){
	struct sockaddr_un remote_sock_addr;
	int fd_conn, fd_data, ret_code;
	char buff = cmd;
	memset(&remote_sock_addr, 0, sizeof(struct sockaddr_un));

	fd_conn = socket(AF_UNIX, SOCK_STREAM, 0);
	if(fd_conn < 0){
		fprintf(stderr, "Error socket(): %s\n", strerror(errno));
		exit(1);
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
			fprintf(stderr, "Error close(): %s\n", strerror(errno));
			exit(1);
		}

		return 0;
	}
	ret_code = send(fd_conn, &buff, PLAYBACK_BUF_SIZE, 0); 
	if(ret_code < 0){
		fprintf(stdout, "Error send(): %s\n", strerror(errno));
		exit(1);
	}
	ret_code = recv(fd_conn, &buff, PLAYBACK_BUF_SIZE, 0);
	if(ret_code < 0){
		fprintf(stderr, "Error recv(): %s\n", strerror(errno));
		exit(1);
	}
	if(buff != 'a'){
		fprintf(stderr, "Error wr_to_playback_sock(): No ACK from Server.\n");
		exit(1);
	}
	printf("Received ACK\n");
	fflush(stdout);
	
	ret_code = close(fd_conn);
	if(ret_code == -1){
		fprintf(stderr, "Error close(): %s\n", strerror(errno));
		exit(1);
	}
	return 1;
}

int sa_main_mem_is_pat(void *ptr, size_t n, char c){
	char *b_ptr = ptr;
	for(size_t i = 0; i < n; ++i){
		if(*(b_ptr + i) != c)
			return 0;
	}
	return 1;
}