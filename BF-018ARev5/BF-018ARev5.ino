// copyright 2025 BotanicFields, Inc.
// BF-018A Rev.5
// JJY Simulator for ATOM Lite / ATOM Matrix / ATOMS3 Lite, with a web UI
//
#include <M5Unified.h>
#include <FastLED.h>      // https://github.com/FastLED/FastLED
#include <Ticker.h>
#include <WiFi.h>
#include <WiFiManager.h>  // https://github.com/tzapu/WiFiManager
#include <esp_timer.h>
#if ESP_ARDUINO_VERSION_MAJOR < 3
#include <lwip/tcpip.h>   // tcpip_callback(): the web UI's time sync restarts SNTP in the TCP/IP task on board package 2.x
#endif
#include "BF_Log.h"
#include "BF_Pcf8563.h"
#include "BF_RtcxNtp.h"
#include "BF_WebUi.h"

//..:....1....:....2....:....3....:....4....:....5....:....6....:....7..
// settings: the defaults below are used until they are changed on the web UI
WebUiSettings settings = {
  40000,           // jjy_frequency: 40kHz(east), 60kHz(west)
  "JST-9",         // time_zone
  "pool.ntp.org",  // ntp_server
};

//..:....1....:....2....:....3....:....4....:....5....:....6....:....7..
// for the web UI: http://jjy-sim.local/
const char* web_hostname = "jjy-sim";
const char* web_user     = "admin";
const char* web_password = "";  // set a password to allow changes from the web UI; empty: the page is read-only

//..:....1....:....2....:....3....:....4....:....5....:....6....:....7..
// for TCO(Time Code Output)
struct tm       td;  // time of day: year, month, day, day of week, hour, minute, second
struct timespec ts;  // time spec: second, nano-second

// statistics of the 100ms slots, copied once a minute for loop() to print and for the web UI
// (defined before the first function: the Arduino build inserts its prototypes there, and TcoStatsGet() returns it)
struct tco_stats_t {
  uint32_t seq;  // counts the copies: loop() prints when it changes
  int      count;
  int      distribution[9];
  double   sum;
  double   sq_sum;
  int      min;
  int      max;
};

//..:....1....:....2....:....3....:....4....:....5....:....6....:....7..
// for NTP
bool          rtcx_available(false);
volatile bool localtime_valid(false);  // set in loop(), read by the Ticker task
time_t        ntp_last_sync(0);        // when SNTP last set the time, 0: not yet

//..:....1....:....2....:....3....:....4....:....5....:....6....:....7..
// for WiFi
const          int wifi_config_portal_timeout_sec(60);
const unsigned int wifi_retry_interval_ms(60000);
      unsigned int wifi_retry_last_ms(0);
const          int wifi_retry_max_times(3);       // restart after this many failed retries while there is no valid time or no stored Wi-Fi
const          int wifi_restart_retry_times(360); // with a valid time, restart only after this many (about 6 hours): a stuck Wi-Fi driver
               int wifi_retry_times(0);
              bool wifi_saved(false);              // Wi-Fi settings are stored, set in setup()
              bool wifi_portal_active(false);      // the configuration portal is running (green LED)

wl_status_t wifi_status(WL_NO_SHIELD);
const char* wl_status_str[] = {
  "WL_IDLE_STATUS",      // 0
  "WL_NO_SSID_AVAIL",    // 1
  "WL_SCAN_COMPLETED",   // 2
  "WL_CONNECTED",        // 3
  "WL_CONNECT_FAILED",   // 4
  "WL_CONNECTION_LOST",  // 5
  "WL_DISCONNECTED",     // 6
  "WL_NO_SHIELD",        // 7 <-- 255
  "wl_status invalid",   // 8
};

//..:....1....:....2....:....3....:....4....:....5....:....6....:....7..
// for FastLED
const int led_pin_atom(27);        // GPIO27 for ATOM Lite/Matrix
const int led_pin_atoms3lite(35);  // GPIO35 for ATOMS3 Lite
const unsigned char led_num(25);   // ATOM Matrix: 25, ATOM lite/ATOMS3 Lite: 1 --> 25
CRGB leds[led_num]; 

const int led_on_r(0x80);                // 0..0xFF
const int led_on_g(0x80);                // 0..0xFF
const int led_on_b(0x80);                // 0..0xFF
const int led_brightness(40);            // 0..0xFF
const unsigned int blink_slow_ms(1000);  // blink period
const unsigned int blink_fast_ms( 200);  // blink period

