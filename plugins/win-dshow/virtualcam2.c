#include <obs-module.h>
#include <obs-frontend-api.h>
#include <util/config-file.h>
#include <util/platform.h>
#include "util/threading.h"
#include "shared-memory-queue.h"

#define VCAM2_CONFIG_SECTION "VirtualCam"
#define VCAM2_CONFIG_KEY "Scene2"
#define VCAM2_CONFIG_DEFAULT "Camera2"
#define VCAM2_QUEUE_NAME L"OBSVirtualCamVideo2"
#define VCAM2_RES_FILE "obs-virtualcam2.txt"

struct virtualcam2_data {
	obs_output_t *output;
	video_queue_t *vq;
	obs_view_t *view;
	volatile bool active;
	volatile bool stopping;
};

static const char *virtualcam2_name(void *unused)
{
	UNUSED_PARAMETER(unused);
	return "Virtual Camera 2 Output";
}

static void virtualcam2_destroy(void *data)
{
	struct virtualcam2_data *vcam = (struct virtualcam2_data *)data;
	video_queue_close(vcam->vq);
	if (vcam->view) {
		obs_view_remove(vcam->view);
		obs_view_destroy(vcam->view);
	}
	bfree(data);
}

static void *virtualcam2_create(obs_data_t *settings, obs_output_t *output)
{
	struct virtualcam2_data *vcam =
		(struct virtualcam2_data *)bzalloc(sizeof(*vcam));
	vcam->output = output;

	UNUSED_PARAMETER(settings);
	return vcam;
}

static bool virtualcam2_start(void *data)
{
	struct virtualcam2_data *vcam = (struct virtualcam2_data *)data;

	/* Get base video info first — always valid */
	struct obs_video_info ovi;
	if (!obs_get_video_info(&ovi)) {
		blog(LOG_WARNING,
		     "VirtualCam2: OBS video not initialized");
		return false;
	}

	uint32_t width = ovi.output_width;
	uint32_t height = ovi.output_height;
	uint64_t interval = ovi.fps_den * 10000000ULL / ovi.fps_num;

	/* Look up which scene the user assigned to Camera 2 */
	const char *scene_name = NULL;
	config_t *config = obs_frontend_get_profile_config();
	if (config) {
		config_set_default_string(config, VCAM2_CONFIG_SECTION,
					 VCAM2_CONFIG_KEY,
					 VCAM2_CONFIG_DEFAULT);
		scene_name = config_get_string(config, VCAM2_CONFIG_SECTION,
					      VCAM2_CONFIG_KEY);
	}
	if (!scene_name || !*scene_name)
		scene_name = VCAM2_CONFIG_DEFAULT;

	/* Try to find the named scene; fall back to current scene */
	obs_source_t *scene_source = obs_get_source_by_name(scene_name);
	if (!scene_source) {
		scene_source = obs_frontend_get_current_scene();
		blog(LOG_INFO,
		     "VirtualCam2: scene '%s' not found, using current scene",
		     scene_name);
	}

	/* Create an independent view so this camera renders its own scene */
	vcam->view = obs_view_create();
	if (scene_source) {
		obs_view_set_source(vcam->view, 0, scene_source);
		obs_source_release(scene_source);
	}

	video_t *view_video = obs_view_add(vcam->view);
	if (view_video) {
		obs_output_set_media(vcam->output, view_video,
				     obs_get_audio());
	} else {
		blog(LOG_INFO,
		     "VirtualCam2: obs_view_add failed, using main output");
		obs_view_destroy(vcam->view);
		vcam->view = NULL;
	}

	/* Write resolution file for the DirectShow filter */
	char res[64];
	snprintf(res, sizeof(res), "%dx%dx%lld", (int)width, (int)height,
		 (long long)interval);

	char *res_file = os_get_config_path_ptr(VCAM2_RES_FILE);
	os_quick_write_utf8_file_safe(res_file, res, strlen(res), false, "tmp",
				     NULL);
	bfree(res_file);

	vcam->vq = video_queue_create_named(width, height, interval,
					   VCAM2_QUEUE_NAME);
	if (!vcam->vq) {
		blog(LOG_WARNING, "VirtualCam2: starting virtual-output failed");
		if (vcam->view) {
			obs_view_remove(vcam->view);
			obs_view_destroy(vcam->view);
			vcam->view = NULL;
		}
		return false;
	}

	struct video_scale_info vsi = {0};
	vsi.format = VIDEO_FORMAT_NV12;
	vsi.width = width;
	vsi.height = height;
	obs_output_set_video_conversion(vcam->output, &vsi);

	os_atomic_set_bool(&vcam->active, true);
	os_atomic_set_bool(&vcam->stopping, false);
	blog(LOG_INFO, "VirtualCam2: output started (%dx%d, scene: %s)",
	     width, height, scene_name);
	obs_output_begin_data_capture(vcam->output, 0);
	return true;
}

static void virtualcam2_deactive(struct virtualcam2_data *vcam)
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

	blog(LOG_INFO, "VirtualCam2: output stopped");
}

static void virtualcam2_stop(void *data, uint64_t ts)
{
	struct virtualcam2_data *vcam = (struct virtualcam2_data *)data;
	os_atomic_set_bool(&vcam->stopping, true);

	blog(LOG_INFO, "VirtualCam2: output stopping");

	UNUSED_PARAMETER(ts);
}

static void virtual_video2(void *param, struct video_data *frame)
{
	struct virtualcam2_data *vcam = (struct virtualcam2_data *)param;

	if (!vcam->vq)
		return;

	if (!os_atomic_load_bool(&vcam->active))
		return;

	if (os_atomic_load_bool(&vcam->stopping)) {
		virtualcam2_deactive(vcam);
		return;
	}

	video_queue_write(vcam->vq, frame->data, frame->linesize,
			 frame->timestamp);
}

struct obs_output_info virtualcam2_info = {
	.id = "virtualcam2_output",
	.flags = OBS_OUTPUT_VIDEO,
	.get_name = virtualcam2_name,
	.create = virtualcam2_create,
	.destroy = virtualcam2_destroy,
	.start = virtualcam2_start,
	.stop = virtualcam2_stop,
	.raw_video = virtual_video2,
};
