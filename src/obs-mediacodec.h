#pragma once

#include <obs-module.h>
#include <media/NdkMediaCodec.h>
#include <media/NdkMediaFormat.h>
#include <android/native_window.h>

/* ------------------------------------------------------------------------- */
/* Main Implementation Structure */

struct mediacodec_encoder {
	obs_encoder_t *encoder;
	AMediaCodec *codec;
	AMediaFormat *format;
	ANativeWindow *window;

	int width;
	int height;

	uint8_t *extra_data;
	size_t extra_data_size;

	uint8_t *packet_buffer;
	size_t packet_buffer_size;

	int64_t pts_queue[256];
	int pts_head;
	int pts_tail;
	
	bool eos_signaled;
};

/* ------------------------------------------------------------------------- */
/* Core Engine Functions */

void *mediacodec_create_internal(obs_data_t *settings, obs_encoder_t *encoder, const char *mime);
void mediacodec_destroy(void *data);
bool mediacodec_encode(void *data, struct encoder_frame *frame, struct encoder_packet *packet, bool *received_packet);
bool mediacodec_get_extra_data(void *data, uint8_t **extra_data, size_t *size);
void mediacodec_get_video_info(void *data, struct video_scale_info *info);

/* ------------------------------------------------------------------------- */
/* Properties */

void mediacodec_get_defaults(obs_data_t *settings);
obs_properties_t *mediacodec_get_properties(void *unused);
