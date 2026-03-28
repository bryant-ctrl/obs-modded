#include <obs-module.h>
#include <obs-frontend-api.h>
#include <util/config-file.h>
#include <util/platform.h>
#include "util/threading.h"
#include "shared-memory-queue.h"

/* Scene name read from module config at start time */
#define VCAM1_CONFIG_SECTION "VirtualCam"
#define VCAM1_CONFIG_KEY "Scene1"
#define VCAM1_CONFIG_DEFAULT "Camera1"
#define VCAM1_QUEUE_NAME L"OBSVirtualCamVideo"
#define VCAM1_RES_FILE "obs-virtualcam.txt"

struct virtualcam_data {
	obs_output_t *output;
	video_queue_t *vq;
	obs_view_t *view;
	volatile bool active;
	volatile bool stopping;
};

static const char *virtualcam_name(void *unused)
{
	UNUSED_PARAMETER(unused);
	return "Virtual Camera Output";
}

static void virtualcam_destroy(void *data)
{
	struct virtualcam_data *vcam = (struct virtualcam_data *)data;
	video_queue_close(vcam->vq);
	if (vcam->view) {
		obs_view_remove(vcam->view);
		obs_view_destroy(vcam->view);
	}
	bfree(data);
}

static void *virtualcam_create(obs_data_t *settings, obs_output_t *output)
{
	struct virtualcam_data *vcam =
		(struct virtualcam_data *)bzalloc(sizeof(*vcam));
	vcam->output = output;

	UNUSED_PARAMETER(settings);
	return vcam;
}

static bool virtualcam_start(void *data)
{
	struct virtualcam_data *vcam = (struct virtualcam_data *)data;

	/* Look up which scene the user assigned to Camera 1 */
	const char *scene_name = NULL;
	config_t *config = obs_frontend_get_profile_config();
	if (config) {
		config_set_default_string(config, VCAM1_CONFIG_SECTION,
					  VCAM1_CONFIG_KEY,
					  VCAM1_CONFIG_DEFAULT);
		scene_name = config_get_string(config, VCAM1_CONFIG_SECTION,
					       VCAM1_CONFIG_KEY);
	}
	if (!scene_name || !*scene_name)
		scene_name = VCAM1_CONFIG_DEFAULT;

	/* Create an independent view for the target scene */
	vcam->view = obs_view_create();
	obs_source_t *scene_source = obs_get_source_by_name(scene_name);
	if (scene_source) {
		obs_view_set_source(vcam->view, 0, scene_source);
		obs_source_release(scene_source);
	} else {
		blog(LOG_WARNING,
		     "VirtualCam1: scene '%s' not found, using program output",
		     scene_name);
	}

	/* Attach independent video pipeline to the output */
	video_t *scene_video = obs_view_add(vcam->view);
	if (scene_video)
		obs_output_set_media(vcam->output, scene_video,
				     obs_get_audio());

	uint32_t width = obs_output_get_width(vcam->output);
	uint32_t height = obs_output_get_height(vcam->output);

	struct obs_video_info ovi;
	obs_get_video_info(&ovi);

	uint64_t interval = ovi.fps_den * 10000000ULL / ovi.fps_num;

	char res[64];
	snprintf(res, sizeof(res), "%dx%dx%lld", (int)width, (int)height,
		 (long long)interval);

	char *res_file = os_get_config_path_ptr(VCAM1_RES_FILE);
	os_quick_write_utf8_file_safe(res_file, res, strlen(res), false, "tmp",
				      NULL);
	bfree(res_file);

	vcam->vq = video_queue_create_named(width, height, interval,
					    VCAM1_QUEUE_NAME);
	if (!vcam->vq) {
		blog(LOG_WARNING, "VirtualCam1: starting virtual-output failed");
		obs_view_remove(vcam->view);
		obs_view_destroy(vcam->view);
		vcam->view = NULL;
		return false;
	}

	struct video_scale_info vsi = {0};
	vsi.format = VIDEO_FORMAT_NV12;
	vsi.width = width;
	vsi.height = height;
	obs_output_set_video_conversion(vcam->output, &vsi);

	os_atomic_set_bool(&vcam->active, true);
	os_atomic_set_bool(&vcam->stopping, false);
	blog(LOG_INFO, "VirtualCam1: output started (scene: %s)", scene_name);
	obs_output_begin_data_capture(vcam->output, 0);
	return true;
}

static void virtualcam_deactive(struct virtualcam_data *vcam)
{
	obs_output_end_data_capture(vcam->output);
	video_queue_close(vcam->vq);
	vcam->vq = NULL;

	if (vcam->view) {
		obs_view_remove(vcam->view);
		obs_view_destroy(vcam->view);
		vcam->view = NULL;
	}

	os_atomic_set_bool(&vcam->active, false);
	os_atomic_set_bool(&vcam->stopping, false);

	blog(LOG_INFO, "VirtualCam1: output stopped");
}

static void virtualcam_stop(void *data, uint64_t ts)
{
	struct virtualcam_data *vcam = (struct virtualcam_data *)data;
	os_atomic_set_bool(&vcam->stopping, true);

	blog(LOG_INFO, "VirtualCam1: output stopping");

	UNUSED_PARAMETER(ts);
}

static void virtual_video(void *param, struct video_data *frame)
{
	struct virtualcam_data *vcam = (struct virtualcam_data *)param;

	if (!vcam->vq)
		return;

	if (!os_atomic_load_bool(&vcam->active))
		return;

	if (os_atomic_load_bool(&vcam->stopping)) {
		virtualcam_deactive(vcam);
		return;
	}

	video_queue_write(vcam->vq, frame->data, frame->linesize,
			  frame->timestamp);
}

struct obs_output_info virtualcam_info = {
	.id = "virtualcam_output",
	.flags = OBS_OUTPUT_VIDEO,
	.get_name = virtualcam_name,
	.create = virtualcam_create,
	.destroy = virtualcam_destroy,
	.start = virtualcam_start,
	.stop = virtualcam_stop,
	.raw_video = virtual_video,
};
