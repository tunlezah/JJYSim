// BF-018A Rev.5
// web UI: status page, settings and serial log over HTTP
//
#pragma once
#include <Arduino.h>

// settings the web UI can change: stored in NVS and applied at boot
struct WebUiSettings {
  uint32_t jjy_frequency;   // 40000 or 60000
  char     time_zone[64];   // POSIX TZ string, e.g. "JST-9"
  char     ntp_server[64];  // host name or IPv4 address
};

void WebUiLoadSettings(WebUiSettings *settings);  // keeps the given values for anything not stored
void WebUiBegin(const char *hostname, const char *user, const char *password);
void WebUiHandle();       // call from loop()
bool WebUiAuthEnabled();  // a password is set, so the web UI may change things
void WebUiJsonString(Print &out, const char *s);  // write s as a JSON string

// implemented by the sketch
void WebUiStatusJson(Print &out);  // the object /api/status returns
void WebUiSetLed(bool enable);     // LED monitor on/off, like the button
void WebUiSyncTime();              // ask the NTP server for the time now
void WebUiTimeZoneSaving(const char *time_zone);  // called before new settings are stored, with the time zone in them