enum led_r_t {
  led_r_off,   // WL_CONNECTED
  led_r_slow,  // WL_NO_SSID_AVAIL
  led_r_fast,  // WL_DISCONNECTED
  led_r_on,    // WL_IDLE_STATUS, WL_CONNECTION_LOST, etc.
};
enum led_g_t {
  led_g_off,   // time valid
  led_g_slow,  // waiting time valid
  led_g_fast,  //
  led_g_on,    // WiFi configuration portal is active
};
enum led_b_t {
  led_b_off,   // TCO off
  led_b_slow,  //
  led_b_fast,  //
  led_b_on,    // TCO on
};
led_r_t led_r(led_r_off);
led_b_t led_b(led_b_off);  // green: see LedGreen()

bool led_enable(true);

//..:....1....:....2....:....3....:....4....:....5....:....6....:....7..
// LED
// green: the configuration portal, else whether the time is valid. Worked out here instead of being written by
// the Ticker task and the portal callback.
led_g_t LedGreen()
{
  if (wifi_portal_active) {
    return led_g_on;
  }
  return localtime_valid ? led_g_off : led_g_slow;
}

void LedShow()
{
  CRGB led(0);
  if (led_enable) {
    switch (led_r) {
      case led_r_on  : led.r = led_on_r;  break;
      case led_r_fast: led.r = LedBlink(blink_fast_ms) ? led_on_r : 0;  break;
      case led_r_slow: led.r = LedBlink(blink_slow_ms) ? led_on_r : 0;  break;
      default:  break;  // led.r = 0
    }
    switch (LedGreen()) {
      case led_g_on  : led.g = led_on_g;  break;
      case led_g_fast: led.g = LedBlink(blink_fast_ms) ? led_on_g : 0;  break;
      case led_g_slow: led.g = LedBlink(blink_slow_ms) ? led_on_g : 0;  break;
      default:  break;  // led.g = 0
    }
    switch (led_b) {
      case led_b_on  : led.b = led_on_b;  break;
      case led_b_fast: led.b = LedBlink(blink_fast_ms) ? led_on_b : 0;  break;
      case led_b_slow: led.b = LedBlink(blink_slow_ms) ? led_on_b : 0;  break;
      default:  break;  // led.b = 0
    }
    if (wifi_portal_active) {
      led.b = 0;  // plain green: while the portal blocks setup() this runs only once, with the carrier at any state
    }
  }
  leds[0] = led;
  FastLED.show();
}

bool LedBlink(unsigned int period_ms)
{
  return millis() / period_ms % 2 != 0;
}

//..:....1....:....2....:....3....:....4....:....5....:....6....:....7..
// WiFi
const char* WlStatus(wl_status_t wl_status)
{
  if (wl_status >= 0 && wl_status <= 6) {
    return wl_status_str[wl_status];
  }
  if (wl_status == 255) {
    return wl_status_str[7];
  }
  return wl_status_str[8];
}

// failed retries after which WifiCheck() restarts the device
int WifiRestartAfter()
{
  // A rejected password (WL_CONNECT_FAILED) is not retried by the driver and does not go away: restart soon, so that
  // the configuration portal reopens. The same without stored Wi-Fi settings, and without a valid time, when there
  // is no time signal to lose. With a valid time and a lost network, a restart only silences the signal for about
  // a minute, so it is the last resort, for a stuck Wi-Fi driver.
  if (wifi_status == WL_CONNECT_FAILED || !wifi_saved || !localtime_valid) {
    return wifi_retry_max_times;
  }
  return wifi_restart_retry_times;
}

void WifiCheck()
{
  wl_status_t wifi_status_new = WiFi.status();
  if (wifi_status != wifi_status_new) {
    wifi_status = wifi_status_new;
    Log.printf("[WiFi]%s\n", WlStatus(wifi_status));
    switch (wifi_status) {
      case WL_CONNECTED    : led_r = led_r_off;   break;
      case WL_NO_SSID_AVAIL: led_r = led_r_slow;  break;
      case WL_DISCONNECTED : led_r = led_r_fast;  break;
      default              : led_r = led_r_on;    break;  // state transition also
    }
  }

  // retry interval
  if (millis() - wifi_retry_last_ms < wifi_retry_interval_ms) {
    return;
  }
  wifi_retry_last_ms = millis();

  // let the wifi process do unless it has given up: connected, or still trying (WL_NO_SSID_AVAIL, WL_IDLE_STATUS, ..)
  if (wifi_status != WL_DISCONNECTED && wifi_status != WL_CONNECT_FAILED) {
    wifi_retry_times = 0;
    return;
  }
  ++wifi_retry_times;

  // restart as the last resort, or soon when that is what reopens the configuration portal: see WifiRestartAfter()
  if (wifi_retry_times > WifiRestartAfter()) {
    Log.print("[WiFi]not connected for too long: rebooting..\n");
    ESP.restart();
    return;
  }

  // reconnect with the stored settings. WiFi.reconnect() fails while the driver is still busy with its own attempt,
  // and the driver does not retry by itself after WL_CONNECT_FAILED, so disconnect first and start again.
  Log.printf("[WiFi]reconnect %d\n", wifi_retry_times);
  WiFi.disconnect();
  WiFi.begin();
}

