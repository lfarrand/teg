#include "utils.h"
#include "ntp_utils.h"
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

extern char influxDbServerAddress[];
extern int influxDbPort;
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
  for (int i = 0; i < LogSize - 1; i++) {
    logs[i] = logs[i + 1];
  }

  logs[LogSize - 1] = msg;

  Serial.println(msg);

  displayDirty = true;
}

void setStatusLine(const String &line) {
  if (statusLine != line) {
    statusLine = line;
    displayDirty = true;
  }
}

void flushDisplay() {
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

time_t getNtpTime() {
  while (ntpUDP.parsePacket() > 0);
  Serial.println(F("Transmit NTP Request"));
  sendNTPpacket(timeServer);
  uint32_t beginWait = millis();
  while (millis() - beginWait < 1500) {
    int size = ntpUDP.parsePacket();
    if (size >= NTP_PACKET_SIZE) {
      Serial.println(F("Receive NTP Response"));
      ntpUDP.read(packetBuffer, NTP_PACKET_SIZE);
      uint32_t secsSince1900 = parseNtpSeconds(packetBuffer);
      Serial.print(F("Seconds since 1 Jan 1900: "));
      Serial.println(secsSince1900);
      time_t time = ntpToUnixEpoch(secsSince1900);
      Serial.print(F("Seconds since 1 Jan 1970: "));
      Serial.println(time);
      return time;
    }
  }
  Serial.println("No NTP Response :-(");
  return 0;
}

void sendNTPpacket(const char *host) {
  buildNtpRequest(packetBuffer);
  ntpUDP.beginPacket(host, 123);
  ntpUDP.write(packetBuffer, NTP_PACKET_SIZE);
  ntpUDP.endPacket();
}

void writeInfluxDb(const String &data) {
  if (influxDbClient.connect(influxDbServerAddress, influxDbPort)) {
    influxDbClient.print("POST /api/v2/write?org=501eaf58ac3171cd&bucket=power_generator&precision=ms HTTP/1.1\r\n");
    influxDbClient.print("Host: ");
    influxDbClient.println(influxDbServerAddress);
    influxDbClient.println("Authorization: Bearer oSe4_XGLyob-Ns0FT56CouDXw3jEpocQ0ntSuX7q0vr6JOn82GapRz0yUfrnpobYPxTTwS_EV2nyJ6vMCvGTcA==");
    influxDbClient.println("Content-Type: text/plain");
    influxDbClient.print("Content-Length: ");
    influxDbClient.println(data.length());
    influxDbClient.println();
    influxDbClient.print(data);

    while (influxDbClient.connected()) {
      if (influxDbClient.available()) {
        String line = influxDbClient.readStringUntil('\n');
      }
    }
    influxDbClient.stop();
  } else {
    Serial.println("InfluxDB connect failed");
  }
}