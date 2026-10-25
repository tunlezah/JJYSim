// Copyright 2021 BotanicFields, Inc.
// for RTCx(PCf8563) and NTP
//
#include <Arduino.h>
#include "BF_Log.h"
#include "BF_Pcf8563.h"
#include "BF_RtcxNtp.h"

volatile bool                      sntp_sync_status_complete(false);                   // set by the SNTP callback, cleared in loop()
static volatile sntp_sync_status_t sntp_sync_status_last(SNTP_SYNC_STATUS_RESET);  // what the callback saw, printed in loop()

void NtpBegin(const char* time_zone, const char* ntp_server)
{
  configTzTime(time_zone, ntp_server);
  Log.printf("NtpBegin: config TZ time = %s\n", time_zone);
  Log.printf("NtpBegin: SNTP sync mode = %d (0:IMMED 1:SMOOTH)\n", sntp_get_sync_mode());
  Log.printf("NtpBegin: SNTP sync interval = %lums\n", (unsigned long)sntp_get_sync_interval());
  sntp_sync_status_complete = false;
  sntp_set_time_sync_notification_cb(SntpTimeSyncNotificationCallback);
}

void SntpTimeSyncNotificationCallback(struct timeval *tv)
{
  // Runs in the TCP/IP task, whose stack is small on board package 2.x: nothing is printed here, loop() prints.
  sntp_sync_status_t sntp_sync_status = sntp_get_sync_status();
  sntp_sync_status_last = sntp_sync_status;
  if (sntp_sync_status == SNTP_SYNC_STATUS_COMPLETED) {
    sntp_sync_status_complete = true;
  }
}

bool RtcxUpdate(bool rtcx_avail)
{
  if (!sntp_sync_status_complete) {
    return false;
  }
  sntp_sync_status_complete = false;
  PrintSntpStatus("SNTP callback:", sntp_sync_status_last);

  struct tm tm_sync;
  getLocalTime(&tm_sync);
  Log.print(&tm_sync, "SNTP sync: %A, %B %d %Y %H:%M:%S\n");
  // print sample: must be < 64
  //....:....1....:....2....:....3....:....4....:....5....:....6....
  //SNTP sync: Wednesday, September 11 2021 11:10:46

  if (rtcx_avail) {
    // Write the RTC at the start of a second: wait for the system clock's second to change (up to 1s, here in
    // loop()), then write that second with the RTC's clock stopped and start it again, so that the RTC's seconds
    // begin within a few ms of the system clock's instead of up to a second apart.
    struct timeval tv;
    gettimeofday(&tv, NULL);
    time_t sec = tv.tv_sec;
    unsigned long wait_start_ms = millis();
    while (tv.tv_sec == sec && millis() - wait_start_ms < 1100) {
      delay(1);
      gettimeofday(&tv, NULL);
    }
    localtime_r(&tv.tv_sec, &tm_sync);
    if (rtcx.WriteTimeAtSecond(&tm_sync) == 0) {
      Log.print("RTCx updated\n");
    }
    else {
      Log.print("RTCx update failed\n");
    }
  }
  return true;
}

void PrintSntpStatus(const char* header, sntp_sync_status_t sntp_sync_status)
{
  static const char* sntp_sync_status_str[] = {
    "SNTP_SYNC_STATUS_RESET       ",  // 0
    "SNTP_SYNC_STATUS_COMPLETED   ",  // 1
    "SNTP_SYNC_STATUS_IN_PROGRESS ",  // 2
    "sntp_sync_status invalid     ",  // 3
  };
  int sntp_sync_status_index = 3;
  if (sntp_sync_status >= 0 && sntp_sync_status <= 2) {
    sntp_sync_status_index = sntp_sync_status;
  }
  Log.printf("%s status = %d %s\n", header, sntp_sync_status, sntp_sync_status_str[sntp_sync_status_index]);
}

bool SetTimeFromRtcx(const char* time_zone)
{
  bool rtcx_valid(false);
  struct tm tm_init = {};
  tm_init.tm_isdst = -1;  // let mktime() work out whether daylight saving time applies to the RTC's wall-clock time

  setenv("TZ", time_zone, 1);
  tzset();  // assign timezone with setenv for mktime()

  // Accept only years after 2016: getLocalTime() treats older times as not set, and a 19xx year means the
  // RTC was set by other software (opposite century bit, and often UTC), so wait for NTP instead.
  // An RTC whose clock was found stopped at start-up holds a stale time (a write was interrupted): wait too.
  if (rtcx.ReadTime(&tm_init) == 0 && !rtcx.ClockWasStopped() && tm_init.tm_year > 116) {
    // Read the RTC at the start of a second: poll until its seconds change (up to 1s), so that the system clock
    // is set within a few ms of the RTC rather than up to a second late.
    struct tm tm_now = tm_init;
    unsigned long wait_start_ms = millis();
    while (tm_now.tm_sec == tm_init.tm_sec && millis() - wait_start_ms < 1100) {
      if (rtcx.ReadTime(&tm_now) != 0) {
        tm_now = tm_init;  // keep the first reading if the RTC stops answering
        break;
      }
    }
    rtcx_valid = true;
    struct timeval tv = { mktime(&tm_now), 0 };
    settimeofday(&tv, NULL);
    Log.print("RTCx valid, the localtime was set\n");
  } else {
    Log.print("RTCx not valid\n");
  }
  return rtcx_valid;
}
