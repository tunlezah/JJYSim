// Copyright 2021 BotanicFields, Inc.
// for RTCx(PCf8563) and NTP
//
#include <Arduino.h>
#include <Preferences.h>
#include <esp_timer.h>
#include "BF_Log.h"
#include "BF_Pcf8563.h"
#include "BF_RtcxNtp.h"

volatile bool                      sntp_sync_status_complete(false);                   // set by the SNTP callback, cleared in loop()
static volatile sntp_sync_status_t sntp_sync_status_last(SNTP_SYNC_STATUS_RESET);  // what the callback saw, printed in loop()

// SNTP sets the clock in a single step. The ESP32's crystal drifts by up to a few tenths of a second over the default
// interval of 3 hours, enough to cost a pulse of the time code (see TcoGen()); over 30 minutes it stays well below
// the 0.1s that every pulse survives.
static const uint32_t ntp_sync_interval_ms(30UL * 60UL * 1000UL);

void NtpBegin(const char* time_zone, const char* ntp_server)
{
  sntp_set_sync_interval(ntp_sync_interval_ms);  // before configTzTime(), which starts SNTP
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

// The RTC holds local time. The time zone it was written in is kept in NVS, so that after a change of time zone on the
// page the RTC is not read in the new one: its time is then not used until NTP has set the clock and the RTC has been
// written again. A write in another zone first replaces the record with rtcx_zone_unknown, which no time zone can be
// (the page does not accept '*'), and stores the zone only once the write is complete: a power cut or a failed store
// in between leaves a record that does not match, never one that describes another write. So the flash sees two
// writes per change of time zone, and none otherwise. The record exists only once this firmware has written the RTC.
static const char *rtcx_prefs_namespace = "rtcx";
static const char *rtcx_zone_unknown    = "*";

// the time zone stored, or an empty string if there is none (a first start, or NVS not readable)
static String RtcxZoneStored()
{
  Preferences prefs;
  String zone;
  if (prefs.begin(rtcx_prefs_namespace, true)) {  // read-only: fails while the namespace does not exist yet
    zone = prefs.getString("tz", "");
    prefs.end();
  }
  return zone;
}

// Wait until the system clock is phase_us into a second (a 3ms window, up to 1.2s). false: no window in time.
static bool RtcxWaitForPhase(long phase_us)
{
  unsigned long wait_start_ms = millis();
  for (;;) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    long into_window_us = tv.tv_usec - phase_us;
    if (into_window_us < 0) {
      into_window_us += 1000000L;
    }
    if (into_window_us < 3000L) {
      return true;
    }
    if (millis() - wait_start_ms >= 1200) {
      return false;
    }
    delay(1);
  }
}

// Store the time zone record, if it differs from the one stored. true: the record now holds time_zone. A flash write
// holds the processor up, which would lengthen a pulse of the time code that is due to end meanwhile; so the write is
// made 0.95s into a second, when no pulse is on (TcoGen() starts a pulse at most 0.1s into a second, and the longest
// lasts 0.8s): however long the write takes, it can only delay the next pulse's start, and TcoGen() leaves out a pulse
// that would start too late.
static bool RtcxZoneStore(const char *time_zone)
{
  if (time_zone == nullptr) {
    return false;
  }
  if (RtcxZoneStored() == time_zone) {
    return true;
  }
  RtcxWaitForPhase(950000L);
  Preferences prefs;
  bool stored = prefs.begin(rtcx_prefs_namespace, false) && prefs.putString("tz", time_zone) == strlen(time_zone);
  prefs.end();
  Log.printf("RTCx: time zone %s %s\n", time_zone, stored ? "stored" : "could not be stored");
  return stored;
}

// Watch the RTC for a change of its seconds, for up to timeout_ms. The change is timed only when it falls between two
// successful readings begun at most 5ms apart: a reading that fails (an I2C timeout takes 50ms) or a hold-up of loop()
// leaves the moment of the change unknown, and the watch goes on for the next one. Up to three failed readings in a
// row are tolerated. true: tm_before and tm_after are the readings on either side of the change (fields other than
// the date and time are left as they were in tm_after), and change_us is when it was seen, on the monotonic clock.
static bool RtcxWatchChange(struct tm &tm_before, struct tm &tm_after, int64_t &change_us, unsigned long timeout_ms)
{
  bool have_before = false;
  int64_t before_us = 0;  // when the reading in tm_before was begun
  int read_errors = 0;
  unsigned long wait_start_ms = millis();
  while (millis() - wait_start_ms < timeout_ms) {
    int64_t read_us = esp_timer_get_time();
    if (rtcx.ReadTime(&tm_after) != 0) {
      have_before = false;
      if (++read_errors >= 3) {
        return false;
      }
      continue;
    }
    read_errors = 0;
    if (have_before && tm_after.tm_sec != tm_before.tm_sec) {
      change_us = esp_timer_get_time();
      if (change_us - before_us <= 5000) {
        return true;
      }
    }
    tm_before = tm_after;
    before_us = read_us;
    have_before = true;
  }
  return false;
}

