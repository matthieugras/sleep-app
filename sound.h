#include <stdio.h>
#include <pulse/error.h>
#include <pulse/stream.h>
#include <pulse/mainloop.h>
#include <pulse/thread-mainloop.h>
#include <pulse/context.h>
#include <pulse/mainloop-signal.h>
#include <pulse/xmalloc.h>
#include <pulse/introspect.h>
#include <pulse/volume.h>
#include <pulse/subscribe.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <pthread.h>
#include "sa_util.h"
#include "audio_transcode.h"
#define PLAYBACK_SOCK_NAME "/tmp/sa_playback.socket"
#define PLAYBACK_BUF_SIZE 1
#define SOUND_LVL_6 0x10000U
#define SOUND_LVL_5 0xD555U
#define SOUND_LVL_4 0xAAAAU
#define SOUND_LVL_3 0x8000U
#define SOUND_LVL_2 0x5555U

void sa_sound_playback();