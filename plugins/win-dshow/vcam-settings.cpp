#include "vcam-settings.hpp"

#include <obs-module.h>
#include <obs-frontend-api.h>
#include <util/config-file.h>

#include <QGridLayout>
#include <QLabel>
#include <QMainWindow>
#include <QString>

/* =========================================================================
 * Helpers
 * ======================================================================= */

static bool enum_scene_cb(void *param, obs_source_t *source)
{
	QComboBox *combo = reinterpret_cast<QComboBox *>(param);
	if (obs_source_get_type(source) == OBS_SOURCE_TYPE_SCENE) {
		const char *name = obs_source_get_name(source);
		combo->addItem(QString::fromUtf8(name));
	}
	return true;
}

/* =========================================================================
 * VCamSettingsWidget
 * ======================================================================= */

VCamSettingsWidget::VCamSettingsWidget(QWidget *parent) : QWidget(parent)
{
	QGridLayout *layout = new QGridLayout(this);
	layout->setColumnStretch(1, 1);

	/* --- Camera 1 row --- */
	layout->addWidget(new QLabel(QStringLiteral("Cam 1 Scene:"), this), 0,
			  0);
	scene1Combo = new QComboBox(this);
	layout->addWidget(scene1Combo, 0, 1);

	startCam1Btn = new QPushButton(QStringLiteral("Start"), this);
	stopCam1Btn = new QPushButton(QStringLiteral("Stop"), this);
	layout->addWidget(startCam1Btn, 0, 2);
	layout->addWidget(stopCam1Btn, 0, 3);

	/* --- Camera 2 row --- */
	layout->addWidget(new QLabel(QStringLiteral("Cam 2 Scene:"), this), 1,
			  0);
	scene2Combo = new QComboBox(this);
	layout->addWidget(scene2Combo, 1, 1);

	startCam2Btn = new QPushButton(QStringLiteral("Start"), this);
	stopCam2Btn = new QPushButton(QStringLiteral("Stop"), this);
	layout->addWidget(startCam2Btn, 1, 2);
	layout->addWidget(stopCam2Btn, 1, 3);

	setLayout(layout);

	connect(scene1Combo, QOverload<int>::of(&QComboBox::currentIndexChanged),
		this, &VCamSettingsWidget::OnScene1Changed);
	connect(scene2Combo, QOverload<int>::of(&QComboBox::currentIndexChanged),
		this, &VCamSettingsWidget::OnScene2Changed);
	connect(startCam1Btn, &QPushButton::clicked, this,
		&VCamSettingsWidget::OnStartCam1);
	connect(stopCam1Btn, &QPushButton::clicked, this,
		&VCamSettingsWidget::OnStopCam1);
	connect(startCam2Btn, &QPushButton::clicked, this,
		&VCamSettingsWidget::OnStartCam2);
	connect(stopCam2Btn, &QPushButton::clicked, this,
		&VCamSettingsWidget::OnStopCam2);

	obs_frontend_add_event_callback(FrontendEventCB, this);

	UpdateButtonStates();
}

/* static */
void VCamSettingsWidget::PopulateSceneList(QComboBox *combo,
					   const char *selected_name)
{
	combo->blockSignals(true);
	combo->clear();
	obs_enum_sources(enum_scene_cb, combo);

	int idx = combo->findText(QString::fromUtf8(selected_name));
	if (idx >= 0)
		combo->setCurrentIndex(idx);

	combo->blockSignals(false);
}

void VCamSettingsWidget::RefreshSceneLists()
{
	config_t *cfg = obs_frontend_get_profile_config();

	config_set_default_string(cfg, VCAM_CONFIG_SECTION, VCAM_CONFIG_KEY1,
				  VCAM_CONFIG_DEFAULT1);
	config_set_default_string(cfg, VCAM_CONFIG_SECTION, VCAM_CONFIG_KEY2,
				  VCAM_CONFIG_DEFAULT2);

	const char *s1 =
		config_get_string(cfg, VCAM_CONFIG_SECTION, VCAM_CONFIG_KEY1);
	const char *s2 =
		config_get_string(cfg, VCAM_CONFIG_SECTION, VCAM_CONFIG_KEY2);

	PopulateSceneList(scene1Combo, s1 ? s1 : VCAM_CONFIG_DEFAULT1);
	PopulateSceneList(scene2Combo, s2 ? s2 : VCAM_CONFIG_DEFAULT2);
}