void WifiConfigModeCallback(WiFiManager *wm)
{
  wifi_portal_active = true;  // green LED indicates configuration portal is active
  LedShow();
}

//..:....1....:....2....:....3....:....4....:....5....:....6....:....7..
// TCO(Time Code Output)
Ticker tk;
const int ticker_interval_ms(10);  // poll every 10ms, TcoGen() acts once per 100ms slot
const int marker(0xff);  // marker code TcoValue() returns

// PWM for TCO signal
const uint8_t  ledc_pin_atom(22);   // GPIO22 for ATOM Lite/Matrix
const uint8_t  ledc_pin_atoms3(5);  // GPIO5 for ATOMS3 Lite
const uint8_t  ledc_channel(0);     // board package 2.x drives LEDC by channel
const uint8_t  ledc_resolution(8);  // 2^8 = 256
const uint32_t ledc_duty_on(128);   // 128/256 = 50%
const uint32_t ledc_duty_off(0);    // 0
      uint8_t  ledc_pin(ledc_pin_atom);
      uint32_t ledc_frequency(0);   // the carrier LEDC makes: close to settings.jjy_frequency

// statistics of the 100ms slots (tco_stats_t is defined at the top)
tco_stats_t  tco_stats = {};
portMUX_TYPE tco_stats_mux = portMUX_INITIALIZER_UNLOCKED;

void TcoInit()
{
  if (M5.getBoard() == m5::board_t::board_M5AtomS3Lite) {
    ledc_pin = ledc_pin_atoms3;
  }
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  Log.printf("ledcAttach result= %d\n", ledcAttach(ledc_pin, settings.jjy_frequency, ledc_resolution));
  // ledcReadFreq() reports 0 while the duty is 0, so take the frequency from ledcChangeFrequency(): it sets the
  // same timer values again and returns the frequency the timer really makes.
  ledc_frequency = ledcChangeFrequency(ledc_pin, settings.jjy_frequency, ledc_resolution);
  Log.printf("ledcWrite result= %d\n", ledcWrite(ledc_pin, ledc_duty_off));  // carrier off until the time is valid
  Log.printf("pin= %u, duty= %lu, freq= %lu\n", ledc_pin, (unsigned long)ledcRead(ledc_pin), (unsigned long)ledc_frequency);
#else
  ledc_frequency = ledcSetup(ledc_channel, settings.jjy_frequency, ledc_resolution);
  ledcAttachPin(ledc_pin, ledc_channel);
  ledcWrite(ledc_channel, ledc_duty_off);  // carrier off until the time is valid
  Log.printf("pin= %u, channel= %u, freq= %lu\n", ledc_pin, ledc_channel, (unsigned long)ledc_frequency);
#endif

  // start Ticker for TCO
  // No need to align the Ticker to the 100ms slots: TcoGen() reads the slot from the clock on every call.
  tk.attach_ms(ticker_interval_ms, TcoGen);
}

