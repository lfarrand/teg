#include "utils.h"
#include "event_log_api.h"
#include "main.h" // kickWatchdog
#include "ntp_utils.h"
#include <QNEthernet.h>
#include "config_json.h"
#include "Adafruit_SSD1306.h"
#include "qnethernet/QNEthernetClient.h"
#include "qnethernet/QNEthernetUDP.h"

extern Adafruit_SSD1306 display;
extern char timeServer[];

String logs[LogSize] = {
  "Line 1",
  "Line 2",
  "Line 3",
  "Line 4",
  "Line 5"
};

constexpr int NTP_PACKET_SIZE = 48;
byte packetBuffer[NTP_PACKET_SIZE];

extern qindesign::network::EthernetUDP ntpUDP;

extern qindesign::network::EthernetClient influxDbClient;

// OLED redraws are deferred and rate-limited: a full display.display() pushes
// the 1KB framebuffer over I2C (~25-30ms blocking), so writers only mark the
// display dirty and flushDisplay() coalesces changes into at most one redraw
// per DisplayFlushIntervalMs.
static String statusLine;
static bool displayDirty = false;
static unsigned long lastDisplayFlush = 0;
constexpr unsigned long DisplayFlushIntervalMs = 250;

// Deliberately does NOT redraw the display: a full OLED push blocks for
// ~25-30ms, which would land inside latency-sensitive paths (the settings
// apply path in particular). loop() calls flushDisplay() every pass, so the
// display still updates within a millisecond of the log line.
void writeLog(const String &msg) {
  writeLogLevel(EventInfo, msg);
}

void writeLogLevel(uint8_t level, const String &msg) {
  for (int i = 0; i < LogSize - 1; i++) {
    logs[i] = logs[i + 1];
  }

  logs[LogSize - 1] = msg;

  Serial.println(msg);

  // Timestamped copy for /api/log
  eventLogRecord(level, msg.c_str());

  displayDirty = true;
}

void setStatusLine(const String &line) {
  if (statusLine != line) {
    statusLine = line;
    displayDirty = true;
  }
}

void flushDisplay() {
  extern bool displayAvailable;
  if (!displayAvailable) {
    displayDirty = false;
    return;
  }
  if (!displayDirty || millis() - lastDisplayFlush < DisplayFlushIntervalMs) {
    return;
  }

  display.clearDisplay();

  display.setCursor(0, 0);
  display.println(statusLine);

  for (int16_t i = 0; i < LogSize; i++) {
    display.setCursor(10, (i + 1) * 10);
    display.println(logs[i]);
  }

  display.display();

  displayDirty = false;
  lastDisplayFlush = millis();
}

int getFreeMemory() {
  char top;
  return &top - static_cast<char *>(sbrk(0));
}

int freeram() {
  extern unsigned long _heap_end;
  extern char *__brkval;
  return (char *)&_heap_end - __brkval;
}

void printDigits(int digits) {
  Serial.print(F(":"));
  if (digits < 10)
    Serial.print('0');
  Serial.print(digits);
}

// Non-blocking NTP client. The request is sent from loop() and the reply is
// collected on later passes, so no control task ever waits on the network.
// The server name is resolved ONCE (DNS lookups block for seconds) and the
// address cached; a failed resolve is retried on a slow cadence with the
// watchdog serviced around it.
namespace {
constexpr uint32_t NtpResyncIntervalMs = 3600000UL; // hourly is ample for logs
constexpr uint32_t NtpRetryIntervalMs = 60000UL;
constexpr uint32_t NtpReplyTimeoutMs = 2000UL;

IPAddress ntpAddr;
bool ntpAddrValid = false;
bool ntpAwaiting = false;
uint32_t ntpSentAt = 0;
uint32_t ntpLastAttempt = 0;
bool ntpEverAttempted = false;
} // namespace

void ntpTask() {
  const uint32_t nowMs = millis();
  const bool haveTime = eventClockNtpSynced();
  const uint32_t due = haveTime ? NtpResyncIntervalMs : NtpRetryIntervalMs;

  if (ntpAwaiting) {
    const int size = ntpUDP.parsePacket();
    if (size >= NTP_PACKET_SIZE) {
      ntpUDP.read(packetBuffer, NTP_PACKET_SIZE);
      const uint32_t secsSince1900 = parseNtpSeconds(packetBuffer);
      const uint32_t epoch = static_cast<uint32_t>(ntpToUnixEpoch(secsSince1900));
      ntpAwaiting = false;
      // Validate before adopting: an unsolicited or malformed datagram must
      // never set the clock, and must never reach the battery-backed RTC
      if (epoch >= MinPlausibleEpoch) {
        eventClockSynced(epoch);
      } else {
        writeLogLevel(EventWarn, "NTP reply rejected (implausible time)");
      }
      return;
    }
    if (nowMs - ntpSentAt > NtpReplyTimeoutMs) {
      ntpAwaiting = false; // no reply; retry on the normal cadence
    }
    return;
  }

  if (ntpEverAttempted && nowMs - ntpLastAttempt < due) {
    return;
  }
  if (static_cast<uint32_t>(Ethernet.localIP()) == 0) {
    return; // no link yet
  }
  ntpLastAttempt = nowMs;
  ntpEverAttempted = true;

  if (!ntpAddrValid) {
    // The one blocking step, rate-limited to once a minute at worst and
    // bracketed by watchdog kicks
    kickWatchdog();
    ntpAddrValid = Ethernet.hostByName(timeServer, ntpAddr);
    kickWatchdog();
    if (!ntpAddrValid) {
      return;
    }
  }
  while (ntpUDP.parsePacket() > 0) {
  }
  buildNtpRequest(packetBuffer);
  ntpUDP.beginPacket(ntpAddr, 123);
  ntpUDP.write(packetBuffer, NTP_PACKET_SIZE);
  ntpUDP.endPacket();
  ntpSentAt = millis();
  ntpAwaiting = true;
}

void writeInfluxDb(const String &data) {
  // Metrics are disabled until a token is configured (settings.cfg / web UI);
  // the token is deliberately never present in source
  if (config.Influx.Token[0] == '\0' || config.Influx.Host[0] == '\0') {
    return;
  }

  // Called from loop(): bound every wait so an unreachable server can only
  // stall the loop briefly, not indefinitely
  influxDbClient.setConnectionTimeout(500);
  if (influxDbClient.connect(config.Influx.Host, config.Influx.Port)) {
    influxDbClient.printf("POST /api/v2/write?org=%s&bucket=%s&precision=ms HTTP/1.1\r\n",
                          config.Influx.Org, config.Influx.Bucket);
    influxDbClient.print("Host: ");
    influxDbClient.println(config.Influx.Host);
    influxDbClient.print("Authorization: Bearer ");
    influxDbClient.println(config.Influx.Token);
    influxDbClient.println("Content-Type: text/plain");
    influxDbClient.print("Content-Length: ");
    influxDbClient.println(data.length());
    influxDbClient.println();
    influxDbClient.print(data);

    elapsedMillis drainTimer;
    while (influxDbClient.connected() && drainTimer < 250) {
      if (influxDbClient.available()) {
        influxDbClient.read();
      }
    }
    influxDbClient.stop();
  } else {
    Serial.println("InfluxDB connect failed");
  }
}