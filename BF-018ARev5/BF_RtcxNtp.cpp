// Copyright 2021 BotanicFields, Inc.
// for RTCx(PCf8563) and NTP
//
#include <Arduino.h>
#include <esp_timer.h>
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

// The RTC is written so that its seconds begin on the system clock's. The write runs with the RTC's clock stopped,
// and the time circuits make their first increment some time after the clock is started again: 0.5078s on the
// PCF8563 (datasheet 8.10, table 26), possibly 1s on a clone that resets its whole divider chain (the HYM8563 data
// sheet does not say). The lead below is that time plus the I2C transfers before the start. After each write the
// RTC is watched for its first change of seconds and the lead is corrected from what is measured, so that the
// alignment comes out right on either chip; the log shows the result.
static long rtcx_lead_us(507813L + 2000L);

// Wait for the system clock to reach start_us into a second (a 3ms window, up to 1.2s), then write the second in
// progress plus offset_sec with the RTC's clock stopped and started again. false: no window in time (loop() was
// held up) or the write failed; the RTC is then left as it is.
static bool RtcxWriteAligned(long start_us, int offset_sec)
{
  struct timeval tv;
  unsigned long wait_start_ms = millis();
  for (;;) {
    gettimeofday(&tv, NULL);
    if (tv.tv_usec >= start_us && tv.tv_usec < start_us + 3000L) {
      break;
    }
    if (millis() - wait_start_ms >= 1200) {
      Log.print("RTCx not updated: no quiet moment for the write\n");
      return false;
    }
    delay(1);
  }
  struct tm tm_write;
  time_t sec = tv.tv_sec + offset_sec;
  localtime_r(&sec, &tm_write);
  int64_t write_start_us = esp_timer_get_time();
  int result = rtcx.WriteTimeAtSecond(&tm_write);
  Log.printf("RTCx write %s: started at %ld us of the second, the clock started %ld us later\n",
             result == 0 ? "done" : "failed", (long)tv.tv_usec, (long)rtcx.StartedAfterUs(write_start_us));
  return result == 0;
}

// The system clock's phase (us into its second) at the RTC's next change of seconds, or -1 if that could not be
// seen within 1.2s. The reads take about 1ms each, so the result is late by up to that much.
static long RtcxSecondChangePhaseUs()
{
  struct tm tm_first, tm_now;
  if (rtcx.ReadTime(&tm_first) != 0) {
    return -1;
  }
  unsigned long wait_start_ms = millis();
  for (;;) {
    if (rtcx.ReadTime(&tm_now) != 0 || millis() - wait_start_ms >= 1200) {
      return -1;
    }
    if (tm_now.tm_sec != tm_first.tm_sec) {
      struct timeval tv;
      gettimeofday(&tv, NULL);
      return tv.tv_usec;
    }
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
    // once per NTP sync, here in loop(): the aligned write, a check of the RTC's first change of seconds (up to
    // about 2.5s in all), and one more write with the corrected lead if the check was off by 20ms or more
    for (int attempt = 0; attempt < 2; ++attempt) {
      long start_us = 1000000L - rtcx_lead_us;
      int offset_sec = 0;
      while (start_us < 0) {  // a lead of a second or more: start before the boundary and write the second to come
        start_us += 1000000L;
        ++offset_sec;
      }
      if (!RtcxWriteAligned(start_us, offset_sec)) {
        break;
      }
      long phase_us = RtcxSecondChangePhaseUs();
      if (phase_us < 0) {
        Log.print("RTCx: its change of seconds could not be watched\n");
        break;
      }
      long error_us = phase_us > 500000L ? phase_us - 1000000L : phase_us;  // +: the RTC's seconds begin after the system clock's
      Log.printf("RTCx: its seconds begin %+ld us from the system clock's (lead %ld us)\n", error_us, rtcx_lead_us);
      if (error_us > -20000L && error_us < 20000L) {
        break;
      }
      rtcx_lead_us += error_us;  // late by error_us: start that much earlier next time
      if (rtcx_lead_us < 100000L || rtcx_lead_us > 1900000L) {
        rtcx_lead_us = 507813L + 2000L;  // not credible: back to the data sheet value
        break;
      }
      Log.print("RTCx: written again with the corrected lead\n");
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
  // An RTC whose last write was interrupted (its marker is still set) holds a stale time: wait too.
  if (rtcx.ReadTime(&tm_init) == 0 && !rtcx.WriteInterrupted() && tm_init.tm_year > 116) {
    // Read the RTC at the start of a second: poll until its seconds change (up to 1s), so that the system clock
    // is set within a few ms of the RTC rather than up to a second late. The reading at the change must be exactly
    // one second on from the first: one bad I2C reading, or an RTC that does not advance, must not become the time.
    struct tm tm_now = tm_init;
    unsigned long wait_start_ms = millis();
    while (tm_now.tm_sec == tm_init.tm_sec && millis() - wait_start_ms < 1100) {
      if (rtcx.ReadTime(&tm_now) != 0) {
        break;
      }
    }
    struct tm tm_first = tm_init;  // mktime() alters its argument
    struct tm tm_change = tm_now;
    if (mktime(&tm_change) - mktime(&tm_first) == 1) {
      rtcx_valid = true;
      struct timeval tv = { mktime(&tm_now), 0 };
      settimeofday(&tv, NULL);
      Log.print("RTCx valid, the localtime was set\n");
    } else {
      Log.print("RTCx not valid: no clean change of its seconds\n");
    }
  } else {
    Log.print("RTCx not valid\n");
  }
  return rtcx_valid;
}
