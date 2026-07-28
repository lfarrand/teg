#ifndef MTP_WDOG_H
#define MTP_WDOG_H

// LOCAL PATCH (not upstream MTP_Teensy).
//
// Upstream runs an entire MTP file transfer, directory scan or recursive
// delete to completion inside a single MTP.loop() call, and services no
// watchdog while doing so. This firmware arms WDOG1 with an 8s timeout that
// is normally fed once per superloop pass, and on the i.MX RT1062 WDOG1's
// timeout cannot be widened or disabled once enabled - so an unpatched
// transfer of more than a few MB is a guaranteed hardware reset of a
// running inverter.
//
// These are two plain register writes (the documented WDOG1 service
// sequence). They are safe to call from anywhere, including from inside a
// filesystem operation.

#include <imxrt.h>

static inline void mtpKickWatchdog() {
  WDOG1_WSR = 0x5555;
  WDOG1_WSR = 0xAAAA;
}

#endif
