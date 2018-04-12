#include "sound.h"

static size_t nb_so_far = 0;
static int stop_stream;
static volatile int stop_sig_rcvd = 1;
static pa_stream *p;
static pa_context *c;
static pa_mainloop *m;
static FILE *music;
pthread_t thread;
static const pa_sample_spec ss = {
    PA_SAMPLE_S16NE,
    44100,
    2
};

static pa_cvolume volume_change;

void *gen_rdm_bytestream (size_t num_bytes)
{
  unsigned char *stream = pa_xmalloc (num_bytes);
  size_t i;

  for (i = 0; i < num_bytes; i++)
  {
    stream[i] = rand ();
  }

  return (void*)stream;
}

void *read_music(size_t num_bytes, size_t offset){
    void *music_buff = malloc(num_bytes);
    int ret_code = fseek(music, offset, SEEK_SET);
    int restart_write_nb, new_offset;
    if(ret_code == -1){
        fprintf(stderr, "Error fseek(): %s\n", strerror(errno));
        exit(1);
    }
    ret_code = fread(music_buff, 1, num_bytes, music);
    if(ret_code < num_bytes){
        fprintf(stdout, "End fread(): Rewinding\n");
        rewind(music);
        new_offset = ret_code;
        restart_write_nb = num_bytes - new_offset;
        ret_code = fread(music_buff + new_offset, 1, restart_write_nb, music);
        if(ret_code != restart_write_nb){
            fprintf(stderr, "Error fread(): Unexpected number of bytes read %d\n", ret_code);
            exit(1);
        }
        nb_so_far = restart_write_nb;
    } else {
        nb_so_far += num_bytes;
    }
    return music_buff;
}

void context_drain_completed(pa_context *c, void *userdata){
    pa_context_disconnect(c);
}

void stream_drain_completed(pa_stream *s, int success, void *userdata){
    pa_operation *pa_op;
    if(!success){
        fprintf(stderr, "Draining stream failed");
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
    
    printf("Writing %lu b to buffer\n", nbytes);
    fflush(stdout);
    if(!stop_stream){
        //void *temp_data = gen_rdm_bytestream(nbytes);
        void *temp_data = read_music(nbytes, nb_so_far);
        pa_stream_write(s, temp_data, nbytes, free, 0, PA_SEEK_RELATIVE);
        //nb_so_far += nbytes;
        
        if(stop_sig_rcvd){
            stop_stream = 1;
            pa_operation_unref(pa_stream_drain(s,stream_drain_completed,NULL));
        }
    }
}

void stream_started(pa_stream *p, void *userdata){
    pa_stream_state_t curr_state = pa_stream_get_state(p);
    if(curr_state == PA_STREAM_FAILED){
        fprintf(stderr, "Error stream_started(): Stream unexpectedly closed");
        exit(1);
    }
    printf("pa_stream in state %u\n",curr_state);
    fflush(stdout);
}

void sa_sound_set_spkr_max(pa_context *c, const pa_sink_info *info, int eol, void *userdata){
    if(eol)
        return;

    pa_sink_port_info ** ports = info->ports;
    uint32_t n_ports = info->n_ports;
    pa_sink_port_info *curr_port;
    uint32_t idx = info->index;
    pa_cvolume_set(&volume_change, info->channel_map.channels, PA_VOLUME_NORM);

    for(unsigned i = 0; i < n_ports; ++i){
        curr_port = ports[i];
        if(strstr(curr_port->name, "speaker")){
            pa_operation_unref(pa_context_set_sink_port_by_index(c, idx, curr_port->name, NULL, NULL));
            pa_operation_unref(pa_context_set_sink_mute_by_index(c, idx, 0, NULL, NULL));
            pa_operation_unref(pa_context_set_sink_volume_by_index(c, idx, &volume_change, NULL, NULL));
        }
    }
}

void initialize_stream(pa_context *c, void *userdata){
    pa_context_state_t curr_state = pa_context_get_state(c);
    printf("pa_context in state %u.\n", curr_state);
    fflush(stdout);

    switch(curr_state){
        case PA_CONTEXT_FAILED: {
            fprintf(stderr, "Error initialize_stream(): Context unexpectedly closed: %s", pa_strerror(pa_context_errno(c)));
            exit(1);
        }
        case PA_CONTEXT_READY: {
            //Change volume of sinks and set to speakers - async mäßig
            pa_operation_unref(pa_context_get_sink_info_list(c, sa_sound_set_spkr_max, NULL));
            
            if(!(p = pa_stream_new(c, "Alarm clock sound", &ss, NULL))){
                fprintf(stderr, "Error pa_stream_new()\n");
                exit(1);
            }
            pa_stream_set_state_callback(p, stream_started, NULL);
            pa_stream_set_write_callback(p, write_data_to_stream, NULL);
            if(pa_stream_connect_playback(p, NULL, NULL, (pa_stream_flags_t)0, NULL, NULL)){
                fprintf(stderr, "Error pa_stream_connect_playback(): %s\n", pa_strerror(pa_context_errno(c)));
                exit(1);
            }
            break;
        }
        case PA_CONTEXT_TERMINATED: {
            pa_context_unref(c);
            pa_mainloop_quit(m, 0);
            break;
        }
        default: break;
    }
}

void sa_sound_stop_playback(){
    stop_sig_rcvd = 1;
    if(pthread_join(thread, NULL)){
        fprintf(stderr, "Error pthread_join()\n");
        exit(1);
    }
    if(music != NULL)
        fclose(music);
}

void *sa_sound__start_playback(void *arg){
    stop_stream = 0;
    if(!(m = pa_mainloop_new())){
        fprintf(stderr, "Error pa_mainloop_new()\n");
        exit(1);
    }
    
    if(!(c = pa_context_new(pa_mainloop_get_api(m), "Sleep app"))){
        fprintf(stderr, "Error pa_context_new()\n");
        exit(1);
    }
    
    pa_context_set_state_callback(c, initialize_stream, NULL);
    

    if(pa_context_connect(c, NULL, (pa_context_flags_t) 0, NULL) < 0){
        fprintf(stderr, "Error pa_context_connect()\n");
        exit(1);
    }

    int ret;
    pa_mainloop_run(m,&ret);

    printf("Finished playing.\n");
    fflush(stdout);
    
    pa_mainloop_free(m);
    return NULL;
}

int main(){
    int fd_conn, fd_data, ret_code, nbytes;


    fprintf(stdout, "Creating music file descriptor\n");
    fflush(stdout);
    music = fopen("haftex", "rb");
    if(!music){
        fprintf(stderr, "Error fopen() music file: %s\n", strerror(errno));
        exit(1);
    }

    stop_sig_rcvd = 0;
    
    ret_code = pthread_create(&thread, NULL, sa_sound__start_playback, NULL);
    if(ret_code){
        fprintf(stderr, "Error pthread_create()\n");
        exit(1);
    }

    sleep(10);
    sa_sound_stop_playback();
    return 0;
}