// main task of TCO: every 10ms in the Ticker (esp_timer) task
void TcoGen()
{
  // statistics of ts_nsec
  static int    tk_count(0);
  static int    tk_max(0);
  static int    tk_min(0);
  static double tk_sum(0.0);
  static double tk_sq_sum(0.0);
  static int    tk_distribution[9] = {0, 0, 0, 0, 0, 0, 0, 0, 0};
  static int    tk_last_nsec(0);
  static int    tk_last_slot(-1);   // the 100ms slot handled last
  static time_t tk_sec(0);          // the second whose time code value is in tk_on_slots
  static int    tk_on_slots(0);     // the carrier is on in slots 0 .. tk_on_slots-1 of that second: 2, 5 or 8
  static bool    tk_carrier(false);  // the carrier state last written
  static time_t  tk_off_sec(0);      // the second in which the carrier was last switched off
  static int64_t tk_last_ns(0);      // the clock at the previous call, to measure a step back
  static int     tk_hold_calls(0);   // calls spent holding the carrier on after a small step back, 0: not holding

  if (!localtime_valid) {
    if (tk_carrier) {
      tk_carrier = false;
      TcOff();
    }
    tk_last_slot = -1;
    tk_sec = 0;
    tk_hold_calls = 0;
    return;
  }

  // The carrier is keyed by level, not by edge: every call works out from the clock whether the carrier should be
  // on right now, and writes it only when that changes. A late call or a small clock step then delays an edge by
  // one call (10ms) at most, and whatever happens, the next second is sent right. A step forward past a pulse
  // still loses that pulse, and a large step back still costs one second: the time itself moved.
  clock_gettime(CLOCK_REALTIME, &ts);
  int64_t now_ns = (int64_t)ts.tv_sec * 1000000000LL + ts.tv_nsec;
  int64_t step_back_ns = tk_last_ns - now_ns;  // > 0 when the clock was stepped back since the previous call
  tk_last_ns = now_ns;
  int ts_100ms = ts.tv_nsec / 100000000;
  if (ts.tv_sec < tk_sec) {
    // The clock was stepped back into a second already sent. While that second's pulse is still on and the step
    // is small, keep it on until the clock reaches that second again: the pulse is stretched by the step, which
    // keeps its meaning below 150ms. Otherwise start afresh with the new time, so that second is sent again.
    bool hold = (tk_hold_calls > 0) ? (tk_hold_calls < 20)
                                    : (tk_carrier && tk_off_sec != tk_sec && step_back_ns < 150000000LL);
    if (hold) {
      ++tk_hold_calls;
      return;
    }
    tk_sec = 0;
  }
  tk_hold_calls = 0;
  bool new_second = false;
  if (ts.tv_sec != tk_sec) {
    bool fresh = (tk_sec == 0);
    tk_sec = ts.tv_sec;
    localtime_r(&ts.tv_sec, &td);  // td from the same clock reading as ts
    int value = TcoValue();
    tk_on_slots = (value == marker) ? 2 : (value != 0) ? 5 : 8;  // 0.2s, 0.5s, 0.8s
    if (fresh && ts_100ms != 0) {
      tk_off_sec = tk_sec;  // starting in the middle of a second: no partial pulse, the next second starts clean
    }
    new_second = true;
  }
  // on in the first tk_on_slots slots, and never on again in a second once it has been switched off: a small step
  // back within the second would otherwise add a short extra pulse
  bool carrier = (ts_100ms < tk_on_slots) && (tk_sec != tk_off_sec);
  if (carrier != tk_carrier) {
    tk_carrier = carrier;
    if (carrier) {
      TcOn();
    }
    else {
      tk_off_sec = tk_sec;
      TcOff();
      switch (tk_on_slots) {
        case 2:  Log.printf(" %d ", td.tm_sec);  break;  // marker
        case 5:  Log.print("1");  break;
        default: Log.print("0");  break;
      }
    }
  }
  if (new_second && td.tm_sec == 0) {
    Log.print(&td, "\n%A %B %d %Y %H:%M:%S\n");  // after the carrier write, so the minute marker's edge is not delayed
  }

  // statistics: on the first call in each 100ms slot
  if (ts_100ms == tk_last_slot) {
    return;
  }
  tk_last_slot = ts_100ms;

  if (tk_count++ != 0) {
    int tk_deviation = ts.tv_nsec - tk_last_nsec;
    if (tk_deviation < 0) {
      tk_deviation += 1000000000;  // 0xx - 9xx ms --> 10xx - 9xx ms
    }
    tk_deviation -= 100000000; // center 100 ms --> 0

    if (tk_max < tk_deviation) tk_max = tk_deviation;
    if (tk_min > tk_deviation) tk_min = tk_deviation;
    tk_sum    += (double)tk_deviation;
    tk_sq_sum += (double)tk_deviation * (double)tk_deviation;

    if      (tk_deviation < -50000000) ++tk_distribution[0];  //     ~ -50ms
    else if (tk_deviation <  -5000000) ++tk_distribution[1];  //     ~  -5ms
    else if (tk_deviation <   -500000) ++tk_distribution[2];  //     ~  -0.5ms
    else if (tk_deviation <    -50000) ++tk_distribution[3];  //     ~  -0.05ms
    else if (tk_deviation <     50000) ++tk_distribution[4];  // -0.05 ~ 0.05ms
    else if (tk_deviation <    500000) ++tk_distribution[5];  //  0.05ms ~
    else if (tk_deviation <   5000000) ++tk_distribution[6];  //  0.5ms  ~
    else if (tk_deviation <  50000000) ++tk_distribution[7];  //  5ms    ~
    else                               ++tk_distribution[8];  // 50ms    ~
  }
  tk_last_nsec = ts.tv_nsec;

  // once a minute, a copy for loop() to print and for the web UI: nothing is formatted with %f in this task
  if ((td.tm_sec == 0) && (ts_100ms == 0)) {
    portENTER_CRITICAL(&tco_stats_mux);
    ++tco_stats.seq;
    tco_stats.count = tk_count;
    memcpy(tco_stats.distribution, tk_distribution, sizeof(tco_stats.distribution));
    tco_stats.sum    = tk_sum;
    tco_stats.sq_sum = tk_sq_sum;
    tco_stats.min    = tk_min;
    tco_stats.max    = tk_max;
    portEXIT_CRITICAL(&tco_stats_mux);
  }
}