void VCamSettingsWidget::SaveSceneName(const char *key, const char *name)
{
	config_t *cfg = obs_frontend_get_profile_config();
	if (cfg && name) {
		config_set_string(cfg, VCAM_CONFIG_SECTION, key, name);
		config_save_safe(cfg, "tmp", nullptr);
	}
}

void VCamSettingsWidget::UpdateButtonStates()
{
	bool cam1Active = cam1Output && obs_output_active(cam1Output);
	bool cam2Active = cam2Output && obs_output_active(cam2Output);

	startCam1Btn->setEnabled(!cam1Active);
	stopCam1Btn->setEnabled(cam1Active);
	startCam2Btn->setEnabled(!cam2Active);
	stopCam2Btn->setEnabled(cam2Active);
}

void VCamSettingsWidget::OnScene1Changed(int /*idx*/)
{
	QString name = scene1Combo->currentText();
	if (!name.isEmpty())
		SaveSceneName(VCAM_CONFIG_KEY1, name.toUtf8().constData());
}

void VCamSettingsWidget::OnScene2Changed(int /*idx*/)
{
	QString name = scene2Combo->currentText();
	if (!name.isEmpty())
		SaveSceneName(VCAM_CONFIG_KEY2, name.toUtf8().constData());
}

void VCamSettingsWidget::OnStartCam1()
{
	if (!cam1Output) {
		cam1Output = obs_output_create("virtualcam_output",
					       "Virtual Camera 1", nullptr,
					       nullptr);
	}
	if (cam1Output && !obs_output_active(cam1Output)) {
		obs_output_start(cam1Output);
	}
	UpdateButtonStates();
}

void VCamSettingsWidget::OnStopCam1()
{
	if (cam1Output && obs_output_active(cam1Output)) {
		obs_output_stop(cam1Output);
	}
	UpdateButtonStates();
}

void VCamSettingsWidget::OnStartCam2()
{
	if (!cam2Output) {
		cam2Output = obs_output_create("virtualcam2_output",
					       "Virtual Camera 2", nullptr,
					       nullptr);
	}
	if (cam2Output && !obs_output_active(cam2Output)) {
		obs_output_start(cam2Output);
	}
	UpdateButtonStates();
}

void VCamSettingsWidget::OnStopCam2()
{
	if (cam2Output && obs_output_active(cam2Output)) {
		obs_output_stop(cam2Output);
	}
	UpdateButtonStates();
}

/* static */
void VCamSettingsWidget::FrontendEventCB(enum obs_frontend_event event,
					 void *data)
{
	VCamSettingsWidget *w = reinterpret_cast<VCamSettingsWidget *>(data);
	w->OnFrontendEvent(event);
}

void VCamSettingsWidget::OnFrontendEvent(enum obs_frontend_event event)
{
	switch (event) {
	case OBS_FRONTEND_EVENT_SCENE_LIST_CHANGED:
	case OBS_FRONTEND_EVENT_FINISHED_LOADING:
		RefreshSceneLists();
		break;
	case OBS_FRONTEND_EVENT_EXIT:
		obs_output_release(cam1Output);
		cam1Output = nullptr;
		obs_output_release(cam2Output);
		cam2Output = nullptr;
		break;
	default:
		break;
	}
	UpdateButtonStates();
}

/* =========================================================================
 * Registration
 * ======================================================================= */

void vcam_settings_dock_register()
{
	QMainWindow *main =
		reinterpret_cast<QMainWindow *>(obs_frontend_get_main_window());

	VCamSettingsWidget *w = new VCamSettingsWidget(main);

	obs_frontend_add_dock_by_id("vcam-settings-dock", "Virtual Cameras", w);
}
