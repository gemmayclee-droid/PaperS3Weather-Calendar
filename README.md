# PaperWeather-Calendar

PaperWeather-Calendar is a weather and Google Calendar dashboard for the **M5Paper v1.1** e-ink display (ESP32, 4.7", 960×540). It shows current weather, the next 8 hours, the next 3 days, and today's calendar events on one screen.

**English UI only.** Chinese display language and the on-screen language toggle from the Paper S3 fork are not included.

![English dashboard](M5PaperS3_Weather_Calendar_EN.png)

[![Arduino](https://img.shields.io/badge/Arduino-Compatible-brightgreen.svg)](https://www.arduino.cc/)
[![PlatformIO](https://img.shields.io/badge/PlatformIO-Ready-orange.svg)](https://platformio.org/)
[![ESP32](https://img.shields.io/badge/ESP32-M5Paper%20v1.1-blue.svg)](https://docs.m5stack.com/)

## Features

- **Single-screen dashboard**: Current weather, next 8 hours, next 3 days, and Google Calendar events
- **Web setup portal**: WiFi, location, calendar ICS URL, temperature unit, refresh intervals, night mode
- **Onboard SHT30**: Local temperature and humidity when the sensor is available
- **Power saving**: RTC timer sleep between updates, with separate day/night refresh intervals
- **Open-Meteo weather API**: No API key required

## Hardware Requirements

- **M5Paper v1.1** (SKU K049-B) — [product page](https://shop.m5stack.com/products/m5paper-esp32-development-kit-v1-1-960x540-4-7-eink-display-235-ppi)
- USB-C cable for programming and power
- 2.4 GHz WiFi

Libraries: **M5Unified**, **M5GFX**, and **M5Unit-ENV** (for the onboard SHT30). Do not use the deprecated M5EPD library.

## Build and Flash (PlatformIO)

```bash
git clone <this-repo>
cd m5paperWeather-Calendar
pio run -e Paper
pio run -e Paper --target upload
pio device monitor --baud 115200
```

`platformio.ini` uses `board = m5stack-fire` with `-DARDUINO_M5STACK_Paper` because `m5stack-paper` is missing from many PlatformIO installs. Flash size is 16MB with PSRAM enabled. If your PlatformIO package includes `m5stack-paper`, you can switch `board` to that name.

## First Setup

1. After boot (or when WiFi/calendar is missing), connect to WiFi **`PaperWeather-Calendar`**, password **`configure`**.
2. Open `http://192.168.4.1`.
3. Enter WiFi, city (or coordinates), Google Calendar ICS URL, temperature unit, and refresh settings.
4. Click **Save & Restart**.

### Re-open configuration later

1. Press the **reset** button on the M5Paper.
2. Within 30 seconds, tap **[CFG]** in the bottom-right corner of the screen.
3. Connect to the setup AP as above.

To leave the config portal without saving, rotate the side wheel **up** (`BtnA`).

## Google Calendar ICS URL

1. Open Google Calendar → calendar settings → **Integrate calendar**.
2. Copy **Secret address in iCal format** (private) or the public iCal address.
3. Paste into the setup portal. The URL should contain `/calendar/ical/` and end with `/basic.ics`.

`HTTP 404` usually means the URL is not a valid accessible ICS feed.

## Power / Sleep Notes

- Sleep uses `M5.Power.timerSleep()` (RTC alarm + power off).
- **Full power-off / RTC wake works on battery.** When powered over USB, the device may not fully shut down (M5Paper power design).
- Validate overnight refresh cycles while running on battery.

## Attribution

Based on [Bastelschlumpf's M5PaperWeather](https://github.com/Bastelschlumpf/M5PaperWeather), later adapted for M5Paper S3, and ported here back to classic **M5Paper v1.1** with M5Unified/M5GFX and an English-only UI.

## License

MIT License — see `LICENSE`.

## Acknowledgments

- [Bastelschlumpf](https://github.com/Bastelschlumpf) — original M5PaperWeather design
- [Open-Meteo](https://open-meteo.com/) — weather data
- [M5Stack](https://m5stack.com/) — M5Paper hardware and M5Unified / M5GFX
