#define _GNU_SOURCE
#include "audio_transcode.h"

static void sa_decode_set_err(sa_transcode_context_t *cont, char *err1, char *err2){
	int ret_code;
	if(cont->err_str != NULL)
		free(cont->err_str);

	ret_code = asprintf(&(cont->err_str), "Error %s: %s", err1, err2);
	if(ret_code == -1){
		char err_msg[] = "Error: Error occured, but could not create error message";
		cont->err_str = malloc(sizeof(err_msg));
		memcpy(cont->err_str, err_msg, sizeof(err_msg));
	}
}

sa_transcode_context_t *sa_decode_alloc_context(){
	return malloc(sizeof(sa_transcode_context_t));
}

void sa_decode_context_close(sa_transcode_context_t *sa_trans_cntx){
	if(sa_trans_cntx->err_str != NULL)
		free(sa_trans_cntx->err_str);

	free(sa_trans_cntx->file_path);
	sa_trans_cntx->file_path = NULL;

	if(sa_trans_cntx->fmt_cntx != NULL){
		avformat_close_input(&(sa_trans_cntx->fmt_cntx));
		sa_trans_cntx->fmt_cntx = NULL;
	}

	if(sa_trans_cntx->de_cntx != NULL){
		avcodec_free_context(&(sa_trans_cntx->de_cntx));
		sa_trans_cntx->de_cntx = NULL;
	}

	if(sa_trans_cntx->en_cntx != NULL){
		avcodec_free_context(&(sa_trans_cntx->en_cntx));
		sa_trans_cntx->en_cntx = NULL;
	}

	if(sa_trans_cntx->rest_buf != NULL){
		ringbuf_free(&sa_trans_cntx->rest_buf);
		sa_trans_cntx->rest_buf = NULL;
	}

	free(sa_trans_cntx);
}

int sa_decode_context_init(const char *f_name, sa_transcode_context_t *sa_trans_cntx){

	/*
	*	Initialize 
	*/

	int ret_code;
	sa_trans_cntx->file_path = NULL;
	sa_trans_cntx->err_str = NULL; 
	sa_trans_cntx->fmt_cntx = NULL;
	sa_trans_cntx->de_cntx = NULL;
	sa_trans_cntx->en_cntx = NULL;
	sa_trans_cntx->rest_buf = NULL;

	//Register formats and codecs
	av_register_all();
	avcodec_register_all();

	//Create 256K ring buffer
	if(!(sa_trans_cntx->rest_buf = ringbuf_new(256000))){
		sa_decode_set_err(sa_trans_cntx, "malloc()", "Could not allocate ring buffer");
		return 1;
	}
	sa_trans_cntx->poll_nxt_time = 0;


	/*
	*	Setup for decoding the input file
	*/

	//Create filename with protocol
	ret_code = asprintf(&sa_trans_cntx->file_path, "file:%s", f_name);
	if(ret_code == -1){
		sa_decode_set_err(sa_trans_cntx, "asprintf()", "Could not create file path");
		return 1;
	}

	//Allocate format context
	if(!(sa_trans_cntx->fmt_cntx = avformat_alloc_context())){
		sa_decode_set_err(sa_trans_cntx, "avformat_alloc_context()", "Could not allocate space.");
		return 1;
	}

	//Open file
	ret_code = avformat_open_input(&(sa_trans_cntx->fmt_cntx), sa_trans_cntx->file_path, NULL, NULL);
	if(ret_code < 0){
		sa_decode_set_err(sa_trans_cntx, "avformat_open_input()", av_err2str(ret_code));
		return 1;
	}

	//Get audio streams and info
	ret_code = avformat_find_stream_info(sa_trans_cntx->fmt_cntx, NULL);
	if(ret_code < 0){
		sa_decode_set_err(sa_trans_cntx, "avformat_find_stream_info()", av_err2str(ret_code));
		return 1;
	}

	//Allocate decoder context
	if(!(sa_trans_cntx->de_cntx = avcodec_alloc_context3(NULL))){
		sa_decode_set_err(sa_trans_cntx, "avcodec_alloc_context3()", "Could not allocate");
		return 1;
	}

	sa_trans_cntx->de_cntx->request_sample_fmt = AV_SAMPLE_FMT_S16;

	//find decoder for the input file we assume that the file only has 1 stream and find the codec of the stream
	if(!(sa_trans_cntx->de_codec = avcodec_find_decoder(sa_trans_cntx->fmt_cntx->streams[0]->codecpar->codec_id))){
		sa_decode_set_err(sa_trans_cntx, "avcodec_find_decoder()", "Could not find decoder");
		return 1;
	}

	//Set the parameters of the decoder codec
	ret_code = avcodec_parameters_to_context(sa_trans_cntx->de_cntx, sa_trans_cntx->fmt_cntx->streams[0]->codecpar);
	if(ret_code < 0){
		sa_decode_set_err(sa_trans_cntx, "avcodec_parameters_to_context()", av_err2str(ret_code));
		return -1;
	}
	
	//Set the codec in the context
	ret_code = avcodec_open2(sa_trans_cntx->de_cntx, sa_trans_cntx->de_codec, NULL);
	if(ret_code < 0){
		sa_decode_set_err(sa_trans_cntx, "avcodec_open2()", av_err2str(ret_code));
		return 1;
	}


	/*
	*	Setup for encoding to PCM
	*/

	//Allocate the PCM encoder context
	if(!(sa_trans_cntx->en_cntx = avcodec_alloc_context3(NULL))){
		sa_decode_set_err(sa_trans_cntx, "avcodec_alloc_context3()", "Could not allocate");
		return 1;
	}

	//Set sample rate and channels
	sa_trans_cntx->en_cntx->sample_fmt = AV_SAMPLE_FMT_S16;
	sa_trans_cntx->en_cntx->sample_rate = 44100;
	sa_trans_cntx->en_cntx->channels = 2;

	//Find the PCM encoder AV_CODEC_ID_PCM_S16LE
	if(!(sa_trans_cntx->en_codec = avcodec_find_encoder(AV_CODEC_ID_PCM_S16LE))){
		sa_decode_set_err(sa_trans_cntx, "avcodec_find_encoder()", "Could not find encoder");
		return 1;
	}

	//Set the decoding codec in the context
	ret_code = avcodec_open2(sa_trans_cntx->en_cntx, sa_trans_cntx->en_codec, NULL);
	if(ret_code < 0){
		sa_decode_set_err(sa_trans_cntx, "avcodec_open2()", av_err2str(ret_code));
		return 1;
	}

	return 0;
}

