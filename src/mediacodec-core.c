#include "obs-mediacodec.h"

/* ------------------------------------------------------------------------- */
/* Initialization */

void *mediacodec_create_internal(obs_data_t *settings, obs_encoder_t *encoder, const char *mime)
{
	struct mediacodec_encoder *ctx = bzalloc(sizeof(*ctx));
	ctx->encoder = encoder;
	ctx->eos_signaled = false;

	video_t *video = obs_encoder_video(encoder);
	const struct video_output_info *voi = video_output_get_info(video);

	ctx->width = obs_encoder_get_width(encoder);
	ctx->height = obs_encoder_get_height(encoder);

	int bitrate = (int)(obs_data_get_int(settings, "bitrate") * 1000);
	int max_bitrate = (int)(obs_data_get_int(settings, "max_bitrate") * 1000);
	int keyint = (int)obs_data_get_int(settings, "keyint_sec");
	if (keyint <= 0) {
		keyint = 2;
	}

	const char *rc = obs_data_get_string(settings, "rate_control");
	int rc_mode = 2;
	if (strcmp(rc, "VBR") == 0) rc_mode = 1;

	ctx->codec = AMediaCodec_createEncoderByType(mime);
	if (!ctx->codec) {
		goto fail;
	}

	ctx->format = AMediaFormat_new();
	AMediaFormat_setString(ctx->format, "mime", mime);
	AMediaFormat_setInt32(ctx->format, "width", ctx->width);
	AMediaFormat_setInt32(ctx->format, "height", ctx->height);
	AMediaFormat_setInt32(ctx->format, "bitrate", bitrate);
	AMediaFormat_setInt32(ctx->format, "frame-rate", voi->fps_num / voi->fps_den);
	AMediaFormat_setInt32(ctx->format, "i-frame-interval", keyint);
	AMediaFormat_setInt32(ctx->format, "color-format", 0x7F000789);
	AMediaFormat_setInt32(ctx->format, "bitrate-mode", rc_mode);
	if (rc_mode == 1) {
		AMediaFormat_setInt32(ctx->format, "max-bitrate", max_bitrate);
	}

	if (AMediaCodec_configure(ctx->codec, ctx->format, NULL, NULL, AMEDIACODEC_CONFIGURE_FLAG_ENCODE) != AMEDIA_OK) {
		AMediaFormat_setInt32(ctx->format, "bitrate-mode", 0);
		if (AMediaCodec_configure(ctx->codec, ctx->format, NULL, NULL, AMEDIACODEC_CONFIGURE_FLAG_ENCODE) != AMEDIA_OK) {
			goto fail;
		}
	}

	if (AMediaCodec_createInputSurface(ctx->codec, &ctx->window) != AMEDIA_OK || !ctx->window) {
		goto fail;
	}

	ANativeWindow_setBuffersGeometry(ctx->window, ctx->width, ctx->height, WINDOW_FORMAT_RGBA_8888);

	blog(LOG_INFO, "[obs-mediacodec] Initialized native hardware surface for %s", mime);

	if (AMediaCodec_start(ctx->codec) != AMEDIA_OK) {
		goto fail;
	}

	ctx->pts_head = 0;
	ctx->pts_tail = 0;

	return ctx;

fail:
	if (ctx->window)
		ANativeWindow_release(ctx->window);
	if (ctx->codec) {
		AMediaCodec_stop(ctx->codec);
		AMediaCodec_delete(ctx->codec);
	}
	if (ctx->format)
		AMediaFormat_delete(ctx->format);

	bfree(ctx);
	return NULL;
}

void mediacodec_destroy(void *data)
{
	struct mediacodec_encoder *ctx = data;
	if (!ctx)
		return;

	if (ctx->window)
		ANativeWindow_release(ctx->window);
	if (ctx->codec) {
		AMediaCodec_stop(ctx->codec);
		AMediaCodec_delete(ctx->codec);
	}
	if (ctx->format)
		AMediaFormat_delete(ctx->format);
	if (ctx->extra_data)
		bfree(ctx->extra_data);
	if (ctx->packet_buffer)
		bfree(ctx->packet_buffer);

	bfree(ctx);
}

