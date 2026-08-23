/*
   Adapted from Bastelschlumpf/M5PaperWeather for M5Paper v1.1
   Uses M5Unified + M5GFX, Open-Meteo API, English UI only

   Version 1.1 - M5Paper v1.1 English-only port
*/

#include <M5Unified.h>
#include <WiFi.h>
#include <Preferences.h>

#include "constants.h"
#include "utils.h"
#include "weather_api.h"
#include "calendar_api.h"
#include "config.h"
#include "display.h"

Preferences preferences;
M5Canvas canvas(&M5.Display);
WeatherData currentWeather;

bool useCelsius = false;
bool nightModeSleep = true;
String cityName = DEFAULT_CITY;
String calendarMode = DEFAULT_CALENDAR_MODE;
int displayFace = DEFAULT_DISPLAY_FACE;

unsigned long lastRefreshTime = 0;
String lastDataRefreshClock = "--:--";
int refreshCounter = 0;

static float cachedLat = DEFAULT_LATITUDE;
static float cachedLon = DEFAULT_LONGITUDE;
static bool haveCoords = false;

static void captureDataRefreshClock() {
    struct tm timeinfo;
    if (getLocalTime(&timeinfo)) {
        char buf[8];
        sprintf(buf, "%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);
        lastDataRefreshClock = String(buf);
    }
}

static void loadDisplayFacePref() {
    preferences.begin("weather", true);
    displayFace = preferences.getInt("display_face", DEFAULT_DISPLAY_FACE);
    preferences.end();
    if (displayFace != 0 && displayFace != 1) {
        displayFace = DEFAULT_DISPLAY_FACE;
    }
}

static void saveDisplayFacePref() {
    preferences.begin("weather", false);
    preferences.putInt("display_face", displayFace);
    preferences.end();
}

static void loadBasicPrefs() {
    preferences.begin("weather", true);
    String configuredCalendarMode = preferences.getString("calendar_mode", DEFAULT_CALENDAR_MODE);
    displayFace = preferences.getInt("display_face", DEFAULT_DISPLAY_FACE);
    cityName = preferences.getString("city", DEFAULT_CITY);
    String tempUnit = preferences.getString("tempunit", "F");
    nightModeSleep = preferences.getBool("nightmode", true);
    preferences.end();

    configuredCalendarMode.trim();
    configuredCalendarMode.toLowerCase();
    calendarMode = (configuredCalendarMode == "google") ? "google" : DEFAULT_CALENDAR_MODE;
    if (displayFace != 0 && displayFace != 1) {
        displayFace = DEFAULT_DISPLAY_FACE;
    }
    useCelsius = (tempUnit == "C");
}

static void cacheCoordinates() {
    loadPreferences(cachedLat, cachedLon, cityName);
    haveCoords = true;
}

static bool fetchWeatherOnce() {
    if (!haveCoords) {
        cacheCoordinates();
    }

    for (int retry = 0; retry < HTTP_RETRY_ATTEMPTS; retry++) {
        if (retry > 0) {
            delay(HTTP_RETRY_DELAY_MS);
        }
        if (fetchWeatherData(cachedLat, cachedLon)) {
            if (calendarMode == "google") {
                preferences.begin("weather", true);
                String calendarIcsUrl = preferences.getString("calendar_ics", "");
                preferences.end();
                fetchCalendarData(calendarIcsUrl);
            }
            applyWeatherTimezone();
            captureDataRefreshClock();
            return true;
        }
    }
    return false;
}

static bool fetchAndShowFull() {
    loadDisplayFacePref();
    if (!haveCoords) {
        cacheCoordinates();
    }

    Serial.printf("Fetching weather for: %.4f, %.4f (%s)\n",
                  cachedLat, cachedLon, cityName.c_str());
    Serial.printf("Display face: %d\n", displayFace);

    if (fetchWeatherOnce()) {
        Serial.println("Weather fetch successful!");
        storeWeatherFetchState();
        showActiveFace();
        lastRefreshTime = millis();
        return true;
    }
    return false;
}

static void markSettingsActivity() {
    // Settings UI manages its own idle timeout; parent window resets when settings closes.
}

static bool openConfigFromUi() {
    Serial.println("\n*** CONFIG button pressed! ***");

    SettingsExitResult result = runOnDeviceSettings(markSettingsActivity);

    if (result == SETTINGS_EXIT_SAVED) {
        ESP.restart();
    }
    if (result == SETTINGS_EXIT_WEB_PORTAL) {
        M5.Display.startWrite();
        M5.Display.fillScreen(TFT_WHITE);
        M5.Display.setTextSize(2);
        M5.Display.setCursor(20, 20);
        M5.Display.println("Web setup mode");
        M5.Display.println("\nConnect to:");
        M5.Display.println("  " + String(CONFIG_AP_SSID));
        M5.Display.println("Password: configure");
        M5.Display.println("URL: 192.168.4.1");
        M5.Display.endWrite();
        M5.Display.display();

        disconnectWiFi();
        delay(500);
        startConfigPortal();
        ESP.restart();
    }
    if (result == SETTINGS_EXIT_CANCEL) {
        showActiveFace();
    }
    return true;
}

static void reloadNightModePref() {
    preferences.begin("weather", true);
    nightModeSleep = preferences.getBool("nightmode", true);
    preferences.end();
}

static unsigned long clockWeatherFetchIntervalMs() {
    return (unsigned long)getFace1WeatherRefreshMinutes(isNightTime()) * 60000UL;
}

static unsigned long clockDisplayRefreshIntervalMs() {
    return (unsigned long)getFace1ClockRefreshMinutes(isNightTime()) * 60000UL;
}

static bool pollFaceControls() {
    M5.update();

    if (M5.BtnA.wasPressed() || M5.BtnC.wasPressed()) {
        displayFace = (displayFace == 0) ? 1 : 0;
        saveDisplayFacePref();
        Serial.printf("*** Face toggled to %d ***\n", displayFace);
        showActiveFace();
        return true;
    }

    auto touch = M5.Touch.getDetail();
    if (touch.wasPressed()) {
        if (touch.x > (SCREEN_WIDTH - CFG_BUTTON_TOUCH_WIDTH) &&
            touch.y > (SCREEN_HEIGHT - CFG_BUTTON_TOUCH_HEIGHT)) {
            openConfigFromUi();
            return true;
        }
    }
    return false;
}

static void runFaceSelectionWindow() {
    Serial.println("\n*** Face selection window (30s) ***");
    Serial.println("*** Tap bottom-right for CONFIG; rotary up/down to switch faces ***");

    unsigned long startWait = millis();
    const unsigned long waitDuration = USER_INTERACTION_TIMEOUT_MS;
    unsigned long lastSerialUpdate = 0;

    while (millis() - startWait < waitDuration) {
        if (pollFaceControls()) {
            startWait = millis();
        }

        unsigned long remaining = (waitDuration - (millis() - startWait)) / 1000;
        if (millis() - lastSerialUpdate >= 1000) {
            Serial.printf("Waiting for interaction... %lu seconds remaining (face %d)\n",
                          remaining, displayFace);
            lastSerialUpdate = millis();
        }

        delay(100);
    }

    Serial.println("*** Face selection window ended ***\n");
}

// Stay awake on clock face: clock display and weather fetch use separate configurable intervals
static void runClockFaceLoop() {
    Serial.println("*** Clock face active — staying awake (no deep sleep) ***");
    reloadNightModePref();
    int clockMinutes = getFace1ClockRefreshMinutes(isNightTime());
    int weatherMinutes = getFace1WeatherRefreshMinutes(isNightTime());
    Serial.printf("*** Clock display every %d min; weather fetch every %d min (%s) ***\n",
                  clockMinutes, weatherMinutes, isNightTime() ? "night" : "day");

    disconnectWiFi();

    unsigned long lastWeatherMs = millis();
    unsigned long lastClockMs = millis();
    int partialCount = 0;

    while (displayFace == 1) {
        pollFaceControls();
        if (displayFace != 1) {
            break;
        }

        reloadNightModePref();
        unsigned long clockIntervalMs = clockDisplayRefreshIntervalMs();
        if (millis() - lastClockMs >= clockIntervalMs) {
            lastClockMs = millis();
            struct tm timeinfo;
            if (getLocalTime(&timeinfo)) {
                Serial.printf("Clock refresh (%d min) %02d:%02d — partial update\n",
                              getFace1ClockRefreshMinutes(isNightTime()),
                              timeinfo.tm_hour, timeinfo.tm_min);
            } else {
                Serial.printf("Clock refresh (%d min) — partial update\n",
                              getFace1ClockRefreshMinutes(isNightTime()));
            }
            updateClockFacePartial(false);
            partialCount++;
            if (partialCount >= CLOCK_PARTIAL_FULL_EVERY) {
                Serial.println("Periodic full Face 1 redraw (ghosting cleanup)");
                showActiveFace();
                partialCount = 0;
            }
        }

        unsigned long weatherIntervalMs = clockWeatherFetchIntervalMs();
        if (millis() - lastWeatherMs >= weatherIntervalMs) {
            int intervalMin = getFace1WeatherRefreshMinutes(isNightTime());
            Serial.printf("Face 1 weather fetch (%d min, %s) — enabling WiFi\n",
                          intervalMin, isNightTime() ? "night" : "day");
            lastWeatherMs = millis();
            if (connectWiFiStation()) {
                preferences.begin("weather", true);
                String prevDate = preferences.getString("wx_date", "");
                preferences.end();

                if (fetchWeatherOnce()) {
                    bool dateChanged = (currentWeather.localDateYmd.length() > 0 &&
                                        currentWeather.localDateYmd != prevDate);
                    bool weatherChanged = weatherDataChangedSinceLastStore();
                    storeWeatherFetchState();

                    if (dateChanged) {
                        Serial.println("Date changed — full Face 1 redraw");
                        showActiveFace();
                        partialCount = 0;
                    } else if (weatherChanged) {
                        Serial.println("Weather changed — partial weather update");
                        updateClockFacePartial(true);
                    } else {
                        Serial.println("Weather unchanged — clock header only");
                        updateClockFacePartial(false);
                    }
                    lastClockMs = 0;  // force next clock refresh on schedule
                }
            } else {
                Serial.println("Hourly WiFi connect failed — keeping previous weather");
            }
            disconnectWiFi();
        }

        delay(CLOCK_LOOP_POLL_MS);
    }

    Serial.println("*** Left clock face — resuming normal sleep cycle ***");
}

void enterDeepSleep(unsigned long sleepTimeMs) {
    Serial.printf("Entering deep sleep for %lu ms (%lu minutes)\n",
                  sleepTimeMs, sleepTimeMs / 60000);

    disconnectWiFi();

    M5.Display.sleep();
    M5.Display.waitDisplay();
    delay(200);

    Serial.flush();

    int sleepSeconds = sleepTimeMs / 1000;
    if (sleepSeconds < 1) {
        sleepSeconds = 1;
    }
    // timerSleep sets the RTC alarm and powers down (battery only; USB may keep device on)
    M5.Power.timerSleep(sleepSeconds);
}

void setup() {
    M5.begin();
    M5.Display.begin();
    Serial.begin(115200);

    delay(100);

    Serial.println("\n=================================");
    Serial.println(String(APP_NAME) + " " + String(VERSION));
    Serial.println("Based on Bastelschlumpf design");
    Serial.println("=================================");

    initOnboardSensors();

    canvas.setColorDepth(8);
    canvas.createSprite(1, 1);
    canvas.deleteSprite();

    M5.Display.setRotation(1);
    M5.Display.setEpdMode(epd_mode_t::epd_quality);

    loadBasicPrefs();

    M5.Display.startWrite();
    M5.Display.fillScreen(TFT_WHITE);
    M5.Display.setTextColor(TFT_BLACK);
    M5.Display.setTextSize(2);
    M5.Display.setCursor(20, 20);
    M5.Display.println(String(APP_NAME) + " " + String(VERSION));
    M5.Display.setCursor(20, 50);
    M5.Display.println("Initializing...");
    M5.Display.endWrite();
    M5.Display.display();

    Serial.println("Splash screen displayed");
    delay(2000);

    setupWiFi();

    preferences.begin("weather", true);
    String configuredCalendarIcs = preferences.getString("calendar_ics", "");
    preferences.end();

    if (WiFi.status() == WL_CONNECTED &&
        calendarMode == "google" &&
        configuredCalendarIcs.length() == 0) {
        Serial.println("Google Calendar ICS URL is missing; opening configuration portal");
        M5.Display.startWrite();
        M5.Display.fillScreen(TFT_WHITE);
        M5.Display.setTextColor(TFT_BLACK);
        M5.Display.setTextSize(2);
        M5.Display.setCursor(20, 20);
        M5.Display.println("Calendar setup required");
        M5.Display.println("\nConnect to:");
        M5.Display.println("  " + String(CONFIG_AP_SSID));
        M5.Display.println("Password: configure");
        M5.Display.println("URL: 192.168.4.1");
        M5.Display.endWrite();
        M5.Display.display();

        disconnectWiFi();
        delay(500);
        startConfigPortal();
        ESP.restart();
    }

    setupTime();

    if (WiFi.status() == WL_CONNECTED) {
        if (!fetchAndShowFull()) {
            Serial.println("All weather fetch attempts failed!");
            M5.Display.startWrite();
            M5.Display.fillScreen(TFT_WHITE);
            M5.Display.setTextColor(TFT_BLACK);
            M5.Display.setCursor(20, 20);
            M5.Display.println("Failed to fetch weather");
            M5.Display.println("Will retry in 1 minute");
            M5.Display.endWrite();
            M5.Display.display();
            lastRefreshTime = millis() - REFRESH_INTERVAL_DAY_MS + 60000;
        }
    } else {
        M5.Display.startWrite();
        M5.Display.fillScreen(TFT_WHITE);
        M5.Display.setTextColor(TFT_BLACK);
        M5.Display.setCursor(20, 20);
        M5.Display.println("No WiFi - Touch to configure");
        M5.Display.endWrite();
        M5.Display.display();
        lastRefreshTime = millis();
    }

    // Clock mode: WiFi off after the initial fetch; stay awake in loop()
    if (displayFace == 1 && WiFi.status() == WL_CONNECTED) {
        disconnectWiFi();
    }

    Serial.println("Setup complete!");
}

void loop() {
    static bool hasWaited = false;

    if (!hasWaited) {
        esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();

        if (wakeup_reason == ESP_SLEEP_WAKEUP_UNDEFINED) {
            Serial.println("\n*** Manual wake detected (reset button or power on) ***");
            runFaceSelectionWindow();
        } else {
            Serial.println("\n*** Automatic wake from timer — skipping interaction window ***");
            Serial.println("*** Waiting 3 seconds for display to refresh ***");
            delay(3000);
        }

        hasWaited = true;
    }

    // Clock face: remain powered on with minute updates (no deep sleep)
    while (displayFace == 1) {
        runClockFaceLoop();
        if (displayFace == 0) {
            runFaceSelectionWindow();
        }
    }

    if (displayFace != 0) {
        // Safety: only weather face uses deep sleep
        delay(1000);
        return;
    }

    reloadNightModePref();
    unsigned long sleepTime = getRefreshInterval();

    preferences.begin("weather", true);
    int nightStart = preferences.getInt("night_start", 22);
    int nightEnd = preferences.getInt("night_end", 5);
    preferences.end();

    Serial.println("=================================");
    Serial.printf("Night mode: %s\n", nightModeSleep ? "ENABLED" : "DISABLED");
    if (nightModeSleep) {
        Serial.printf("Night hours: %d:00 - %d:00\n", nightStart, nightEnd);
    }
    Serial.printf("Current time is: %s\n", isNightTime() ? "NIGHT" : "DAY");
    Serial.printf("Refresh interval: %lu minutes\n", sleepTime / 60000);
    Serial.printf("Refresh counter: %d/6\n", refreshCounter % 6);
    Serial.printf("Display face: %d\n", displayFace);
    Serial.println("=================================");

    enterDeepSleep(sleepTime);
}
