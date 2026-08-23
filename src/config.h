#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

struct WeatherSettings {
    String ssid;
    String password;
    String city;
    String latitude;
    String longitude;
    String calendarMode;
    String calendarIcs;
    String tempUnit;
    bool nightMode;
    int face0Day;
    int face0Night;
    int face1Day;
    int face1Night;
    int face1ClockDay;
    int face1ClockNight;
    int nightStart;
    int nightEnd;
};

enum SettingsExitResult {
    SETTINGS_EXIT_CANCEL,
    SETTINGS_EXIT_SAVED,
    SETTINGS_EXIT_WEB_PORTAL
};

typedef void (*SettingsActivityCallback)();

// WiFi setup with retry logic (opens config portal on failure)
void setupWiFi();

// Station-only connect/disconnect (no config portal) for clock-mode hourly refresh
bool connectWiFiStation();
void disconnectWiFi();

// Configuration portal (web AP fallback)
void startConfigPortal();

// Shared settings load / validate / save
void loadSettingsFromPreferences(WeatherSettings &settings);
String validateSettings(const WeatherSettings &settings, bool requireGoogleIcs);
void saveSettingsToPreferences(const WeatherSettings &settings, bool preserveCalendarIcs);

// On-device quick settings UI
SettingsExitResult runOnDeviceSettings(SettingsActivityCallback onActivity);

// Load preferences for runtime (geocoding, globals)
void loadPreferences(float &latitude, float &longitude, String &cityName);

#endif // CONFIG_H