/* ------------------------------------------------------------------------- */
/* Encoding Core */

bool mediacodec_encode(void *data, struct encoder_frame *frame, struct encoder_packet *packet, bool *received_packet)
{
	struct mediacodec_encoder *ctx = data;
	*received_packet = false;

	if (!frame) {
		if (ctx->window && !ctx->eos_signaled) {
			AMediaCodec_signalEndOfInputStream(ctx->codec);
			ctx->eos_signaled = true;
		}
	} else if (ctx->window) {
		ANativeWindow_Buffer out_buffer;
		if (ANativeWindow_lock(ctx->window, &out_buffer, NULL) == 0) {
			uint8_t *dst = (uint8_t *)out_buffer.bits;
			uint8_t *src = frame->data[0];
			int dst_stride = out_buffer.stride * 4;
			int src_stride = frame->linesize[0];

			if (dst_stride == src_stride) {
				memcpy(dst, src, src_stride * ctx->height);
			} else {
				for (int y = 0; y < ctx->height; y++) {
					memcpy(dst + (y * dst_stride), src + (y * src_stride), ctx->width * 4);
				}
			}
			ANativeWindow_unlockAndPost(ctx->window);

			ctx->pts_queue[ctx->pts_head] = frame->pts;
			ctx->pts_head = (ctx->pts_head + 1) % 256;
		}
	}

	int64_t timeout_us = frame ? 0 : 10000; 
	int flush_retries = 0;

	while (true) {
		AMediaCodecBufferInfo info;
		ssize_t out_idx = AMediaCodec_dequeueOutputBuffer(ctx->codec, &info, timeout_us);

		if (out_idx == AMEDIACODEC_INFO_TRY_AGAIN_LATER) {
			if (!frame) {
				flush_retries++;
				if (flush_retries > 50) { 
					blog(LOG_WARNING, "[obs-mediacodec] Flush timed out waiting for End Of Stream.");
					break;
				}
				continue;
			}
			break; 
		} else if (out_idx == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED) {
			AMediaFormat *new_format = AMediaCodec_getOutputFormat(ctx->codec);
			if (new_format && !ctx->extra_data) {
				void *csd0_data = NULL;
				size_t csd0_size = 0;
				void *csd1_data = NULL;
				size_t csd1_size = 0;

				AMediaFormat_getBuffer(new_format, "csd-0", &csd0_data, &csd0_size);
				AMediaFormat_getBuffer(new_format, "csd-1", &csd1_data, &csd1_size);

				if (csd0_size > 0) {
					if (ctx->extra_data) {
						bfree(ctx->extra_data);
						ctx->extra_data = NULL;
					}
					ctx->extra_data_size = csd0_size + csd1_size;
					ctx->extra_data = bmalloc(ctx->extra_data_size);
					memcpy(ctx->extra_data, csd0_data, csd0_size);
					if (csd1_size > 0)
						memcpy(ctx->extra_data + csd0_size, csd1_data, csd1_size);
				}
				AMediaFormat_delete(new_format);
			}
			continue; 
		} else if (out_idx >= 0) {
			
			if (info.flags & AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM) {
				AMediaCodec_releaseOutputBuffer(ctx->codec, out_idx, false);
				break;
			}

			size_t out_size;
			uint8_t *out_buf = AMediaCodec_getOutputBuffer(ctx->codec, out_idx, &out_size);

			if (out_buf && info.size > 0) {
				if (info.flags & AMEDIACODEC_BUFFER_FLAG_CODEC_CONFIG) {
					if (ctx->extra_data) {
						bfree(ctx->extra_data);
						ctx->extra_data = NULL;
					}
					ctx->extra_data = bmemdup(out_buf + info.offset, info.size);
					ctx->extra_data_size = info.size;
					
					AMediaCodec_releaseOutputBuffer(ctx->codec, out_idx, false);
					continue; 
				} else {
					if (!ctx->extra_data && (info.flags & AMEDIACODEC_BUFFER_FLAG_KEY_FRAME)) {
						AMediaFormat *fmt = AMediaCodec_getOutputFormat(ctx->codec);
						if (fmt) {
							void *csd0 = NULL;
							size_t csd0_sz = 0;
							void *csd1 = NULL;
							size_t csd1_sz = 0;

							AMediaFormat_getBuffer(fmt, "csd-0", &csd0, &csd0_sz);
							AMediaFormat_getBuffer(fmt, "csd-1", &csd1, &csd1_sz);

							if (csd0_sz > 0) {
								ctx->extra_data_size = csd0_sz + csd1_sz;
								ctx->extra_data = bmalloc(ctx->extra_data_size);
								memcpy(ctx->extra_data, csd0, csd0_sz);
								if (csd1_sz > 0)
									memcpy(ctx->extra_data + csd0_sz, csd1, csd1_sz);
							}
							AMediaFormat_delete(fmt);
						}
					}

					if (ctx->extra_data) {
						if (info.size > ctx->packet_buffer_size) {
							ctx->packet_buffer = brealloc(ctx->packet_buffer, info.size);
							ctx->packet_buffer_size = info.size;
						}
						memcpy(ctx->packet_buffer, out_buf + info.offset, info.size);

						int64_t correct_pts = ctx->pts_queue[ctx->pts_tail];
						ctx->pts_tail = (ctx->pts_tail + 1) % 256;

						packet->type = OBS_ENCODER_VIDEO;
						packet->pts = correct_pts;
						packet->dts = correct_pts;
						packet->data = ctx->packet_buffer;
						packet->size = info.size;
						packet->keyframe = (info.flags & AMEDIACODEC_BUFFER_FLAG_KEY_FRAME) != 0;
						*received_packet = true;
					} else {
						blog(LOG_WARNING, "[obs-mediacodec] Dropping frame: Waiting for codec configuration headers");
					}
				}
			}
			
			AMediaCodec_releaseOutputBuffer(ctx->codec, out_idx, false);
			
			if (*received_packet || !ctx->extra_data) {
				break; 
			}
		} else {
			break; 
		}
	}

	return true;
}

