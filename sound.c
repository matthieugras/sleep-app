#include "sound.h"

static size_t nb_so_far = 0;
static int stop_stream;
static pa_stream *p;
static pa_context *c;
static pa_threaded_mainloop *m;
static const pa_sample_spec ss = {
    PA_SAMPLE_S16LE,
    44100,
    2
};
static pa_cvolume volume_change;
static sa_transcode_context_t *sa_trans_cntx;

static pthread_t thread;
static pthread_mutex_t stop_sig_mut;
static pthread_mutex_t volume_mut;
static pthread_mutex_t stdout_lk;
static pthread_mutex_t stderr_lk;
static pthread_mutex_t pa_cntx_ready;
static pthread_cond_t pa_cntx_ready_cond;
static volatile int pa_cntx_ready_flag = 0;
static volatile pa_volume_t curr_volume;
static volatile int stop_sig_rcvd = 1;
static volatile int pa_mainloop_stopped_flag = 0;



void context_drain_completed(pa_context *c, void *userdata){
    pa_context_disconnect(c);
}

void stream_drain_completed(pa_stream *s, int success, void *userdata){
    pa_operation *pa_op;
    if(!success){
        sa_util_print_err(&stderr_lk, "Draining stream failed");
        exit(1);
    }
    pa_stream_disconnect(p);
    pa_stream_unref(p);
    if(!(pa_op = pa_context_drain(c, context_drain_completed, NULL)))
        pa_context_disconnect(c);
    else
        pa_operation_unref(pa_op);
}

void write_data_to_stream(pa_stream *s, size_t nbytes, void *userdata){
    //sa_util_print_info(&stdout_lk, "Writing %lu b to buffer\n", nbytes);
    if(!stop_stream){
        pthread_mutex_lock(&stdout_lk);
            sa_decode_transcode(sa_trans_cntx, nbytes);
        pthread_mutex_unlock(&stdout_lk);
        pa_stream_write(s, sa_trans_cntx->tmp_buf, nbytes, free, 0, PA_SEEK_RELATIVE);
        //nb_so_far += nbytes;
        pthread_mutex_lock(&stop_sig_mut);
        if(stop_sig_rcvd){
            stop_stream = 1;
            pa_operation_unref(pa_stream_drain(s,stream_drain_completed,NULL));
        }
        pthread_mutex_unlock(&stop_sig_mut);
    }
}

void stream_started(pa_stream *p, void *userdata){
    pa_stream_state_t curr_state = pa_stream_get_state(p);
    if(curr_state == PA_STREAM_FAILED){
        sa_util_print_err(&stderr_lk, "Error stream_started(): Stream unexpectedly closed");
        exit(1);
    }
    sa_util_print_info(&stdout_lk, "pa_stream in state %u\n",curr_state);
}

void sa_sound_unlock_callback(pa_context *c, int success, void *userdata){
    if(!pa_threaded_mainloop_in_thread(m))
        pa_threaded_mainloop_unlock(m);
}

void sa_sound_set_spkr_max(pa_context *c, const pa_sink_info *info, int eol, void *userdata){
    if(eol)
        return;

    pa_sink_port_info ** ports = info->ports;
    uint32_t n_ports = info->n_ports;
    pa_sink_port_info *curr_port;
    uint32_t idx = info->index;
    pthread_mutex_lock(&volume_mut);
        pa_cvolume_set(&volume_change, info->channel_map.channels, curr_volume);
    pthread_mutex_unlock(&volume_mut);

    for(unsigned i = 0; i < n_ports; ++i){
        curr_port = ports[i];
        if(strstr(curr_port->name, "speaker")){
            if(!pa_threaded_mainloop_in_thread(m))
                pa_threaded_mainloop_lock(m);
            pa_operation_unref(pa_context_set_sink_port_by_index(c, idx, curr_port->name, sa_sound_unlock_callback, NULL));

            if(!pa_threaded_mainloop_in_thread(m))
                pa_threaded_mainloop_lock(m);
            pa_operation_unref(pa_context_set_sink_mute_by_index(c, idx, 0, sa_sound_unlock_callback, NULL));

            if(!pa_threaded_mainloop_in_thread(m))
                pa_threaded_mainloop_lock(m);
            pa_operation_unref(pa_context_set_sink_volume_by_index(c, idx, &volume_change, sa_sound_unlock_callback, NULL));
        }
    }
    if(!pa_threaded_mainloop_in_thread(m))
        pa_threaded_mainloop_unlock(m);
}

