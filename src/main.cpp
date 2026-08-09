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

static bool fetchAndShowFull() {
    float latitude, longitude;
    loadPreferences(latitude, longitude, cityName);
    loadDisplayFacePref();

    Serial.printf("Fetching weather for: %.4f, %.4f (%s)\n",
                  latitude, longitude, cityName.c_str());
    Serial.printf("Display face: %d\n", displayFace);

    for (int retry = 0; retry < HTTP_RETRY_ATTEMPTS; retry++) {
        if (retry > 0) {
            Serial.printf("Weather fetch retry %d/%d...\n", retry + 1, HTTP_RETRY_ATTEMPTS);
            delay(HTTP_RETRY_DELAY_MS);
        }

        if (fetchWeatherData(latitude, longitude)) {
            Serial.println("Weather fetch successful!");
            if (calendarMode == "google") {
                preferences.begin("weather", true);
                String calendarIcsUrl = preferences.getString("calendar_ics", "");
                preferences.end();
                fetchCalendarData(calendarIcsUrl);
            }
            applyWeatherTimezone();
            captureDataRefreshClock();
            storeWeatherFetchState();
            showActiveFace();
            lastRefreshTime = millis();
            preferences.begin("weather", false);
            preferences.putInt("clk_partials", 0);
            preferences.end();
            return true;
        }
    }
    return false;
}

static void handleClockFaceAutoWake() {
    Serial.println("*** Clock-face auto wake ***");
    applyStoredTimezone();
    setupTime();

    preferences.begin("weather", true);
    int partialCount = preferences.getInt("clk_partials", 0);
    preferences.end();

    bool needFull = (partialCount >= CLOCK_PARTIAL_FULL_EVERY);
    bool weatherDue = isWeatherFetchDue();
    bool weatherChanged = false;
    bool dateChanged = false;
    bool fetched = false;

    if (weatherDue || needFull) {
        Serial.printf("Weather fetch due=%d full=%d — connecting WiFi\n", weatherDue, needFull);
        setupWiFi();
        if (WiFi.status() == WL_CONNECTED) {
            float latitude, longitude;
            loadPreferences(latitude, longitude, cityName);
            if (fetchWeatherData(latitude, longitude)) {
                fetched = true;
                if (calendarMode == "google") {
                    preferences.begin("weather", true);
                    String calendarIcsUrl = preferences.getString("calendar_ics", "");
                    preferences.end();
                    fetchCalendarData(calendarIcsUrl);
                }
                applyWeatherTimezone();
                captureDataRefreshClock();
                preferences.begin("weather", true);
                String prevDate = preferences.getString("wx_date", "");
                preferences.end();
                dateChanged = (currentWeather.localDateYmd.length() > 0 &&
                               currentWeather.localDateYmd != prevDate);
                weatherChanged = weatherDataChangedSinceLastStore();
                storeWeatherFetchState();
            }
        } else {
            Serial.println("WiFi unavailable on clock wake — clock-only update");
        }
    } else {
        Serial.println("Weather still fresh — clock panel only (no WiFi)");
    }

    if (needFull || dateChanged) {
        Serial.printf("Full Face 1 redraw (full=%d dateChanged=%d)\n", needFull, dateChanged);
        showActiveFace();
        partialCount = 0;
    } else if (weatherChanged) {
        Serial.println("Partial clock + weather update");
        updateClockFacePartial(true);
        partialCount++;
    } else {
        Serial.println("Partial clock update");
        updateClockFacePartial(false);
        partialCount++;
    }

    preferences.begin("weather", false);
    preferences.putInt("clk_partials", partialCount);
    preferences.end();
    lastRefreshTime = millis();
    (void)fetched;
}

void enterDeepSleep(unsigned long sleepTimeMs) {
    Serial.printf("Entering deep sleep for %lu ms (%lu minutes)\n",
                  sleepTimeMs, sleepTimeMs / 60000);

    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);

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

    esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
    bool autoWake = (wakeup_reason != ESP_SLEEP_WAKEUP_UNDEFINED);

    loadBasicPrefs();

    // Fast path: clock face timer wake — prefer partial clock update, skip WiFi when possible
    if (autoWake && displayFace == 1) {
        handleClockFaceAutoWake();
        Serial.println("Setup complete (clock auto-wake)!");
        return;
    }

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

        WiFi.disconnect();
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

    Serial.println("Setup complete!");
}

void loop() {
    static bool hasWaited = false;

    if (!hasWaited) {
        esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();

        if (wakeup_reason == ESP_SLEEP_WAKEUP_UNDEFINED) {
            Serial.println("\n*** Manual wake detected (reset button or power on) ***");
            Serial.println("*** Waiting 30 seconds for user interaction ***");
            Serial.println("*** Tap bottom-right for CONFIG; rotary up/down to switch faces ***");

            unsigned long startWait = millis();
            const unsigned long waitDuration = USER_INTERACTION_TIMEOUT_MS;
            unsigned long lastSerialUpdate = 0;

            while (millis() - startWait < waitDuration) {
                M5.update();

                if (M5.BtnA.wasPressed() || M5.BtnC.wasPressed()) {
                    displayFace = (displayFace == 0) ? 1 : 0;
                    saveDisplayFacePref();
                    Serial.printf("*** Face toggled to %d ***\n", displayFace);
                    showActiveFace();
                    startWait = millis();
                }

                auto touch = M5.Touch.getDetail();
                if (touch.wasPressed()) {
                    int touchX = touch.x;
                    int touchY = touch.y;

                    if (touchX > (SCREEN_WIDTH - CFG_BUTTON_TOUCH_WIDTH) &&
                        touchY > (SCREEN_HEIGHT - CFG_BUTTON_TOUCH_HEIGHT)) {
                        Serial.println("\n*** CONFIG button pressed! ***");
                        M5.Display.startWrite();
                        M5.Display.fillScreen(TFT_WHITE);
                        M5.Display.setTextSize(2);
                        M5.Display.setCursor(20, 20);
                        M5.Display.println("Opening Configuration...");
                        M5.Display.println("\nConnect to:");
                        M5.Display.println("  " + String(CONFIG_AP_SSID));
                        M5.Display.println("Password: configure");
                        M5.Display.println("URL: 192.168.4.1");
                        M5.Display.endWrite();
                        M5.Display.display();

                        WiFi.disconnect();
                        delay(500);
                        startConfigPortal();
                        ESP.restart();
                    }
                }

                unsigned long remaining = (waitDuration - (millis() - startWait)) / 1000;
                if (millis() - lastSerialUpdate >= 1000) {
                    Serial.printf("Waiting for interaction... %lu seconds remaining (face %d)\n",
                                  remaining, displayFace);
                    lastSerialUpdate = millis();
                }

                delay(100);
            }

            Serial.println("*** Wait period ended, entering sleep mode ***\n");
        } else {
            Serial.println("\n*** Automatic wake from timer - skipping interaction window ***");
            // Brief settle only when we did a full splash path; clock path already drew
            if (displayFace != 1) {
                Serial.println("*** Waiting 3 seconds for display to refresh ***");
                delay(3000);
            }
        }

        hasWaited = true;
    }

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
