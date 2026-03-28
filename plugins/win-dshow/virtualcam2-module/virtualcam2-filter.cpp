#include "virtualcam2-filter.hpp"
#include "sleepto.h"

#include <shlobj_core.h>
#include <strsafe.h>
#include <inttypes.h>

using namespace DShow;

/* Use camera 2's dedicated shared memory queue */
#define VIDEO2_NAME L"OBSVirtualCamVideo2"
#define RES2_FILE L"\\obs-virtualcam2.txt"

extern bool initialize_placeholder();
extern const uint8_t *get_placeholder_ptr();
extern const bool get_placeholder_size(int *out_cx, int *out_cy);
extern volatile long locks2;

/* ========================================================================= */

VCamFilter2::VCamFilter2() : OutputFilter()
{
	thread_start = CreateEvent(nullptr, true, false, nullptr);
	thread_stop = CreateEvent(nullptr, true, false, nullptr);

	format = VideoFormat::NV12;

	placeholder.scaled_data = nullptr;

	/* ---------------------------------------- */
	/* detect if this filter is within obs      */

	wchar_t file[MAX_PATH];
	if (!GetModuleFileNameW(nullptr, file, MAX_PATH)) {
		file[0] = 0;
	}

#ifdef _WIN64
	const wchar_t *obs_process = L"obs64.exe";
#else
	const wchar_t *obs_process = L"obs32.exe";
#endif

	in_obs = !!wcsstr(file, obs_process);

	/* ---------------------------------------- */
	/* add last/current obs res/interval        */

	uint32_t new_obs_cx = obs_cx;
	uint32_t new_obs_cy = obs_cy;
	uint64_t new_obs_interval = obs_interval;

	vq = video_queue_open_named(VIDEO2_NAME);
	if (vq) {
		if (video_queue_state(vq) == SHARED_QUEUE_STATE_READY) {
			video_queue_get_info(vq, &new_obs_cx, &new_obs_cy,
					     &new_obs_interval);
		}
		video_queue_close(vq);
		vq = nullptr;
	} else {
		wchar_t res_file[MAX_PATH];
		SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr,
				 SHGFP_TYPE_CURRENT, res_file);
		StringCbCat(res_file, sizeof(res_file), RES2_FILE);

		HANDLE hfile = CreateFileW(res_file, GENERIC_READ, 0, nullptr,
					   OPEN_EXISTING, 0, nullptr);
		if (hfile) {
			char res[128];
			DWORD len = 0;

			if (ReadFile(hfile, res, sizeof(res) - 1, &len,
				     nullptr)) {
				res[len] = 0;
				int vals = sscanf(
					res,
					"%" PRIu32 "x%" PRIu32 "x%" PRIu64,
					&new_obs_cx, &new_obs_cy,
					&new_obs_interval);
				if (vals != 3) {
					new_obs_cx = obs_cx;
					new_obs_cy = obs_cy;
					new_obs_interval = obs_interval;
				}
			}
			CloseHandle(hfile);
		}
	}

	if (new_obs_cx != obs_cx || new_obs_cy != obs_cy ||
	    new_obs_interval != obs_interval) {
		AddVideoFormat(VideoFormat::NV12, new_obs_cx, new_obs_cy,
			       new_obs_interval);
		AddVideoFormat(VideoFormat::I420, new_obs_cx, new_obs_cy,
			       new_obs_interval);
		AddVideoFormat(VideoFormat::YUY2, new_obs_cx, new_obs_cy,
			       new_obs_interval);
		SetVideoFormat(VideoFormat::NV12, new_obs_cx, new_obs_cy,
			       new_obs_interval);

		obs_cx = new_obs_cx;
		obs_cy = new_obs_cy;
		obs_interval = new_obs_interval;
	}

	/* ---------------------------------------- */

	th = std::thread([this] { Thread(); });

	AddRef();
	os_atomic_inc_long(&locks2);
}

VCamFilter2::~VCamFilter2()
{
	SetEvent(thread_stop);
	if (th.joinable())
		th.join();
	video_queue_close(vq);

	if (placeholder.scaled_data)
		free(placeholder.scaled_data);

	os_atomic_dec_long(&locks2);
}

const wchar_t *VCamFilter2::FilterName() const
{
	return L"VCamFilter2";
}

STDMETHODIMP VCamFilter2::Pause()
{
	HRESULT hr;

	hr = OutputFilter::Pause();
	if (FAILED(hr)) {
		return hr;
	}

	os_atomic_set_bool(&active, true);
	SetEvent(thread_start);
	return S_OK;
}

STDMETHODIMP VCamFilter2::Run(REFERENCE_TIME tStart)
{
	os_atomic_set_bool(&active, true);
	return OutputFilter::Run(tStart);
}

STDMETHODIMP VCamFilter2::Stop()
{
	os_atomic_set_bool(&active, false);
	return OutputFilter::Stop();
}

inline uint64_t VCamFilter2::GetTime()
{
	if (!!clock) {
		REFERENCE_TIME rt;
		HRESULT hr = clock->GetTime(&rt);
		if (SUCCEEDED(hr)) {
			return (uint64_t)rt;
		}
	}

	return gettime_100ns();
}

