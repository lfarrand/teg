#include "metrics.h"
#include "config_json.h"
#include "pwm_utils.h"
#include "thermal.h"
#include "capture.h"
#include "meter.h"
#include "power_monitor.h"
#include "waveform.h"
#include "utils.h"
#include <Arduino.h>

extern MainConfig config;

FLASHMEM void metricsTask() {
  if (config.Influx.Token[0] == '\0' || config.Influx.IntervalSeconds == 0) {
    return;
  }

  static elapsedMillis sinceLastPush;
  if (sinceLastPush < static_cast<uint32_t>(config.Influx.IntervalSeconds) * 1000UL) {
    return;
  }
  sinceLastPush = 0;

  // InfluxDB line protocol; the server assigns the timestamp
  char line[560];
  int n = snprintf(line, sizeof(line),
                   "teg index_milli=%lu,target_milli=%lu,isr_cycles=%lu,apply_us=%lu,"
                   "mod_mhz=%llu,fault=%d,derate_milli=%u,dtcm_free=%d,ocram_free=%d,"
                   "underruns=%lu",
                   static_cast<unsigned long>(modulationIndexNowMilli()),
                   static_cast<unsigned long>(modulationIndexTargetMilli()),
                   static_cast<unsigned long>(vIsrCycles),
                   0UL, // apply latency is event-driven; carried by the log instead
                   static_cast<unsigned long long>(modulationActualMilliHz()),
                   vFaultTripped ? 1 : 0, thermalDerateMilliNow(), getFreeMemory(), freeram(),
                   static_cast<unsigned long>(waveformStreamUnderruns()));

  // Optional fields only when the reading exists
  if (captureActive()) {
    n += snprintf(line + n, sizeof(line) - n, ",feedback_raw=%lu",
                  static_cast<unsigned long>(captureMeanRaw(64)));
  }
  const MeterReadings meter = meterReadings();
  if (meter.valid) {
    n += snprintf(line + n, sizeof(line) - n,
                  ",power_mw=%ld,vrms_mv=%lu,irms_ma=%lu,pf_milli=%ld,energy_mwh=%llu",
                  static_cast<long>(meter.powerMw), static_cast<unsigned long>(meter.vrmsMv),
                  static_cast<unsigned long>(meter.irmsMa), static_cast<long>(meter.pfMilli),
                  static_cast<unsigned long long>(meterEnergyMwh()));
  }
  if (thermalHotDeciC() != INT16_MIN) {
    n += snprintf(line + n, sizeof(line) - n, ",hot_decic=%d", thermalHotDeciC());
  }
  if (thermalColdDeciC() != INT16_MIN) {
    n += snprintf(line + n, sizeof(line) - n, ",cold_decic=%d", thermalColdDeciC());
  }
  if (thermalChipDeciC() != INT16_MIN) {
    n += snprintf(line + n, sizeof(line) - n, ",chip_decic=%d", thermalChipDeciC());
  }
  const PowerMonReadings aux = powerMonReadings();
  if (aux.valid) {
    n += snprintf(line + n, sizeof(line) - n,
                  ",aux_power_mw=%lu,aux_bus_mv=%lu,aux_current_ma=%ld,aux_energy_mwh=%llu,"
                  "aux_pg_efuse=%d,aux_pg_buck=%d,aux_alert=%d",
                  static_cast<unsigned long>(aux.powerMw), static_cast<unsigned long>(aux.busMv),
                  static_cast<long>(aux.currentMa),
                  static_cast<unsigned long long>(powerMonEnergyMwh()), aux.pgEfuse, aux.pgBuck,
                  aux.alert);
    if (aux.imonMa >= 0) {
      n += snprintf(line + n, sizeof(line) - n, ",aux_imon_ma=%ld",
                    static_cast<long>(aux.imonMa));
    }
  }
  if (n >= static_cast<int>(sizeof(line))) {
    return; // truncated; skip this push
  }

  writeInfluxDb(line);
}
