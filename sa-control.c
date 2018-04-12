#include <stdlib.h>
#include <argp.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <arpa/inet.h>
#include <signal.h>
#include "2048.h"
#include "sa_util.h"

const char *argp_program_version =
  "sa-control 1.0";
const char *argp_program_bug_address =
  "<leckmich@am.arsch>";

/* Program documentation. */
static char doc[] =
  "Control the sleep app daemon";

static char args_doc[] = "lol du opfer";

static struct argp_option options[] = {
  {"hour", 'h', 0, 0, "Set the alarm hour", 0},
  {"min", 'm', 0, 0, "Set the alarm minute", 1},
  {"activate", 'a', 0, 0, "Activate the alarm", 2},
  {"deactivate", 'd', 0, 0, "Deactivate the alarm", 3},
  {"disarm", 'r', 0, 0, "Disarm the alarm", 4},
  {"file", 'f', 0, 0, "Set the audio filename", 1},
  {"kill", 'k', 0, 0, "Kill the daemon", 1},
  {0}
};

struct arguments
{
    int hour, min;
    int active, a_h, a_m, dis_flag, f_flag, kill_flag;
    int count;
    char fname[CONTROL_BUF_SIZE - 1];
};

static error_t
parse_opt (int key, char *arg, struct argp_state *state)
{
  struct arguments *arguments = state->input;

  switch (key)
    {
    case 'a':
        arguments->active = 1;
        break;
    case 'd':
        arguments->active = 0;
        break;
    case 'f':
        arguments->f_flag = 1;
        break;
    case 'h':
        arguments->a_h = 1;
        break;
    case 'm':
        arguments->a_m = 1;
        break;
    case 'k':
        arguments->kill_flag = 1;
        break;
    case 'r':
        sa_game_2048();
        arguments->dis_flag = 1;
        break;

    case ARGP_KEY_ARG:
        if(arguments->a_h)
        {
            arguments->hour = atoi(arg);
            arguments->a_h = 0;
            arguments->count += 1;

            if(arguments->hour < 0 || arguments->hour > 23)
                argp_usage(state);

        }
        else if(arguments->a_m){
            arguments->min = atoi(arg);
            arguments->a_m = 0;
            arguments->count += 1;

            if(arguments->min < 0 || arguments->min > 59)
                argp_usage(state);
        }
        else if(arguments->f_flag){
            strncpy(arguments->fname, arg, CONTROL_BUF_SIZE - 1);
            FILE *test;
            if(!(test = fopen(arguments->fname, "r"))){
                argp_usage(state);
            }

            fclose(test);
        }
        else
        {
            argp_usage(state);
        }

        break;

    case ARGP_KEY_END:
        if(arguments->count != 0 && arguments->count != 2){
            argp_usage(state);
        }
        break;
    
    default:
      return ARGP_ERR_UNKNOWN;
    }
  return 0;
}

void sa_control_send_to_daemon(struct arguments *args){
    struct sockaddr_un remote_sock_addr;
	int fd_conn, fd_data, ret_code;
	memset(&remote_sock_addr, 0, sizeof(struct sockaddr_un));

	fd_conn = socket(AF_UNIX, SOCK_STREAM, 0);
	if(fd_conn < 0){
		fprintf(stderr, "Error socket(): %s\n", strerror(errno));
		exit(1);
	}

	remote_sock_addr.sun_family = AF_UNIX;
	strncpy(remote_sock_addr.sun_path, CONTROL_SOCK_NAME, sizeof(remote_sock_addr.sun_path)-1);

	fd_data = connect(fd_conn, (struct sockaddr*) &remote_sock_addr,
		strlen(remote_sock_addr.sun_path) + sizeof(remote_sock_addr.sun_family)
	);

	if(fd_data < 0){
		fprintf(stderr, "Error connect(): %s\n", strerror(errno));
		exit(1);
	}
    char buf[CONTROL_BUF_SIZE];


    //KILL THE DAEMON
    memset(buf, 0, CONTROL_BUF_SIZE);
    if(args->kill_flag == 1){
        *buf = 0x06;
        ret_code = send(fd_conn, &buf, CONTROL_BUF_SIZE, 0);
        if(ret_code < 0){
            fprintf(stderr, "Error send(): %s\n", strerror(errno));
            exit(1);
        }
    }

    //New time
    memset(buf, 0, CONTROL_BUF_SIZE);
    if(args->count == 2){
        memcpy((void *) (buf + 1), (void *) &(args->hour), sizeof(int));
        memcpy((void *) (buf + 5), (void *) &(args->min), sizeof(int));
        ret_code = send(fd_conn, buf, CONTROL_BUF_SIZE, 0); 
        if(ret_code < 0){
            fprintf(stderr, "Error send(): %s\n", strerror(errno));
            exit(1);
        }
    }

    //Active status
    memset(buf, 0, CONTROL_BUF_SIZE);
    if(args->active == 1){
        *buf = 0x01;
    } else {
        *buf = 0x02;
    }
    ret_code = send(fd_conn, &buf, CONTROL_BUF_SIZE, 0);
    if(ret_code < 0){
        fprintf(stderr, "Error send(): %s\n", strerror(errno));
        exit(1);
    }

    //Disarm
    memset(buf, 0, CONTROL_BUF_SIZE);
    if(args->dis_flag == 1){
        *buf = 0x04;
        ret_code = send(fd_conn, &buf, CONTROL_BUF_SIZE, 0);
        if(ret_code < 0){
            fprintf(stderr, "Error send(): %s\n", strerror(errno));
            exit(1);
        }
    }

    //Filename
    memset(buf, 0, CONTROL_BUF_SIZE);
    if(args->f_flag == 1){
        buf[0] = 0x05;
        strncpy((void *) (buf + 1), args->fname, CONTROL_BUF_SIZE - 1);
        ret_code = send(fd_conn, &buf, CONTROL_BUF_SIZE, 0);
        if(ret_code < 0){
            fprintf(stderr, "Error send() %s\n", strerror(errno));
            exit(1);
        }
    }

    //EOF
    memset(buf, 0xFF, CONTROL_BUF_SIZE);
    ret_code = send(fd_conn, buf, CONTROL_BUF_SIZE, 0); 
	if(ret_code < 0){
		fprintf(stderr, "Error send(): %s\n", strerror(errno));
		exit(1);
	}
	
	ret_code = close(fd_data);
	if(ret_code < 0){
		fprintf(stderr, "Error close(): %s\n", strerror(errno));
		exit(1);
	}
}

void signal_handler(int sig){
    if(sig == SIGPIPE){
        fprintf(stdout, "Could not send all the data. Other side died\n");
        exit(1);
    }
}

/* Our argp parser. */
static struct argp argp = { options, parse_opt, args_doc, doc , NULL, NULL, NULL};

int main (int argc, char **argv)
{
    if(getuid() != 0){
        fprintf(stderr, "You need to be root to use this program.\n");
        exit(1);
    }
    signal(SIGPIPE, signal_handler);

    struct arguments arguments;

    arguments.hour = 5;
    arguments.min = 30;
    arguments.active = 1;
    arguments.count = 0;
    arguments.a_h = 0;
    arguments.a_m = 0;
    arguments.dis_flag = 0;
    arguments.f_flag = 0;
    arguments.kill_flag = 0;

    
    argp_parse (&argp, argc, argv, 0, 0, &arguments);
    sa_control_send_to_daemon(&arguments);

    fprintf(stdout, "Sent the arguments to the daemon.\n");
    

    return 0;
}