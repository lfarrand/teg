#pragma once

#ifndef TEG_WITH_OLED
#define TEG_WITH_OLED 1
#endif

#ifndef TEG_WITH_THERMAL
#define TEG_WITH_THERMAL 1
#endif

#ifndef TEG_WITH_MQTT
#define TEG_WITH_MQTT 1
#endif

#ifndef TEG_WITH_INFLUX
#define TEG_WITH_INFLUX 1
#endif

#ifndef TEG_WITH_SPECTRUM
#define TEG_WITH_SPECTRUM 1
#endif

#ifndef TEG_WITH_POWERMON
#define TEG_WITH_POWERMON 1
#endif

#ifndef TEG_WITH_MTP_SERVICE
// 0 later skips MTP.begin()/MTP.loop() only; USB stays bare -DUSB_MTPDISK_SERIAL.
#define TEG_WITH_MTP_SERVICE 1
#endif