// statistics of the 100ms slots: the latest copy, its average and standard deviation, and the line loop() prints
tco_stats_t TcoStatsGet()
{
  tco_stats_t stats;
  portENTER_CRITICAL(&tco_stats_mux);
  stats = tco_stats;
  portEXIT_CRITICAL(&tco_stats_mux);
  return stats;
}

double TcoStatsAverage(const tco_stats_t &stats)
{
  return stats.count > 0 ? stats.sum / (double)stats.count : 0.0;
}

double TcoStatsStdDeviation(const tco_stats_t &stats)
{
  if (stats.count <= 0) {
    return 0.0;
  }
  double variance = (stats.sq_sum - stats.sum * stats.sum / (double)stats.count) / (double)stats.count;
  return variance > 0.0 ? sqrt(variance) : 0.0;
}

void TcoStatsPrint()
{
  static uint32_t printed_seq(0);
  tco_stats_t stats = TcoStatsGet();
  if (stats.seq == printed_seq) {
    return;
  }
  printed_seq = stats.seq;
  char line[256];  // one write, so the Ticker task's bit characters cannot land inside the line
  int n = 0;
  for (int i = 0; i < 9 && n < (int)sizeof(line); ++i) {
    n += snprintf(line + n, sizeof(line) - n, "%d ", stats.distribution[i]);
  }
  if (n < (int)sizeof(line)) {
    snprintf(line + n, sizeof(line) - n, "\nn= %d, ave= %.4f  sdv= %.4f  min= %d  max= %d\n",
             stats.count, TcoStatsAverage(stats), TcoStatsStdDeviation(stats), stats.min, stats.max);
  }
  Log.print(line);
}

void TcOn()
{
  CarrierWrite(ledc_duty_on);
  led_b = led_b_on;
}

void TcOff()
{
  CarrierWrite(ledc_duty_off);
  led_b = led_b_off;
}

void CarrierWrite(uint32_t duty)
{
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcWrite(ledc_pin, duty);
#else
  ledcWrite(ledc_channel, duty);
#endif
}

