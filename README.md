# JJY Simulator for M5Atom
## A JJY radio time signal simulator that runs on the ATOMS3 Lite and ATOM Lite/Matrix
Note: the standard ATOMS3 (the non-Lite model) is not supported.

### 2026-10-21 update
- Rev.3/Rev.4: the JJY pulse timing now stays correct after each NTP time adjustment. Previously an adjustment could occasionally leave the timing on a slot boundary, corrupting pulses until the next adjustment.
- Rev.3/Rev.4: RTC times before 2017 are now ignored. An RTC last set by other software (e.g. M5Unified) used to produce a garbage clock; the device now waits for NTP instead.
- Rev.3: on the ATOMS3 Lite, serial output can no longer stall the time signal when a connected PC stops reading the serial port.
- Rev.4: the 40 kHz carrier now stays off until the time is valid.
- Rev.4: likely fix for the ATOM Lite/Matrix LED not lighting up. It has not been tested on hardware yet, so Rev.3 is still recommended for those boards.
- Rev.3/Rev.4: removed the GPIO0 workaround code, which never ran. `M5.begin()` already applies M5Unified's own countermeasure (see 8(6)).
### 2025-08-15 update
- Added Rev.4, which supports version 3.x of the M5Stack board package (Boards Manager). Rev.4 can be used with the ATOMS3 Lite.
- Updated `BF-018ARev3.ino` to improve detection of the M5Atom Lite/Matrix.
### 2025-03-03 update
- Updated `BF-018ARev3.ino` to adapt to a specification change in M5Unified: `cfg.serial_baudrate` is now set explicitly.
### 2024-03-20 update
- Fixed `BF-018ARev3.ino`: with the compiler warning level set higher than the default, the `%d` format specifiers in `Serial.printf()` calls were being reported as errors.
### 2024-03-12 update
- Updated to `BF-018ARev3`.

### Rev.4
- Folder: `BF-018ARev4`
- In the Boards Manager, install version 3.x of the M5Stack board package.
- Works with the ATOMS3 Lite.
- On the M5Atom Lite/Matrix there is currently a bug that stops the LED from lighting up, so please use Rev.3 on those boards.

Confirmed working with:

| Tool | Item | 2025-08-15 |
|:-:|:-:|:-:|
|Application| Arduino-IDE | 2.3.6 |
|Boards Manager| M5Stack by M5Stack official | 3.2.3 |
|Library Manager| M5Unified by M5Stack | 0.2.10  |
|Library Manager| M5GFX by M5Stack | 0.2.16  |
|Library Manager| FastLED by Daniel Garcia | 3.10.3 |
|Library Manager| WiFiManager by tzapu | 2.0.17 |

### Rev.3
- Folder: `BF-018ARev3`
- In the Boards Manager, install version 2.x of the M5Stack board package.
- In the Library Manager, install M5Unified 0.2.5 and M5GFX 0.2.6.
- Works with the M5Atom Lite/Matrix and the ATOMS3 Lite.

Confirmed working with:

| Tool | Item | 2025-08-15 |
|:-:|:-:|:-:|
|Application| Arduino-IDE | 2.3.6 |
|Boards Manager| M5Stack by M5Stack official | 2.1.4 |
|Library Manager| M5Unified by M5Stack | 0.2.5  |
|Library Manager| M5GFX by M5Stack | 0.2.6  |
|Library Manager| FastLED by Daniel Garcia | 3.10.3 |
|Library Manager| WiFiManager by tzapu | 2.0.17 |

### Rev.2
- Folder: `BF-018ARev2`
- Please use Rev.3 or later.

### Rev.1 (original release)
- Folder: `BF-018A`
- Please use Rev.3 or later.

## 1. Overview
This project uses an M5Atom to generate a simulated JJY signal for radio-controlled clocks, so you can set the time on clocks that are out of range of the real JJY broadcast. It gets the time via NTP over Wi-Fi and outputs the JJY signal on a GPIO pin.