// The RTC is written so that its seconds begin on the system clock's. The write runs with the RTC's clock stopped,
// and the time circuits make their first increment some time after the clock is started again: 0.5078s on the
// PCF8563 (datasheet 8.10, table 26), possibly 1s on a clone that resets its whole divider chain (the HYM8563 data
// sheet does not say). The lead below is that time plus the I2C transfers before the start. After each write the
// RTC's first change of seconds is timed on the processor's monotonic clock, and the lead is corrected from what is
// measured, so that the alignment comes out right on either chip; the log shows the result.
static long       rtcx_lead_us(507813L + 2000L);
static const long rtcx_transfer_us(2000L);  // the I2C transfers before the clock start, without hold-ups

enum rtcx_write_t { rtcx_not_written, rtcx_written, rtcx_checked };

// Wait for the system clock to reach lead_us before the start of a second (a 3ms window, up to 1.2s), write that
// second with the RTC's clock stopped and started again, then time the RTC's first change of seconds.
// rtcx_checked: error_us holds how long after the system clock's second the RTC's began (+: later), and delay_us how
// long after its clock start the RTC made its first increment, both to within the few ms the readings take.
// rtcx_written: the RTC was written but its change of seconds could not be timed. rtcx_not_written: no window in time
// (loop() was held up) or the write failed; the RTC is then left as it is.
static rtcx_write_t RtcxWriteAligned(long lead_us, long &error_us, long &delay_us)
{
  long start_us = 1000000L - lead_us;  // the write begins here in the second, so that the RTC's seconds begin at 0
  int offset_sec = 0;
  while (start_us < 0) {  // a lead of a second or more: write the second after the one in progress
    start_us += 1000000L;
    ++offset_sec;
  }

  // The window may run on into the next second, which is then the one in progress at the write.
  struct timeval tv;
  int64_t tv_us;  // when tv was read, on the monotonic clock
  long into_window_us;
  unsigned long wait_start_ms = millis();
  for (;;) {
    gettimeofday(&tv, NULL);
    tv_us = esp_timer_get_time();
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
  if (rtcx.WriteTimeAtSecond(&tm_write) != 0) {
    Log.print("RTCx write failed\n");
    return rtcx_not_written;
  }
  Log.printf("RTCx write done: begun %ld us into the window, the clock started %ld us later\n",
             into_window_us, (long)(rtcx.StartedUs() - tv_us));

  // The RTC must show the seconds written up to its first change of seconds, and then the next.
  struct tm tm_before = tm_write;
  struct tm tm_after = tm_write;
  int64_t change_us;
  if (!RtcxWatchChange(tm_before, tm_after, change_us, 1200)) {
    Log.print("RTCx: its change of seconds could not be timed\n");
    return rtcx_written;
  }
  if (tm_before.tm_sec != tm_write.tm_sec || tm_after.tm_sec != (tm_write.tm_sec + 1) % 60) {
    Log.print("RTCx: the change of seconds timed was not its first one after the write\n");
    return rtcx_written;
  }
  // The system clock's second began lead_us - into_window_us after tv was read; the RTC's began at its change of
  // seconds. Both on the monotonic clock, so a second's worth of ambiguity does not come into it.
  error_us = (long)(change_us - tv_us) - lead_us + into_window_us;
  delay_us = (long)(change_us - rtcx.StartedUs());
  Log.printf("RTCx: its seconds begin %+ld us from the system clock's (lead %ld us)\n", error_us, lead_us);
  return rtcx_checked;
}

// Write the RTC from the system clock, aligned, and time its first change of seconds. If its seconds begin 20ms or more
// off, or the change could not be timed, write it again, up to four writes in all (about 1s with a PCF8563; up to about
// 10s when readings fail: two untimed writes, then the one that measures a 1s chip, then the aligned one). The lead for the next write
// is the transfers plus the chip's own delay from its clock start to its first increment, as measured, not the lead
// plus the error: a write held up before the clock start would carry its hold-up into the lead. So a held-up write is
// simply done again, and a chip that takes 1s to its first increment is aligned by the second write.
static void RtcxWriteFromSystemClock()
{
  const char *time_zone = getenv("TZ");  // the zone of the local time about to be written
  if (time_zone == nullptr || *time_zone == '\0') {
    Log.print("RTCx not updated: no time zone set\n");
    return;
  }
  String zone_stored = RtcxZoneStored();
  if (zone_stored != time_zone && zone_stored != rtcx_zone_unknown && !RtcxZoneStore(rtcx_zone_unknown)) {
    // a write in another zone, and the record that would describe the old one could not be cleared: no write
    Log.print("RTCx not updated: its time zone record could not be cleared\n");
    return;
  }

  bool written = false;
  for (int attempt = 0; attempt < 4; ++attempt) {
    long error_us = 0;
    long delay_us = 0;
    rtcx_write_t result = RtcxWriteAligned(rtcx_lead_us, error_us, delay_us);
    if (result == rtcx_not_written) {
      break;     // the RTC is left as it is; the next NTP sync tries again
    }
    written = true;
    if (result == rtcx_written) {
      continue;  // its change of seconds could not be timed: write it again, to time it
    }
    long lead_us = rtcx_transfer_us + delay_us;
    bool credible = (lead_us >= 100000L && lead_us <= 1900000L);
    if (error_us > -20000L && error_us < 20000L) {
      // Aligned. The lead may only grow here: a hold-up after the clock start can only shorten the delay measured, the
      // readings lengthen it by 5ms at most; so the lead settles just above the chip's delay, and one held-up
      // measurement cannot shorten it for the rest of the uptime.
      if (credible && lead_us > rtcx_lead_us) {
        rtcx_lead_us = lead_us;
      }
      break;
    }
    if (!credible) {
      Log.printf("RTCx: a first increment %ld us after the clock start is not credible: the lead is left as it is\n", delay_us);
      break;
    }
    rtcx_lead_us = lead_us;
    if (attempt < 3) {
      Log.print("RTCx: written again\n");
    }
  }
  if (written) {
    RtcxZoneStore(time_zone);  // the write is complete: the record describes the RTC again
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
    RtcxWriteFromSystemClock();  // once per NTP sync, here in loop()
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

// ReadTime() with up to three tries on an I2C error (1); 2, the RTC has lost its time, is not tried again
static int RtcxReadTime(struct tm &tm_now)
{
  int result = rtcx.ReadTime(&tm_now);
  for (int i = 1; i < 3 && result == 1; ++i) {
    result = rtcx.ReadTime(&tm_now);
  }
  return result;
}

bool SetTimeFromRtcx(const char* time_zone)
{
  bool rtcx_valid(false);
  struct tm tm_init = {};
  // Whether daylight saving time was in effect when the RTC was written, if it is known (-1: let mktime() work it out
  // from the time zone's rules, which is wrong by an hour after a change while the device was off, and in the hour
  // that repeats when daylight saving time ends). With it, mktime() reads the RTC right across any change since.
  tm_init.tm_isdst = rtcx.IsdstWritten();

  setenv("TZ", time_zone, 1);
  tzset();  // assign timezone with setenv for mktime()

  // Accept only years after 2016: getLocalTime() treats older times as not set, and a 19xx year means the
  // RTC was set by other software (opposite century bit, and often UTC), so wait for NTP instead.
  // The RTC is used only when this firmware wrote it last, completely, in this time zone: its state says the write was
  // complete and whether daylight saving time was in effect, and the record names this zone. Otherwise its time may
  // be in another zone or in UTC, or the record may describe another write: an interrupted write (the marker), a
  // change of time zone, a write by other software (Rev.3/Rev.4, another M5 program: no daylight-saving state), or
  // an erased record. The device then waits for NTP, once, after which this firmware has written the RTC.
  String zone_stored = RtcxZoneStored();
  int isdst = rtcx.IsdstWritten();
  Log.printf("RTCx: last written %s, time zone recorded: %s\n",
             rtcx.TimeStale() ? "incompletely" : isdst == 1 ? "in daylight saving time" : isdst == 0 ? "in standard time"
                                                                   : "with no daylight-saving record (other or earlier software)",
             zone_stored.length() != 0 ? zone_stored.c_str() : "none");
  if (rtcx.TimeStale()) {
    Log.print("RTCx not used: its last write was interrupted or failed\n");
  }
  else if (isdst < 0) {
    Log.print("RTCx not used: last written by other software or an earlier version, in an unknown time zone\n");
  }
  else if (zone_stored != time_zone) {
    Log.print("RTCx not used: it holds the local time of another time zone, or of one not recorded\n");
  }
  else if (RtcxReadTime(tm_init) == 0 && tm_init.tm_year > 116) {
    // Read the RTC at a change of its seconds (within about 2s), so that the system clock is set within a few ms of
    // the RTC rather than up to a second late. The readings on either side of the change must be exactly one second
    // apart: one bad I2C reading, or an RTC that does not advance, must not become the time.
    struct tm tm_before = tm_init;
    struct tm tm_after = tm_init;  // tm_isdst stays as recorded: ReadTime() sets the date and time only
    int64_t change_us;
    bool clean = RtcxWatchChange(tm_before, tm_after, change_us, 2100);
    if (clean) {
      struct tm tm_first = tm_before;  // copies: mktime() alters its argument
      struct tm tm_change = tm_after;
      clean = (mktime(&tm_change) - mktime(&tm_first) == 1);
    }
    if (clean) {
      rtcx_valid = true;
      struct timeval tv = { mktime(&tm_after), 0 };
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