// TCO value
//   marker, 1:not zero, 0:zero
int TcoValue()
{
  int bcd_hour = Int3Bcd(td.tm_hour);
  int parity_bcd_hour = Parity8(bcd_hour);

  int bcd_minute = Int3Bcd(td.tm_min);
  int parity_bcd_minute = Parity8(bcd_minute);

  int year = td.tm_year + 1900;
  int bcd_year = Int3Bcd(year);

  int bcd_days = Int3Bcd(td.tm_yday + 1);  // day of the year 1..366, td.tm_yday starts from 0

  int day_of_week = td.tm_wday;

  int tco;
  switch (td.tm_sec) {
    case  0: tco = marker;              break;
    case  1: tco = bcd_minute & 0x40;   break;
    case  2: tco = bcd_minute & 0x20;   break;
    case  3: tco = bcd_minute & 0x10;   break;
    case  4: tco = 0;                   break;
    case  5: tco = bcd_minute & 0x08;   break;
    case  6: tco = bcd_minute & 0x04;   break;
    case  7: tco = bcd_minute & 0x02;   break;
    case  8: tco = bcd_minute & 0x01;   break;
    case  9: tco = marker;              break;

    case 10: tco = 0;                   break;
    case 11: tco = 0;                   break;
    case 12: tco = bcd_hour & 0x20;     break;
    case 13: tco = bcd_hour & 0x10;     break;
    case 14: tco = 0;                   break;
    case 15: tco = bcd_hour & 0x08;     break;
    case 16: tco = bcd_hour & 0x04;     break;
    case 17: tco = bcd_hour & 0x02;     break;
    case 18: tco = bcd_hour & 0x01;     break;
    case 19: tco = marker;              break;

    case 20: tco = 0;                   break;
    case 21: tco = 0;                   break;
    case 22: tco = bcd_days & 0x200;    break;
    case 23: tco = bcd_days & 0x100;    break;
    case 24: tco = 0;                   break;
    case 25: tco = bcd_days & 0x080;    break;
    case 26: tco = bcd_days & 0x040;    break;
    case 27: tco = bcd_days & 0x020;    break;
    case 28: tco = bcd_days & 0x010;    break;
    case 29: tco = marker;              break;

    case 30: tco = bcd_days & 0x008;    break;
    case 31: tco = bcd_days & 0x004;    break;
    case 32: tco = bcd_days & 0x002;    break;
    case 33: tco = bcd_days & 0x001;    break;
    case 34: tco = 0;                   break;
    case 35: tco = 0;                   break;
    case 36: tco = parity_bcd_hour;     break;
    case 37: tco = parity_bcd_minute;   break;
    case 38: tco = 0;                   break;
    case 39: tco = marker;              break;

    case 40: tco = 0;                   break;
    case 41: tco = bcd_year & 0x80;     break;
    case 42: tco = bcd_year & 0x40;     break;
    case 43: tco = bcd_year & 0x20;     break;
    case 44: tco = bcd_year & 0x10;     break;
    case 45: tco = bcd_year & 0x08;     break;
    case 46: tco = bcd_year & 0x04;     break;
    case 47: tco = bcd_year & 0x02;     break;
    case 48: tco = bcd_year & 0x01;     break;
    case 49: tco = marker;              break;

    case 50: tco = day_of_week & 0x04;  break;
    case 51: tco = day_of_week & 0x02;  break;
    case 52: tco = day_of_week & 0x01;  break;
    case 53: tco = 0;                   break;
    case 54: tco = 0;                   break;
    case 55: tco = 0;                   break;
    case 56: tco = 0;                   break;
    case 57: tco = 0;                   break;
    case 58: tco = 0;                   break;
    case 59: tco = marker;              break;
    default: tco = 0;                   break;
  }
  return tco;
}

int Int3Bcd(int a)
{
  return (a % 10) + (a / 10 % 10 * 16) + (a / 100 % 10 * 256);
}

int Parity8(int a)
{
  int pa = a;
  for (int i = 1; i < 8; ++i) {
    pa += a >> i;
  }
  return pa % 2;
}

//..:....1....:....2....:....3....:....4....:....5....:....6....:....7..
// web UI: the status the page shows, and the actions it asks for
const char* BoardName()
{
  switch (M5.getBoard()) {
    case m5::board_t::board_M5AtomS3Lite: return "ATOMS3 Lite";
    case m5::board_t::board_M5AtomLite  : return "ATOM Lite";
    case m5::board_t::board_M5AtomMatrix: return "ATOM Matrix";
    default                             : return "ESP32";
  }
}

const char* ResetReason()
{
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON : return "Power on";
    case ESP_RST_EXT     : return "Reset pin";
    case ESP_RST_SW      : return "Software restart";
    case ESP_RST_PANIC   : return "Crash";
    case ESP_RST_INT_WDT :
    case ESP_RST_TASK_WDT:
    case ESP_RST_WDT     : return "Watchdog";
    case ESP_RST_BROWNOUT: return "Brownout";
    default              : return "Other";
  }
}

double Finite(double a)
{
  return std::isfinite(a) ? a : 0.0;
}

