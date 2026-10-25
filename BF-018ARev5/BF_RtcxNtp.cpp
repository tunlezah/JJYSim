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
// RTC is watched for its first change of seconds, timed on the processor's monotonic clock, and the lead is
// corrected from what is measured, so that the alignment comes out right on either chip; the log shows the result.
static long rtcx_lead_us(507813L + 2000L);

enum rtcx_write_t { rtcx_not_written, rtcx_written, rtcx_checked };

// Wait for the system clock to reach lead_us before the start of a second (a 3ms window, up to 1.2s), write that
// second with the RTC's clock stopped and started again, then watch the RTC for its first change of seconds.
// rtcx_checked: error_us holds how long after the system clock's second the RTC's began (+: later), apart from
// the up to 1ms a read takes. rtcx_written: the RTC was written but the check did not come about. rtcx_not_written:
// no window in time (loop() was held up) or the write failed; the RTC is then left as it is.
static rtcx_write_t RtcxWriteAligned(long lead_us, long &error_us)
{
  long start_us = 1000000L - lead_us;  // the write begins here in the second, so that the RTC's seconds begin at 0
  int offset_sec = 0;
  while (start_us < 0) {  // a lead of a second or more: write the second after the one in progress
    start_us += 1000000L;
    ++offset_sec;
  }

  // The window may run on into the next second, which is then the one in progress at the write.
  struct timeval tv;
  long into_window_us;
  unsigned long wait_start_ms = millis();
  for (;;) {
    gettimeofday(&tv, NULL);
    into_window_us = tv.tv_usec - start_us;
    if (into_window_us < 0) {
      into_window_us += 1000000L;  // past the second boundary: measured from start_us in the second before
    }
    if (into_window_us < 3000L) {
      break;
    }
    if (millis() - wait_start_ms >= 1200) {
      Log.print("RTCx not updated: no quiet moment for the write\n");
      return rtcx_not_written;
    }
    delay(1);
  }
  struct tm tm_write;
  time_t sec = tv.tv_sec + offset_sec;
  if (tv.tv_usec < start_us) {
    --sec;  // the window began in the second before the one in progress
  }
  localtime_r(&sec, &tm_write);
  int64_t write_start_us = esp_timer_get_time();
  if (rtcx.WriteTimeAtSecond(&tm_write) != 0) {
    Log.print("RTCx write failed\n");
    return rtcx_not_written;
  }
  Log.printf("RTCx write done: begun %ld us into the window, the clock started %ld us later\n",
             into_window_us, (long)rtcx.StartedAfterUs(write_start_us));

  // The RTC must still show the seconds written, then change to the next. A read that fails is tried again, up to
  // three times in a row.
  struct tm tm_now;
  int read_errors = 0;
  while (rtcx.ReadTime(&tm_now) != 0) {
    if (++read_errors >= 3) {
      Log.print("RTCx: its change of seconds could not be watched\n");
      return rtcx_written;
    }
  }
  if (tm_now.tm_sec != tm_write.tm_sec) {
    Log.print("RTCx: its first change of seconds was missed\n");
    return rtcx_written;
  }
  wait_start_ms = millis();
  while (tm_now.tm_sec == tm_write.tm_sec) {
    if (millis() - wait_start_ms >= 1200) {
      Log.print("RTCx: its seconds did not change within 1.2s\n");
      return rtcx_written;
    }
    if (rtcx.ReadTime(&tm_now) == 0) {
      read_errors = 0;
    }
    else if (++read_errors >= 3) {
      Log.print("RTCx: its change of seconds could not be watched\n");
      return rtcx_written;
    }
  }
  int64_t change_us = esp_timer_get_time();
  if (tm_now.tm_sec != (tm_write.tm_sec + 1) % 60) {
    Log.print("RTCx: its seconds did not advance by one\n");
    return rtcx_written;
  }
  // The system clock's second began lead_us - into_window_us after the start of the write; the RTC's began at its
  // change of seconds. Both on the monotonic clock, so a second's worth of ambiguity does not come into it.
  error_us = (long)(change_us - write_start_us) - lead_us + into_window_us;
  Log.printf("RTCx: its seconds begin %+ld us from the system clock's (lead %ld us)\n", error_us, lead_us);
  return rtcx_checked;
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
    // once per NTP sync, here in loop(): the aligned write and the check of the RTC's first change of seconds, then
    // one more write with the corrected lead if the check was off by 20ms or more (up to about 5s in all)
    for (int attempt = 0; attempt < 2; ++attempt) {
      long error_us = 0;
      if (RtcxWriteAligned(rtcx_lead_us, error_us) != rtcx_checked) {
        break;
      }
      if (error_us > -20000L && error_us < 20000L) {
        break;
      }
      long lead_us = rtcx_lead_us + error_us;  // late by error_us: start that much earlier next time
      if (lead_us < 100000L || lead_us > 1900000L) {
        Log.print("RTCx: the correction is not credible, the lead is left as it is\n");
        break;
      }
      rtcx_lead_us = lead_us;
      if (attempt == 0) {
        Log.print("RTCx: written again with the corrected lead\n");
      }
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
    // A read that fails is tried again, up to three times in a row.
    struct tm tm_now = tm_init;
    int read_errors = 0;
    unsigned long wait_start_ms = millis();
    while (tm_now.tm_sec == tm_init.tm_sec && millis() - wait_start_ms < 1100) {
      if (rtcx.ReadTime(&tm_now) == 0) {
        read_errors = 0;
      }
      else if (++read_errors >= 3) {
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
