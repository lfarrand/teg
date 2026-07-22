#ifndef METRICS_H
#define METRICS_H

// Periodic InfluxDB metrics push; call from loop(). Active only when a token
// is configured and Influx.IntervalSeconds > 0.

void metricsTask();

#endif