void VCamFilter2::Thread()
{
	HANDLE h[2] = {thread_start, thread_stop};
	DWORD ret = WaitForMultipleObjects(2, h, false, INFINITE);
	if (ret != WAIT_OBJECT_0)
		return;

	uint64_t cur_time = gettime_100ns();
	uint64_t filter_time = GetTime();

	obs_cx = (uint32_t)GetCX();
	obs_cy = (uint32_t)GetCY();
	obs_interval = (uint64_t)GetInterval();
	filter_cx = obs_cx;
	filter_cy = obs_cy;

	if (initialize_placeholder()) {
		placeholder.source_data = get_placeholder_ptr();
		get_placeholder_size(&placeholder.cx, &placeholder.cy);
	} else {
		placeholder.source_data = nullptr;
	}

	placeholder.scaled_data = nullptr;

	nv12_scale_init(&scaler, TARGET_FORMAT_NV12, obs_cx, obs_cy, obs_cx,
			obs_cy);
	nv12_scale_init(&placeholder.scaler, TARGET_FORMAT_NV12, obs_cx,
			obs_cy, placeholder.cx, placeholder.cy);

	UpdatePlaceholder();

	while (!stopped()) {
		if (os_atomic_load_bool(&active))
			Frame(filter_time);
		sleepto_100ns(cur_time += obs_interval);
		filter_time += obs_interval;
	}
}

void VCamFilter2::Frame(uint64_t ts)
{
	uint32_t new_obs_cx = obs_cx;
	uint32_t new_obs_cy = obs_cy;
	uint64_t new_obs_interval = obs_interval;

	if (!vq) {
		vq = video_queue_open_named(VIDEO2_NAME);
	}

	enum queue_state state = video_queue_state(vq);
	if (state != prev_state) {
		if (state == SHARED_QUEUE_STATE_READY) {
			video_queue_get_info(vq, &new_obs_cx, &new_obs_cy,
					     &new_obs_interval);
		} else if (state == SHARED_QUEUE_STATE_STOPPING) {
			video_queue_close(vq);
			vq = nullptr;
		}

		prev_state = state;
	}

	uint32_t new_filter_cx = (uint32_t)GetCX();
	uint32_t new_filter_cy = (uint32_t)GetCY();

	if (state != SHARED_QUEUE_STATE_READY) {
		new_obs_cx = new_filter_cx;
		new_obs_cy = new_filter_cy;
		new_obs_interval = GetInterval();
	}

	if (new_obs_cx != obs_cx || new_obs_cy != obs_cy ||
	    new_obs_interval != obs_interval) {
		if (in_obs) {
			SetVideoFormat(GetVideoFormat(), new_obs_cx, new_obs_cy,
				       new_obs_interval);
			new_filter_cx = new_obs_cx;
			new_filter_cy = new_obs_cy;
		}

		nv12_scale_init(&scaler, scaler.format, new_filter_cx,
				new_filter_cy, new_obs_cx, new_obs_cy);

		obs_cx = new_obs_cx;
		obs_cy = new_obs_cy;
		obs_interval = new_obs_interval;
		filter_cx = new_filter_cx;
		filter_cy = new_filter_cy;

		UpdatePlaceholder();

	} else if (new_filter_cx != filter_cx || new_filter_cy != filter_cy) {
		filter_cx = new_filter_cx;
		filter_cy = new_filter_cy;

		nv12_scale_init(&scaler, scaler.format, new_filter_cx,
				new_filter_cy, new_obs_cx, new_obs_cy);

		UpdatePlaceholder();
	}

	VideoFormat current_format = GetVideoFormat();

	if (current_format != format) {
		if (current_format == VideoFormat::I420)
			scaler.format = placeholder.scaler.format =
				TARGET_FORMAT_I420;
		else if (current_format == VideoFormat::YUY2)
			scaler.format = placeholder.scaler.format =
				TARGET_FORMAT_YUY2;
		else
			scaler.format = placeholder.scaler.format =
				TARGET_FORMAT_NV12;

		format = current_format;

		UpdatePlaceholder();
	}

	uint8_t *ptr;
	if (LockSampleData(&ptr)) {
		if (state == SHARED_QUEUE_STATE_READY)
			ShowOBSFrame(ptr);
		else
			ShowDefaultFrame(ptr);

		UnlockSampleData(ts, ts + obs_interval);
	}
}

void VCamFilter2::ShowOBSFrame(uint8_t *ptr)
{
	uint64_t temp;
	if (!video_queue_read(vq, &scaler, ptr, &temp)) {
		video_queue_close(vq);
		vq = nullptr;
	}
}

void VCamFilter2::ShowDefaultFrame(uint8_t *ptr)
{
	if (placeholder.scaled_data) {
		memcpy(ptr, placeholder.scaled_data, GetOutputBufferSize());
	} else {
		memset(ptr, 127, GetOutputBufferSize());
	}
}

void VCamFilter2::UpdatePlaceholder(void)
{
	if (!placeholder.source_data)
		return;

	if (placeholder.scaled_data)
		free(placeholder.scaled_data);

	placeholder.scaled_data = (uint8_t *)malloc(GetOutputBufferSize());
	if (!placeholder.scaled_data)
		return;

	if (placeholder.cx == GetCX() && placeholder.cy == GetCY() &&
	    placeholder.scaler.format == TARGET_FORMAT_NV12) {
		memcpy(placeholder.scaled_data, placeholder.source_data,
		       GetOutputBufferSize());
	} else {
		nv12_scale_init(&placeholder.scaler, placeholder.scaler.format,
				GetCX(), GetCY(), placeholder.cx,
				placeholder.cy);
		nv12_do_scale(&placeholder.scaler, placeholder.scaled_data,
			      placeholder.source_data);
	}
}

const int VCamFilter2::GetOutputBufferSize(void)
{
	int bits = VFormatBits(format);
	return GetCX() * GetCY() * bits / 8;
}
