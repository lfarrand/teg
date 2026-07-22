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

#endif