void sa_sound_subscribe_callback(pa_context *c, pa_subscription_event_type_t t, uint32_t idx, void *userdata){
    //TODO: More fine grained control
    pa_operation_unref(pa_context_get_sink_info_list(c, sa_sound_set_spkr_max, NULL));
}

void sa_sound_subscribe_succ(pa_context *c, int success, void *userdata){
    if(!success)
        return;

    //Set the callback that gets notified if the state changes
    pa_context_set_subscribe_callback(c, sa_sound_subscribe_callback, NULL);
}

void initialize_stream(pa_context *c, void *userdata){
    pa_context_state_t curr_state = pa_context_get_state(c);
    sa_util_print_info(&stdout_lk, "pa_context in state %u.\n", curr_state);

    switch(curr_state){
        case PA_CONTEXT_FAILED: {
            sa_util_print_err(&stderr_lk, "Error initialize_stream(): Context unexpectedly closed: %s", pa_strerror(pa_context_errno(c)));
            exit(1);
        }
        case PA_CONTEXT_READY: {
            //Set volume to maximum
            pthread_mutex_lock(&volume_mut);
                curr_volume = SOUND_LVL_6;
            pthread_mutex_unlock(&volume_mut);

            //Change volume of sinks and set to speakers - async mäßig
            pa_operation_unref(pa_context_get_sink_info_list(c, sa_sound_set_spkr_max, NULL));
            
            //Try to subscribe to further changes
            pa_operation_unref(pa_context_subscribe(c, PA_SUBSCRIPTION_MASK_ALL, sa_sound_subscribe_succ, NULL));

            if(!(p = pa_stream_new(c, "Alarm clock sound", &ss, NULL))){
                sa_util_print_err(&stderr_lk, "Error pa_stream_new()\n");
                exit(1);
            }
            pa_stream_set_state_callback(p, stream_started, NULL);
            pa_stream_set_write_callback(p, write_data_to_stream, NULL);
            if(pa_stream_connect_playback(p, NULL, NULL, (pa_stream_flags_t)0, NULL, NULL)){
                sa_util_print_err(&stderr_lk, "Error pa_stream_connect_playback(): %s\n", pa_strerror(pa_context_errno(c)));
                exit(1);
            }
            sa_util_print_info(&stdout_lk, "initialize_stream(): Signaling that init is done\n");
            pthread_mutex_lock(&pa_cntx_ready);
                pa_cntx_ready_flag = 1;
                pthread_cond_broadcast(&pa_cntx_ready_cond);
            pthread_mutex_unlock(&pa_cntx_ready);
            break;
        }
        case PA_CONTEXT_TERMINATED: {
            pa_context_unref(c);
            pa_mainloop_stopped_flag = 1;
            pa_threaded_mainloop_signal(m, 1);
            sa_util_print_info(&stdout_lk, "initialize_stream(): Signaling that context terminated.\n");
            break;
        }
        default: break;
    }
}

void sa_sound_stop_playback(){
    pa_threaded_mainloop_lock(m);
        stop_sig_rcvd = 1;
        sa_util_print_info(&stdout_lk, "sa_sound_stop_playback(): Sending stop signal to pa_mainloop.\n");
        while(!pa_mainloop_stopped_flag)
            pa_threaded_mainloop_wait(m);
        sa_util_print_info(&stdout_lk, "sa_sound_stop_playback(): Got confirmation from pa_mainloop\n");
        pa_threaded_mainloop_accept(m);
    pa_threaded_mainloop_unlock(m);
    sa_util_print_info(&stdout_lk, "sa_sound_stop_playback(): Stopping pa_mainloop\n");
    pa_threaded_mainloop_stop(m);
    sa_decode_context_close(sa_trans_cntx);
    pa_threaded_mainloop_free(m);
}

