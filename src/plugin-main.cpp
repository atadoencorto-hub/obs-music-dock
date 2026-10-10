#include <obs-module.h>
#include <obs-frontend-api.h>

#include <QPointer>
#include <QString>

#include "music-dock.hpp"

OBS_DECLARE_MODULE()
OBS_MODULE_AUTHOR("Manuel")

static QPointer<MusicDock> g_dock;

static void on_frontend_event(enum obs_frontend_event event, void *)
{
	// Al cerrar OBS paramos la música y liberamos la fuente de audio
	// ANTES de que libobs se apague.
	if (event == OBS_FRONTEND_EVENT_EXIT && g_dock)
		g_dock->shutdown();
}

bool obs_module_load(void)
{
	char *cfg = obs_module_config_path("playlist.json");
	const QString cfgPath = cfg ? QString::fromUtf8(cfg) : QString();
	bfree(cfg);

	auto *dock = new MusicDock(cfgPath);
	g_dock = dock;

	if (!obs_frontend_add_dock_by_id("obs_music_dock",
					 "Reproductor de música", dock)) {
		blog(LOG_WARNING,
		     "[obs-music-dock] No se pudo registrar el dock "
		     "(¿OBS anterior a la versión 30?)");
		delete dock;
		return false;
	}

	obs_frontend_add_event_callback(on_frontend_event, nullptr);
	blog(LOG_INFO, "[obs-music-dock] Cargado correctamente");
	return true;
}

void obs_module_unload(void)
{
	obs_frontend_remove_event_callback(on_frontend_event, nullptr);
}