void WebUiStatusJson(Print &out)
{
  struct timeval tv;
  gettimeofday(&tv, NULL);
  struct tm local_tm;
  struct tm utc_tm;
  localtime_r(&tv.tv_sec, &local_tm);
  gmtime_r(&tv.tv_sec, &utc_tm);
  int days = local_tm.tm_yday - utc_tm.tm_yday;  // -1..1, or about -365/+365 across the new year
  if (days >  1) days = -1;
  if (days < -1) days = 1;
  long utc_offset = days * 86400L + (local_tm.tm_hour - utc_tm.tm_hour) * 3600L
                  + (local_tm.tm_min - utc_tm.tm_min) * 60L + (local_tm.tm_sec - utc_tm.tm_sec);
  char zone[16];
  strftime(zone, sizeof(zone), "%Z", &local_tm);

  tco_stats_t stats = TcoStatsGet();

  const char* time_source = ntp_last_sync != 0 ? "ntp" : localtime_valid ? "rtc" : "none";

  out.printf("{\"auth\":%s,\"host\":", WebUiAuthEnabled() ? "true" : "false");
  WebUiJsonString(out, web_hostname);
  out.printf(",\"board\":\"%s\",\"uptime\":%lu,\"reset\":\"%s\",\"heap\":%lu",
             BoardName(), (unsigned long)(esp_timer_get_time() / 1000000), ResetReason(), (unsigned long)ESP.getFreeHeap());
  out.printf(",\"epoch_ms\":%lld,\"utc_offset\":%ld,\"zone\":", (long long)tv.tv_sec * 1000 + tv.tv_usec / 1000, utc_offset);
  WebUiJsonString(out, zone);
  out.printf(",\"time_valid\":%s,\"time_source\":\"%s\",\"last_sync\":%lld,\"sync_interval\":%lu,\"rtc\":%s",
             localtime_valid ? "true" : "false", time_source, (long long)ntp_last_sync,
             (unsigned long)(sntp_get_sync_interval() / 1000), rtcx_available ? "true" : "false");
  out.print(",\"tz\":");
  WebUiJsonString(out, settings.time_zone);
  out.print(",\"ntp\":");
  WebUiJsonString(out, settings.ntp_server);
  out.printf(",\"freq\":%lu,\"freq_actual\":%lu,\"pin\":%u",
             (unsigned long)settings.jjy_frequency, (unsigned long)ledc_frequency, ledc_pin);
  out.printf(",\"wifi\":{\"status\":\"%s\",\"ssid\":", WlStatus(wifi_status));
  WebUiJsonString(out, WiFi.SSID().c_str());
  out.printf(",\"rssi\":%d,\"ip\":\"%s\",\"retries\":%d,\"retry_max\":%d}",
             WiFi.RSSI(), WiFi.localIP().toString().c_str(), wifi_retry_times, WifiRestartAfter());
  out.printf(",\"led\":{\"enabled\":%s,\"r\":%d,\"g\":%d,\"b\":%d}",
             led_enable ? "true" : "false", (int)led_r, (int)LedGreen(), (int)led_b);
  out.printf(",\"stats\":{\"n\":%d,\"b\":[", stats.count);
  for (int i = 0; i < 9; ++i) {
    if (i != 0) {
      out.print(',');
    }
    out.print(stats.distribution[i]);
  }
  out.printf("],\"ave\":%.4f,\"sdv\":%.4f,\"min\":%d,\"max\":%d}}",
             Finite(TcoStatsAverage(stats)), Finite(TcoStatsStdDeviation(stats)), stats.min, stats.max);
}

void WebUiSetLed(bool enable)
{
  led_enable = enable;
}

#if ESP_ARDUINO_VERSION_MAJOR < 3
static void SntpRestart(void *)
{
  sntp_stop();  // runs in the TCP/IP task
  sntp_init();
}
#endif

// restarting SNTP makes it ask the server now
void WebUiSyncTime()
{
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  NtpBegin(settings.time_zone, settings.ntp_server);  // configTzTime() takes the lwIP core lock on board package 3.x
#else
  // board package 2.x has no lwIP core locking, so restart SNTP inside the TCP/IP task rather than from loop()
  if (tcpip_callback(SntpRestart, nullptr) != ERR_OK) {
    Log.print("[Web]time sync: the TCP/IP task did not take the request\n");
  }
#endif
}

//..:....1....:....2....:....3....:....4....:....5....:....6....:....7..
// main
const unsigned int loop_period_ms(100);
      unsigned int loop_last_ms;
const int button_atom(39);    // GPIO39
const int button_atoms3(41);  // GPIO41

