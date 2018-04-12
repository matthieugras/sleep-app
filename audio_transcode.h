#include <stdio.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include "ringbuf.h"

typedef struct{
	char *file_path;
	AVFormatContext *fmt_cntx;
	AVCodecContext *en_cntx;
	AVCodecContext *de_cntx;
	AVCodec *en_codec;
	AVCodec *de_codec;
	ringbuf_t rest_buf;
	size_t poll_nxt_time;
	unsigned char *tmp_buf;
	char *err_str;
}sa_transcode_context_t;

static void sa_decode_set_err(sa_transcode_context_t *cont, char *err1, char *err2);
sa_transcode_context_t *sa_decode_alloc_context();
void sa_decode_context_close(sa_transcode_context_t *sa_trans_cntx);
int sa_decode_context_init(const char *f_name, sa_transcode_context_t *sa_trans_cntx);
int sa_decode_transcode(sa_transcode_context_t *sa_trans_cntx, size_t nbytes);