void sa_sound__start_playback(){
    stop_stream = 0;
    if(!(m = pa_threaded_mainloop_new())){
        sa_util_print_err(&stderr_lk, "Error pa_mainloop_new()\n");
        exit(1);
    }
    
    if(!(c = pa_context_new(pa_threaded_mainloop_get_api(m), "Sleep app"))){
        sa_util_print_err(&stderr_lk, "Error pa_context_new()\n");
        exit(1);
    }
    
    pa_context_set_state_callback(c, initialize_stream, NULL);
    

    if(pa_context_connect(c, NULL, (pa_context_flags_t) 0, NULL) < 0){
        sa_util_print_err(&stderr_lk, "Error pa_context_connect()\n");
        exit(1);
    }

    pa_threaded_mainloop_start(m);

    //Wait for everything to by ready
    sa_util_print_info(&stdout_lk, "sa_sound__start_playback(): Wait for init of pa_cntx\n");
    pthread_mutex_lock(&pa_cntx_ready);
        while(!pa_cntx_ready_flag)
            pthread_cond_wait(&pa_cntx_ready_cond, &pa_cntx_ready);
    pthread_mutex_unlock(&pa_cntx_ready);
    sa_util_print_info(&stdout_lk, "sa_sound__start_playback(): Got signal for pa_cntx init\n");
}

