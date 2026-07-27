#ifndef WEB_HANDLERS_H
#define WEB_HANDLERS_H

#include <aWOT.h>

void configureWebServer();
void processWebServer();

extern volatile bool configSaveNeeded;

void index(Request &req, Response &res);
void serve_pico_css(Request &req, Response &res);
void serve_stats(Request &req, Response &res);
void api_config_get(Request &req, Response &res);
void api_config_post(Request &req, Response &res);
void api_status(Request &req, Response &res);
void api_capture(Request &req, Response &res);
void api_waveform_get(Request &req, Response &res);
void api_waveform_post(Request &req, Response &res);
void api_fault_clear(Request &req, Response &res);
void api_crash(Request &req, Response &res);
void api_spectrum(Request &req, Response &res);
void api_scope_get(Request &req, Response &res);
void api_config_export(Request &req, Response &res);
void api_config_import(Request &req, Response &res);
void api_presets_get(Request &req, Response &res);
void api_presets_save(Request &req, Response &res);
void api_presets_load(Request &req, Response &res);
void api_presets_delete(Request &req, Response &res);
void api_ota_get(Request &req, Response &res);
void api_ota_post(Request &req, Response &res);
void api_ota_commit(Request &req, Response &res);
void api_ota_abort(Request &req, Response &res);
void api_scope_arm(Request &req, Response &res);
void api_scope_release(Request &req, Response &res);
void api_capture_raw(Request &req, Response &res);

#endif