void setup()
{
  // Wi-Fi issue caused by the CH552 on ATOM Lite/Matrix: M5.begin() applies M5Unified's own countermeasure
  // (it drives GPIO0 HIGH), so the Rev.2 workaround of driving GPIO0 LOW is not needed here.
  // https://twitter.com/wakwak_koba/status/1553162622479974400
  // https://www.facebook.com/groups/154504605228235/posts/699719300706760/

  // M5Unified
  auto cfg = M5.config();
  cfg.serial_baudrate = 115200;  // default=0. use Log.print(). 
  cfg.external_rtc = true;       // default=false. use Unit RTC.
                                 // Keep it: M5.begin() then starts Wire on the GROVE port, which rtcx.Begin(Wire) needs.
  M5.begin(cfg);
#if ARDUINO_USB_CDC_ON_BOOT && ARDUINO_USB_MODE
  // ATOMS3 Lite: Serial is the USB Serial/JTAG port (HWCDC). TcoGen() prints from the Ticker task, so it must not
  // wait for a USB host that has stopped reading: output that can't be queued at once is dropped instead.
  // 1ms, not 0: from arduino-esp32 2.0.15 on, HWCDC::write() retries once per ms until its counter reaches 0,
  // so 0 never gives up (the 2.0.14 core in M5Stack board package 2.1.4 gives up at once either way).
  Serial.setTxTimeoutMs(1);
#endif
  Log.println();

  // settings saved from the web UI
  WebUiLoadSettings(&settings);
  Log.printf("settings: %lu Hz, %s, %s\n", (unsigned long)settings.jjy_frequency, settings.time_zone, settings.ntp_server);

  // FastLED
#if defined (CONFIG_IDF_TARGET_ESP32S3)  // FastLED requires strict constant for the pin number 
  FastLED.addLeds<WS2811, led_pin_atoms3lite, GRB>(leds, led_num);
  Log.println("FastLED add LEDs for ATOMS3 Lite");
#else
  pinMode(led_pin_atom, OUTPUT);  // M5.begin() leaves GPIO27 routed to the touch sensor (RTC IO); this routes it back
  FastLED.addLeds<WS2811, led_pin_atom, GRB>(leds, led_num);
  Log.println("FastLED add LEDs for M5Atom Lite/Matrix");
#endif
  FastLED.setBrightness(led_brightness);
  for (int i = 0; i < led_num; ++i) {
    leds[i] = 0;
  }

  // Unit RTC (External)
  if (rtcx.Begin(Wire) == 0) {
    rtcx_available = true;
    if (SetTimeFromRtcx(settings.time_zone)) {
      localtime_valid = true;
    }
  }
  if (!localtime_valid) {
    Log.print("RTC not valid: set the localtime temporarily\n");
    td.tm_year = 117;  // 2017 > 2016, getLocalTime() returns true
    td.tm_mon  = 0;    // January
    td.tm_mday = 1;
    td.tm_hour = 0;
    td.tm_min  = 0;
    td.tm_sec  = 0;
    struct timeval tv = { mktime(&td), 0 };
    settimeofday(&tv, NULL);
  }
  getLocalTime(&td);
  Log.print(&td, "localtime: %A, %B %d %Y %H:%M:%S\n");
  // print sample: must be < 64
  //....:....1....:....2....:....3....:....4....:....5....:....6....
  //localtime: Wednesday, September 11 2021 11:10:46

  // TCO start: before Wi-Fi, so that with a valid RTC time the signal starts at once and keeps going while
  // WiFiManager connects or runs its portal (the Ticker task does not depend on this one)
  TcoInit();

  // WiFi start
  WiFiManager wm;  // blocking mode only

  // erase SSID/Key to force rewrite
  int button_pin = button_atom;
  if (M5.getBoard() == m5::board_t::board_M5AtomS3Lite) {
    button_pin = button_atoms3;
  }
  if (digitalRead(button_pin) == LOW) {
    wm.resetSettings();
  }

  // WiFi connect
  wm.setConfigPortalTimeout(wifi_config_portal_timeout_sec);
  wm.setAPCallback(WifiConfigModeCallback);
  wm.setHostname(web_hostname);  // the name the router shows
  wm.autoConnect();
  wifi_portal_active = false;
  wifi_saved = wm.getWiFiIsSaved();
  WiFi.setSleep(false);  // https://macsbug.wordpress.com/2021/05/02/buttona-on-m5stack-does-not-work-properly/
  wifi_retry_last_ms = millis() - wifi_retry_interval_ms;

  // NTP start
  NtpBegin(settings.time_zone, settings.ntp_server);

  // web UI start
  WebUiBegin(web_hostname, web_user, web_password);

  // clear button of erase SSID/Key
  M5.update();

  // loop control
  loop_last_ms = millis();
}

void loop()
{
  M5.update();
  LedShow();

  WifiCheck();
  if (RtcxUpdate(rtcx_available)) {
    localtime_valid = true;  // SNTP sync completed
    ntp_last_sync = time(NULL);
  };
  TcoStatsPrint();  // the once-a-minute statistics line, printed here rather than from the Ticker task
  WebUiHandle();

  // button: TCO monitor on/off
  if (M5.BtnA.wasReleased()) {
    led_enable = !led_enable;
  }

  // loop control
  unsigned int delay_ms(0);
  unsigned int elapse_ms = millis() - loop_last_ms;
  if (elapse_ms < loop_period_ms) {
    delay_ms = loop_period_ms - elapse_ms;
  }
  delay(delay_ms);
  loop_last_ms = millis();
//  Log.printf("loop elapse = %dms\n", elapse_ms);  // for monitoring elapsed time
}