int main(int argc, char const *argv[]){
    unlink(PLAYBACK_SOCK_NAME);

    int flag_finished = 0, flag_vol_change = 0;
    int fd_conn, fd_data, ret_code, nbytes;
    struct sockaddr_un sock_name;
    char buff;
    unsigned int sockaddr_s = sizeof(sock_name);

    pthread_mutex_init(&stop_sig_mut, NULL);
    pthread_mutex_init(&volume_mut, NULL);
    pthread_mutex_init(&stdout_lk, NULL);
    pthread_mutex_init(&stderr_lk, NULL);
    pthread_mutex_init(&pa_cntx_ready, NULL);
    pthread_cond_init(&pa_cntx_ready_cond, NULL);

    if((fd_conn = socket(AF_UNIX, SOCK_STREAM, 0)) == -1){
        sa_util_print_err(&stderr_lk, "Error socket(): %s\n", strerror(errno));
        exit(1);
    }

    memset(&sock_name, 0, sizeof(sock_name));
    sock_name.sun_family = AF_UNIX;
    strncpy(sock_name.sun_path, PLAYBACK_SOCK_NAME, sizeof(sock_name.sun_path) - 1);
    ret_code = bind(fd_conn, (struct sockaddr *) &sock_name,
        strlen(sock_name.sun_path) + sizeof(sock_name.sun_family)
    ); 
    if(ret_code < 0){
        sa_util_print_err(&stderr_lk, "Error bind(): %s\n", strerror(errno));
        exit(1);
    }

    if(!(sa_trans_cntx = sa_decode_alloc_context())){
        sa_util_print_err(&stderr_lk, "Error sa_decode_alloc_context(): Alloc failed\n");
        exit(1);
    }
    if(sa_decode_context_init(argv[0], sa_trans_cntx)){
        sa_util_print_err(&stderr_lk, "%s\n", sa_trans_cntx->err_str);
        exit(1);
    }

    ret_code = listen(fd_conn, 20);
    if(ret_code == -1){
        sa_util_print_err(&stderr_lk, "Error listen(): %s\n", strerror(errno));
        exit(1);
    }

    while(1){
        fd_data = accept(fd_conn, NULL, NULL);
        if(fd_data < 0){
            sa_util_print_err(&stderr_lk, "Error accept(): %s\n", strerror(errno));
            exit(1);
        }

        sa_util_print_info(&stdout_lk, "Sound socket accepted connection.\n");
        
        do{
            nbytes = recv(fd_data, &buff, PLAYBACK_BUF_SIZE, 0);
            if(nbytes < 0){
                sa_util_print_err(&stderr_lk, "Error recv(): %s\n", strerror(errno));
                exit(1);
            }
            if(nbytes == 0)
                goto break_loop;

            switch(buff){
                case 'p': {
                    sa_util_print_info(&stdout_lk, "main(): Got signal to play\n");
                    pthread_mutex_lock(&stop_sig_mut);
                    if(stop_sig_rcvd != 1){
                        sa_util_print_err(&stderr_lk, "Error main(): Signal order.\n");
                        exit(1);
                    }
                    stop_sig_rcvd = 0;
                    pthread_mutex_unlock(&stop_sig_mut);
                    sa_sound__start_playback();
                    goto break_loop;
                }
                case 's': {
                    sa_util_print_info(&stdout_lk, "main(): Got signal to stop\n");
                    flag_finished = 1;
                    goto break_loop;
                }
                case '6' : {
                    pthread_mutex_lock(&volume_mut);
                        curr_volume = SOUND_LVL_6;
                    pthread_mutex_unlock(&volume_mut);
                    flag_vol_change = 1;
                    goto break_loop;
                }
                case '5' : {
                    pthread_mutex_lock(&volume_mut);
                        curr_volume = SOUND_LVL_5;
                    pthread_mutex_unlock(&volume_mut);
                    flag_vol_change = 1;
                    goto break_loop;
                }
                case '4' : {
                    pthread_mutex_lock(&volume_mut);
                        curr_volume = SOUND_LVL_4;
                    pthread_mutex_unlock(&volume_mut);
                    flag_vol_change = 1;
                    goto break_loop;
                }
                case '3' : {
                    pthread_mutex_lock(&volume_mut);
                        curr_volume = SOUND_LVL_3;
                    pthread_mutex_unlock(&volume_mut);
                    flag_vol_change = 1;
                    goto break_loop;
                }
                case '2' : {
                    pthread_mutex_lock(&volume_mut);
                        curr_volume = SOUND_LVL_2;
                    pthread_mutex_unlock(&volume_mut);
                    flag_vol_change = 1;
                    goto break_loop;
                }
                default: {
                    sa_util_print_err(&stderr_lk, "Error main(): Unknown char\n");
                    exit(1);
                }
            }
        }while(nbytes > 0);
    
        break_loop:
        buff = 'a';
        ret_code = send(fd_data, &buff, PLAYBACK_BUF_SIZE, 0);
        if(ret_code < 0){
            sa_util_print_err(&stderr_lk, "Error send(): %s\n", strerror(errno));
            exit(1);
        }
        ret_code = close(fd_data);
        if(ret_code == -1){
            sa_util_print_err(&stderr_lk, "Error close():%s\n", strerror(errno));
            exit(1);
        }
        
        if(flag_vol_change){
            flag_vol_change = 0;
            //Change volume of sinks and set to speakers - async mäßig
            pa_threaded_mainloop_lock(m);
            pa_operation_unref(pa_context_get_sink_info_list(c, sa_sound_set_spkr_max, NULL));
            pa_threaded_mainloop_unlock(m);
        }

        if(flag_finished){
            sa_sound_stop_playback();
            break;
        }
    }

    ret_code = close(fd_conn);
    if(ret_code == -1){
        sa_util_print_err(&stderr_lk, "Error close(): %s\n", strerror(errno));
        exit(1);
    }
    sa_util_print_info(&stdout_lk, "main(): Normal exit of sound playback process\n");
    pthread_mutex_destroy(&stop_sig_mut);
    pthread_mutex_destroy(&volume_mut);
    pthread_mutex_destroy(&stdout_lk);
    pthread_mutex_destroy(&stderr_lk);
    pthread_mutex_destroy(&pa_cntx_ready);
    pthread_cond_destroy(&pa_cntx_ready_cond);
    unlink(PLAYBACK_SOCK_NAME);
    return 0;
}