/*
   Adapted from Bastelschlumpf/M5PaperWeather for M5Paper v1.1
   Uses M5Unified + M5GFX, Open-Meteo API, English UI only

   Version 1.0 - M5Paper v1.1 English-only port
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

unsigned long lastRefreshTime = 0;
int refreshCounter = 0;

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
    String configuredCalendarMode = preferences.getString("calendar_mode", DEFAULT_CALENDAR_MODE);
    preferences.end();
    configuredCalendarMode.trim();
    configuredCalendarMode.toLowerCase();
    if (configuredCalendarMode != "google") {
        configuredCalendarMode = DEFAULT_CALENDAR_MODE;
    }
    calendarMode = configuredCalendarMode;

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
        float latitude, longitude;
        loadPreferences(latitude, longitude, cityName);

        Serial.printf("Fetching weather for: %.4f, %.4f (%s)\n",
                     latitude, longitude, cityName.c_str());

        bool fetchSuccess = false;
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
                displayWeather();
                lastRefreshTime = millis();
                fetchSuccess = true;
                break;
            }
        }

        if (!fetchSuccess) {
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
            Serial.println("*** Tap bottom-right corner of screen for CONFIG ***");

            unsigned long startWait = millis();
            const unsigned long waitDuration = USER_INTERACTION_TIMEOUT_MS;
            unsigned long lastSerialUpdate = 0;

            while (millis() - startWait < waitDuration) {
                M5.update();

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
                    Serial.printf("Waiting for config tap... %lu seconds remaining\n", remaining);
                    lastSerialUpdate = millis();
                }

                delay(100);
            }

            Serial.println("*** Wait period ended, entering sleep mode ***\n");
        } else {
            Serial.println("\n*** Automatic wake from timer - skipping interaction window ***");
            Serial.println("*** Waiting 3 seconds for display to refresh ***");
            delay(3000);
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
    Serial.println("=================================");

    enterDeepSleep(sleepTime);
}
