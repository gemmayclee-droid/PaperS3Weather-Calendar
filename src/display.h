#ifndef DISPLAY_H
#define DISPLAY_H

#include <M5Unified.h>
#include <M5GFX.h>

// Face router
void showActiveFace();

// Face 0 — weather dashboard
void displayWeather();

// Face 1 — clock + weather summary + large calendar
void displayClockFace();
void drawFace1WeatherSummary(int x, int y, int dx, int dy);
// Partial Face 1 updates (clock panel always; weather panel optional)
void updateClockFacePartial(bool updateWeather);

// Panel drawing functions
void drawCurrentConditions(int x, int y, int dx, int dy);
void drawSunInfo(int x, int y, int dx, int dy);
void drawWindInfo(int x, int y, int dx, int dy);
void drawM5PaperInfo(int x, int y, int dx, int dy);
void drawHourlyForecast(int x, int y, int dx, int dy, int index);

// Graph drawing functions
void drawGraph(int x, int y, int dx, int dy, String title, int xMin, int xMax, float yMin, float yMax, float values[]);
void drawTempGraph(int x, int y, int dx, int dy, String title, int xMin, int xMax, float yMin, float yMax, float highValues[], float lowValues[]);

// Component drawing functions
void drawIcon(int x, int y, const uint8_t *icon, int dx = 64, int dy = 64, bool highContrast = false);
void drawRSSI(int x, int y, int rssi);
void drawBattery(int x, int y, int batteryPercent);
void drawArrow(int x, int y, int asize, float aangle, int pwidth, int plength);
void drawWindCompass(int x, int y, float angle, float windspeed, int radius);
void drawCalendarEvents(int x, int y, int dx, int dy, int textSize = 2, int lineSpacing = 32);
void drawMonthCalendar(int x, int y, int dx, int dy, int textSize = 1);

#endif // DISPLAY_H
