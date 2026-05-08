#include <obs-module.h>
#include <util/dstr.h>
#include <ctype.h>
#include <dlfcn.h>

#include "obs-mediacodec.h"

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("obs-mediacodec", "en-US")

MODULE_EXPORT const char *obs_module_description(void)
{
	return "Android NDK MediaCodec Hardware Encoder for OBS Studio";
}

/* ------------------------------------------------------------------------- */
/* Dynamic Registry Structures */

struct dynamic_encoder {
	char id[128];
	char ui_name[128];
	char codec_type[32];
	char mime[64];
	struct obs_encoder_info info;
};

#define MAX_ENCODERS 3
static struct dynamic_encoder active_encoders[MAX_ENCODERS];
static size_t num_encoders = 0;

/* ------------------------------------------------------------------------- */
/* Helper Functions */

static bool is_hardware_encoder(const char *name)
{
	char ln[128] = {0};
	strncpy(ln, name, sizeof(ln) - 1);
	for (int i = 0; ln[i]; i++)
		ln[i] = tolower((unsigned char)ln[i]);

	if (strstr(ln, "google") || strstr(ln, "android") || strstr(ln, "c2.android") ||
	    strstr(ln, "soft") || strstr(ln, ".sw.") ||
	    (strlen(ln) > 2 && strcmp(ln + strlen(ln) - 2, "sw") == 0)) {
		return false;
	}
	return true;
}

static const char *get_vendor(const char *name)
{
	char ln[128] = {0};
	strncpy(ln, name, sizeof(ln) - 1);
	for (int i = 0; ln[i]; i++)
		ln[i] = tolower((unsigned char)ln[i]);

	if (strstr(ln, "qcom") || strstr(ln, "qti"))
		return "Qualcomm";
	if (strstr(ln, "exynos") || strstr(ln, "samsung"))
		return "Samsung";
	if (strstr(ln, "mtk") || strstr(ln, "mediatek"))
		return "MediaTek";
	if (strstr(ln, "hisi") || strstr(ln, "kirin"))
		return "HiSilicon";
	if (strstr(ln, "tegra") || strstr(ln, "nvidia"))
		return "Nvidia";

	return "Generic";
}

/* ------------------------------------------------------------------------- */
/* Dynamic NDK Linking Wrappers */

static void safe_getName(AMediaCodec *codec, char **out_name)
{
	void *lib = dlopen("libmediandk.so", RTLD_LAZY);
	if (lib) {
		typedef media_status_t (*func_t)(AMediaCodec*, char**);
		func_t getName = (func_t)dlsym(lib, "AMediaCodec_getName");
		if (getName) {
			getName(codec, out_name);
		}
		dlclose(lib);
	}
}

static void safe_releaseName(AMediaCodec *codec, char *name)
{
	void *lib = dlopen("libmediandk.so", RTLD_LAZY);
	if (lib) {
		typedef void (*func_t)(AMediaCodec*, char*);
		func_t releaseName = (func_t)dlsym(lib, "AMediaCodec_releaseName");
		if (releaseName) {
			releaseName(codec, name);
		}
		dlclose(lib);
	}
}

/* ------------------------------------------------------------------------- */
/* OBS Encoder Callbacks */

static const char *dynamic_get_name(void *type_data)
{
	struct dynamic_encoder *enc = type_data;
	return enc ? enc->ui_name : "Android MediaCodec";
}

static void *dynamic_create(obs_data_t *settings, obs_encoder_t *encoder)
{
	const char *target_id = obs_encoder_get_id(encoder);

	for (size_t i = 0; i < num_encoders; i++) {
		if (strcmp(active_encoders[i].id, target_id) == 0) {
			return mediacodec_create_internal(settings, encoder, active_encoders[i].mime);
		}
	}

	return NULL;
}

/* ------------------------------------------------------------------------- */
/* Module Load & Probe */

static void probe_and_register(const char *mime, const char *codec_type, const char *ui_prefix)
{
	if (num_encoders >= MAX_ENCODERS)
		return;

	AMediaCodec *codec = AMediaCodec_createEncoderByType(mime);
	if (!codec)
		return;

	bool is_hw = true;
	const char *vendor = "UNKNOWN";
	char *name = NULL;

	safe_getName(codec, &name);

	if (name) {
		is_hw = is_hardware_encoder(name);
		vendor = get_vendor(name);
	} else {
		if (strcmp(mime, "video/avc") == 0) {
			blog(LOG_INFO, "[obs-mediacodec] API < 28 detected. Assuming '%s' is hardware.", mime);
			is_hw = true;
		} else {
			is_hw = false;
		}
	}

	if (is_hw) {
		struct dynamic_encoder *enc = &active_encoders[num_encoders++];

		strncpy(enc->mime, mime, sizeof(enc->mime) - 1);
		strncpy(enc->codec_type, codec_type, sizeof(enc->codec_type) - 1);

		snprintf(enc->id, sizeof(enc->id), "mediacodec_%s_%s", enc->codec_type, vendor);
		snprintf(enc->ui_name, sizeof(enc->ui_name), "MediaCodec %s (%s)", ui_prefix, vendor);

		enc->info.id = enc->id;
		enc->info.type = OBS_ENCODER_VIDEO;
		enc->info.codec = enc->codec_type;
		enc->info.get_name = dynamic_get_name;
		enc->info.create = dynamic_create;
		enc->info.destroy = mediacodec_destroy;
		enc->info.encode = mediacodec_encode;
		enc->info.get_video_info = mediacodec_get_video_info;
		enc->info.get_extra_data = mediacodec_get_extra_data;
		enc->info.get_defaults = mediacodec_get_defaults;
		enc->info.get_properties = mediacodec_get_properties;
		enc->info.type_data = enc;

		obs_register_encoder(&enc->info);
	}

	if (name) {
		safe_releaseName(codec, name);
	}

	AMediaCodec_delete(codec);
}

bool obs_module_load(void)
{
	probe_and_register("video/avc", "h264", "H.264");
	probe_and_register("video/hevc", "hevc", "HEVC");
	probe_and_register("video/av01", "av1", "AV1");

	blog(LOG_INFO, "[obs-mediacodec] Successfully registered %zu hardware encoders.", num_encoders);

	return true;
}

void obs_module_unload(void)
{
	num_encoders = 0;
}