int sa_decode_transcode(sa_transcode_context_t *sa_trans_cntx, size_t nbytes){
	AVPacket *snd_pkt, *rcv_pkt;
	AVFrame *frame;
	void *no_fucks;
	int ret_code, ret_snd_pkt, ret_rcv_frame, ret_snd_frame, ret_rcv_pkt;
	//fprintf(stdout, "Beginning of func %li b in Buffer\n", ringbuf_bytes_used(sa_trans_cntx->rest_buf));
	do{
		if(!(snd_pkt = av_packet_alloc())){
			sa_decode_set_err(sa_trans_cntx, "av_packet_alloc()", "Could not be allocated");
			return 1;
		}

		if(ringbuf_bytes_used(sa_trans_cntx->rest_buf) > nbytes){
			//fprintf(stdout, "%lu b in Buffer\n", ringbuf_bytes_used(sa_trans_cntx->rest_buf));
			if(!(sa_trans_cntx->tmp_buf = malloc(nbytes))){
				sa_decode_set_err(sa_trans_cntx, "malloc()", "Malloc failed");
				return 1;
			}
			if(!(ringbuf_memcpy_from(sa_trans_cntx->tmp_buf, sa_trans_cntx->rest_buf, nbytes))){
				sa_decode_set_err(sa_trans_cntx, "ringbuf_memcpy_into()", "Reading from ring buffer failed");
				return 1;
			}
			break;
		}

		if(av_read_frame(sa_trans_cntx->fmt_cntx, snd_pkt)){
			ret_code = avformat_seek_file(sa_trans_cntx->fmt_cntx, 0, 0, 0, 400, AVSEEK_FORCE | AVSEEK_FLAG_BYTE);
			if(ret_code < 0){
				sa_decode_set_err(sa_trans_cntx, "avformat_seek_file()", av_err2str(ret_code));
				return 1;
			}

			ret_code = av_read_frame(sa_trans_cntx->fmt_cntx, snd_pkt);
			if(ret_code < 0){
				sa_decode_set_err(sa_trans_cntx, "av_read_frame()", "Didn't expect EOF after seeking");
				return 1;
			}

		}

		ret_snd_pkt = avcodec_send_packet(sa_trans_cntx->de_cntx, snd_pkt);
		if(ret_snd_pkt != 0){
			sa_decode_set_err(sa_trans_cntx, "avcodec_send_packet()", av_err2str(ret_snd_pkt));
			return 1;
		}

		frame = av_frame_alloc();
		while((ret_rcv_frame = avcodec_receive_frame(sa_trans_cntx->de_cntx, frame)) != AVERROR(EAGAIN)){
			if(ret_rcv_frame != 0){
				sa_decode_set_err(sa_trans_cntx, "avcodec_receive_frame()", av_err2str(ret_rcv_frame));
				return 1;
			}
			ret_snd_frame = avcodec_send_frame(sa_trans_cntx->en_cntx, frame);
			if(ret_snd_frame != 0){
				sa_decode_set_err(sa_trans_cntx, "avcodec_send_frame()", av_err2str(ret_snd_frame));
				return 1;
			}

			if(!(rcv_pkt = av_packet_alloc())){
				sa_decode_set_err(sa_trans_cntx, "av_packet_alloc()", "Could not be allocated");
				return 1;
			}

			ret_rcv_pkt = avcodec_receive_packet(sa_trans_cntx->en_cntx, rcv_pkt);
			if(ret_rcv_pkt != 0){
				sa_decode_set_err(sa_trans_cntx, "avcodec_receive_packet()", av_err2str(ret_rcv_pkt));
				return 1;
			}
			if(ringbuf_bytes_free(sa_trans_cntx->rest_buf) < rcv_pkt->size){
				sa_decode_set_err(sa_trans_cntx, "sa_decode_transcode()", "Ring buffer to small. Packet does not fit");
				return 1;
			}

			if(!(ringbuf_memcpy_into(sa_trans_cntx->rest_buf, rcv_pkt->data, rcv_pkt->size))){
				sa_decode_set_err(sa_trans_cntx, "ringbuf_memcpy_into()", "Could not copy data to ringbuffer");
				return 1;
			}

			av_packet_free(&rcv_pkt);

		}
		av_frame_free(&frame);
		av_packet_free(&snd_pkt);
	}while(1);

	av_packet_free(&snd_pkt);

	return 0;
}