/* ------------------------------------------------------------------------- */
/* OBS Properties & Callbacks */

bool mediacodec_get_extra_data(void *data, uint8_t **extra_data, size_t *size)
{
	struct mediacodec_encoder *ctx = data;
	if (!ctx->extra_data)
		return false;

	*extra_data = ctx->extra_data;
	*size = ctx->extra_data_size;
	return true;
}

void mediacodec_get_video_info(void *data, struct video_scale_info *info)
{
	UNUSED_PARAMETER(data);
	info->format = VIDEO_FORMAT_RGBA;
}

void mediacodec_get_defaults(obs_data_t *settings)
{
	obs_data_set_default_string(settings, "rate_control", "CBR");
	obs_data_set_default_int(settings, "bitrate", 6000);
	obs_data_set_default_int(settings, "max_bitrate", 8000);
	obs_data_set_default_int(settings, "keyint_sec", 2);
}

static bool rate_control_modified(obs_properties_t *props, obs_property_t *p, obs_data_t *settings)
{
	const char *rc = obs_data_get_string(settings, "rate_control");
	bool is_vbr = (strcmp(rc, "VBR") == 0);

	obs_property_t *max_bitrate_prop = obs_properties_get(props, "max_bitrate");
	obs_property_set_visible(max_bitrate_prop, is_vbr);

	return true;
}

obs_properties_t *mediacodec_get_properties(void *unused)
{
	UNUSED_PARAMETER(unused);
	obs_properties_t *props = obs_properties_create();

	obs_property_t *rc_prop = obs_properties_add_list(props, "rate_control", obs_module_text("RateControl"), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(rc_prop, "CBR", "CBR");
	obs_property_list_add_string(rc_prop, "VBR", "VBR");
	obs_property_set_modified_callback(rc_prop, rate_control_modified);

	obs_properties_add_int(props, "bitrate", obs_module_text("Bitrate"), 50, 100000, 50);
	obs_properties_add_int(props, "max_bitrate", obs_module_text("MaxBitrate"), 50, 100000, 50);
	obs_properties_add_int(props, "keyint_sec", obs_module_text("KeyframeIntervalSec"), 1, 10, 1);

	return props;
}