Reference (Qiita, in Japanese): [Generating a simulated JJY time signal with Ticker on the M5StickC / M5Atom](https://qiita.com/BotanicFields/items/a78c80f947388caf0d36)

## 2. Software
For details of the development environment and libraries, see the section for each revision above.

## 3. Hardware
Transmitting the JJY signal requires an antenna. For experimenting, you can connect a wire about 1 m long between the signal output pin (GPIO22 on the ATOM Lite/Matrix, GPIO5 on the ATOMS3 Lite) and GND, with a resistor of around 1 kΩ in series. Run the wire right next to the radio-controlled clock and the clock will pick up the magnetic field from the wire.

- Demo using a wire and a 1 kΩ resistor (M5StickC)  
YouTube: [JJY Simulator by M5StickC for a radio controlled clock](https://youtu.be/S_t3g5wqyh8)

### 3.1 Printed circuit board
We have made a printed circuit board (PCB) that an M5Atom can be mounted on.

- [Schematic](./bf-018a_scm.pdf)

- The PCB  
[PCB_front]  
<img src="./BF-018A_front.JPG" width=300>

- With an M5Atom Lite mounted  
[PCB_with_M5AtomLite]  
<img src="./BF-018A_M5AtomLite.JPG" width=300>

- With an M5Atom Matrix mounted  
[PCB_with_M5AtomMatrix]  
<img src="./BF-018A_M5AtomMatrix.JPG" width=300>

## 4. Operation

- After power-on or a reset, the device first starts the Wi-Fi connection process.
- Once the Wi-Fi connection is established, it gets the date and time via NTP and starts transmitting the time signal.
- The built-in LED lets you see when the JJY signal is on. On the M5Atom Matrix, only LED 0 is used.  
Rev.1 behaviour (for the Rev.2 changes, see section 8):  
  >Blue: the JJY signal is being transmitted  
Red: lights up when the device fails to get the time via NTP  
Yellow: lights up when the Wi-Fi connection is lost (red + green)

Demo  
- YouTube: [BF-018A: JJY Antenna for M5Atom](https://www.youtube.com/watch?v=4Soobp9k9r0)

## 5. Wi-Fi connection
Wi-Fi setup is handled by tzapu/WiFiManager. For details on how to use it, see the tzapu/WiFiManager documentation.

Reference: GitHub [tzapu/WiFiManager](https://github.com/tzapu/WiFiManager)

### Overview of the process:
> (1)	The M5Atom first tries to connect to the access point it connected to last time.  
(2) If it cannot connect, the M5Atom becomes an access point itself and waits for a smartphone or similar device to connect to it.
>> (2-1) On your smartphone (or similar device), connect to the access point that the M5Atom has created.  
Note: the SSID is a name based on the chip ID, and there is no password/key.  
(2-2) Open the IP address 192.168.4.1 in a web browser.  
(2-3) Enter the SSID/key of the network the M5Atom should connect to.

>(3)	The M5Atom connects to Wi-Fi using the SSID/key you entered.
## 5. LCD
The M5Atom version does not use an external LCD.

## 6. LED
The button turns LED monitoring on and off.

## 7. Serial monitor
The serial monitor lets you follow what the device is doing.

## 8. Miscellaneous

### (1) Wi-Fi connection recovery
We have added recovery handling that responds to the Wi-Fi status. Recovery can take a long time, for example 5 or 10 minutes, or until the Wi-Fi access point comes back. The aim is that you can simply leave the device alone and, before you know it, it will be working normally again. The recovery logic was worked out by trial and error and is not necessarily the best approach, so if you have any advice, please do let us know.

Reference (Qiita, in Japanese): [Trial and error with recovery handling for ESP32 Wi-Fi and tzapu/WiFiManager](https://qiita.com/BotanicFields/items/8a73101a8bfe51e57f67)

### (2) Using an RTC
Even if Wi-Fi is temporarily unavailable at power-on or after a reset, the device still transmits the JJY signal, based on the time held in the RTC (real-time clock). To make this possible, the RTC's time is updated whenever the time is synchronised via NTP, and the RTC keeps the current time based on its most recent update. To use this feature, connect an RTC unit to the M5Atom's GROVE port. In our experience, an RTC unit connected with a 20 cm GROVE cable was unreliable, whereas with a 10 cm cable it worked reliably.

Examples of RTC units you can use:
- [Real-Time Clock (RTC) Unit with HYM8563 for M5Stack](https://www.switch-science.com/catalog/7482/) (Switch Science, in Japanese)
- [Real-Time Clock (RTC) Board for M5](https://www.switch-science.com/catalog/7170/) (Switch Science, in Japanese)

Reference (Qiita, in Japanese): [Catching NTP time synchronisation on the ESP32 to update an RTC](https://qiita.com/BotanicFields/items/f1e28af5a63e4ccf7023)

[RTC module]  
<img src="./RTC.jpeg" width=300>

### (3) Erasing the stored SSID/key
To switch to a different Wi-Fi access point, you need to change the SSID/key settings. If the old access point has already been removed, WiFiManager's configuration portal starts automatically. If the old access point is still running, first erase the SSID/key stored in EEPROM; you can then enter new settings. To erase the SSID/key from EEPROM, hold down the button, press the reset button, and keep holding the button until the LED lights up green. At that point, the "configuration portal" is running.

### (4) Using 40 kHz
The real JJY signal is broadcast at 40 kHz from the transmitting station in Fukushima Prefecture and at 60 kHz from the station in Saga Prefecture. This project uses a PWM frequency of 40 kHz. The ESP32 cannot generate exactly 60 kHz, but it can generate a frequency quite close to it. In eastern Japan, setting the simulated JJY signal to 60 kHz lets you tell whether a clock has synchronised to the real signal from Fukushima or to the simulated one. To set the simulated signal to 60 kHz, change `jjy_frequency` in `BF-018ARevN.ino` to 60000.

```
// for TCO(Time Code Output)
const uint32_t jjy_frequency(40000); // 40kHz(east), 60kHz(west)
```

Reference (Qiita, in Japanese): [Using an Excel sheet to work out the resolution setting for the ESP32's LEDC (LED PWM Controller)](https://qiita.com/BotanicFields/items/e74c449c0bef0820fcd1)

### (5) LED indications

- Wi-Fi connection status  
Any status other than `WL_CONNECTED` is shown by the red LED, either lit steadily or blinking.
- While the config portal is running, the green LED is lit.
- While waiting for NTP synchronisation, the green LED blinks.

Details: [JJY simulator status indications]

| # | State | Wi-Fi status | M5Atom LED: red | M5Atom LED: green | M5Atom LED: blue | M5StickC / M5StickC Plus: LCD |
|:-:|:--|:--|:-:|:-:|:-:|:--|
| 1 | Transmitting the JJY signal | - | - | - | Blinking | Red LED blinking |
| 2 | JJY signal stopped | - | - | - | Off | Red LED off |
| 3 | NTP sync complete, or RTC time is valid | - | - | Off | - | Shows the local time |
| 4 | Waiting for NTP sync | - | - | Slow blink | - | Shows "Localtime not ready" |
| 5 | Wi-Fi SSID/key entry (config portal) in progress | - | - | On | - | Shows the config portal's SSID and IP address |
| 6 | Wi-Fi connected | `WL_CONNECTED` | Off | - | - | Shows the SSID of the connected network and the device's own IP address |
| 7 | Wi-Fi access point not found | `WL_NO_SSID_AVAIL` | Slow blink | - | - | Shows `WL_NO_SSID_AVAIL` |
| 8 | Wi-Fi disconnected | `WL_DISCONNECTED` | Fast blink | - | - | Shows `WL_DISCONNECTED` |
| 9 | Wi-Fi status changing | `WL_CONNECTION_LOST`, `WL_IDLE_STATE`, etc. | On | - | - | Shows `WL_CONNECTION_LOST`, `WL_IDLE_STATE`, etc. |

When the red, green and blue LEDs are lit or blinking at the same time, they can combine to show colours such as yellow or purple. For example, when the device is waiting for the Wi-Fi access point (`WL_NO_SSID_AVAIL`) and also waiting for NTP sync, the LED blinks slowly in yellow.

[Original chart, in Japanese]  
<img src="./wifi_status.jpg">

### (6) Workaround for unstable Wi-Fi connections on the M5Atom

The M5Atom has a problem where its Wi-Fi output power drops and the connection becomes unstable. Hopefully this is being addressed with improved firmware for the CH552 chip that handles USB-to-serial conversion, but many units that have already shipped do not have the fix. As a workaround, code that drives GPIO0 LOW has been added at the start of `setup()`. In Rev.3 and later, M5Unified's `M5.begin()` handles this instead, by driving GPIO0 HIGH.

#### References

- [Twitter: @wakwak_koba, 8:36 AM JST, 30 July 2022](https://twitter.com/wakwak_koba/status/1553162622479974400) (in Japanese)
- [M5Atom CH552 firmware update](https://togetter.com/li/1807623) (Togetter, in Japanese)
- [Bugs and measures for M5Stack ATOM](https://macsbug.wordpress.com/2021/10/10/bugs-and-measures-for-m5stack-atom/) (in Japanese)
