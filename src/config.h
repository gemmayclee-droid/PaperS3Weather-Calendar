#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

// WiFi setup with retry logic (opens config portal on failure)
void setupWiFi();

// Station-only connect/disconnect (no config portal) for clock-mode hourly refresh
bool connectWiFiStation();
void disconnectWiFi();

// Configuration portal
void startConfigPortal();

// Load preferences
void loadPreferences(float &latitude, float &longitude, String &cityName);

#endif // CONFIG_H
