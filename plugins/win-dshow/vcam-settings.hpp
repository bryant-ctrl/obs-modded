#pragma once

#include <obs-module.h>
#include <obs-frontend-api.h>
#include <QWidget>
#include <QComboBox>
#include <QPushButton>

/* Config keys */
#define VCAM_CONFIG_SECTION "VirtualCam"
#define VCAM_CONFIG_KEY1 "Scene1"
#define VCAM_CONFIG_KEY2 "Scene2"
#define VCAM_CONFIG_DEFAULT1 "Camera1"
#define VCAM_CONFIG_DEFAULT2 "Camera2"

class VCamSettingsWidget : public QWidget {
	Q_OBJECT

public:
	VCamSettingsWidget(QWidget *parent = nullptr);

	static void PopulateSceneList(QComboBox *combo,
				      const char *selected_name);

private slots:
	void OnScene1Changed(int idx);
	void OnScene2Changed(int idx);
	void OnStartCam1();
	void OnStopCam1();
	void OnStartCam2();
	void OnStopCam2();
	void OnFrontendEvent(enum obs_frontend_event event);

private:
	QComboBox *scene1Combo = nullptr;
	QComboBox *scene2Combo = nullptr;
	QPushButton *startCam1Btn = nullptr;
	QPushButton *stopCam1Btn = nullptr;
	QPushButton *startCam2Btn = nullptr;
	QPushButton *stopCam2Btn = nullptr;

	obs_output_t *cam1Output = nullptr;
	obs_output_t *cam2Output = nullptr;

	void SaveSceneName(const char *key, const char *name);
	void RefreshSceneLists();
	void UpdateButtonStates();

	static void FrontendEventCB(enum obs_frontend_event event, void *data);
};

/* Called once from dshow-plugin.cpp obs_module_load() */
void vcam_settings_dock_register();
