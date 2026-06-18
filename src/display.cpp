#include "display.h"
#include "constants.h"
#include "utils.h"
#include "Icons.h"
#include <WiFi.h>

extern WeatherData currentWeather;
extern M5Canvas canvas;
extern String cityName;

void drawIcon(int x, int y, const uint8_t *icon, int dx, int dy, bool highContrast) {
    const uint16_t *icon16 = (const uint16_t *)icon;

    for (int yi = 0; yi < dy; yi++) {
        for (int xi = 0; xi < dx; xi++) {
            uint16_t pixel = icon16[yi * dx + xi];
            int grayscale = 15 - (pixel / ICON_GRAYSCALE_DIVISOR);

            if (highContrast) {
                if (grayscale > 0) {
                    canvas.drawPixel(x + xi, y + yi, TFT_BLACK);
                }
            } else {
                uint16_t color = 0xFFFF - (grayscale * ICON_COLOR_MULTIPLIER);
                canvas.drawPixel(x + xi, y + yi, color);
            }
        }
    }
}

void drawRSSI(int x, int y, int rssi) {
    int quality = getRSSIQuality(rssi);

    auto drawArc = [&](int cx, int cy, int r, int fromDeg, int toDeg) {
        for (int i = fromDeg; i < toDeg; i++) {
            double rad = i * PI / 180;
            int px = cx + r * cos(rad);
            int py = cy + r * sin(rad);
            canvas.drawPixel(px, py, TFT_BLACK);
        }
    };

    if (quality >= 80) drawArc(x + 12, y, 16, 225, 315);
    if (quality >= 40) drawArc(x + 12, y, 12, 225, 315);
    if (quality >= 20) drawArc(x + 12, y, 8, 225, 315);
    if (quality >= 10) drawArc(x + 12, y, 4, 225, 315);
    drawArc(x + 12, y, 2, 225, 315);
}

void drawBattery(int x, int y, int batteryPercent) {
    canvas.drawRect(x, y, BATTERY_WIDTH, BATTERY_HEIGHT, TFT_BLACK);
    canvas.drawRect(x + BATTERY_WIDTH, y + BATTERY_TIP_OFFSET, BATTERY_TIP_WIDTH, BATTERY_TIP_HEIGHT, TFT_BLACK);

    // Fill battery based on percentage
    for (int i = x; i < x + BATTERY_WIDTH; i++) {
        canvas.drawLine(i, y, i, y + BATTERY_HEIGHT - 1, TFT_BLACK);
        if ((i - x) * 100.0 / BATTERY_WIDTH > batteryPercent) {
            break;
        }
    }
}

void drawArrow(int x, int y, int asize, float aangle, int pwidth, int plength) {
    float dx = (asize + 21) * cos((aangle - 90) * PI / 180) + x;
    float dy = (asize + 21) * sin((aangle - 90) * PI / 180) + y;
    float x1 = 0;           float y1 = plength;
    float x2 = pwidth / 2;  float y2 = pwidth / 2;
    float x3 = -pwidth / 2; float y3 = pwidth / 2;
    float angle = aangle * PI / 180;
    float xx1 = x1 * cos(angle) - y1 * sin(angle) + dx;
    float yy1 = y1 * cos(angle) + x1 * sin(angle) + dy;
    float xx2 = x2 * cos(angle) - y2 * sin(angle) + dx;
    float yy2 = y2 * cos(angle) + x2 * sin(angle) + dy;
    float xx3 = x3 * cos(angle) - y3 * sin(angle) + dx;
    float yy3 = y3 * cos(angle) + x3 * sin(angle) + dy;
    canvas.fillTriangle(xx1, yy1, xx3, yy3, xx2, yy2, TFT_BLACK);
}

void drawWindCompass(int x, int y, float angle, float windspeed, int radius) {
    int dxo, dyo, dxi, dyi;

    canvas.setTextSize(2);
    canvas.drawCircle(x, y, radius, TFT_BLACK);
    canvas.drawCircle(x, y, radius + 1, TFT_BLACK);
    canvas.drawCircle(x, y, radius * 0.7, TFT_BLACK);

    // Draw compass ticks
    for (float a = 0; a < 360; a += 22.5) {
        dxo = radius * cos((a - 90) * PI / 180);
        dyo = radius * sin((a - 90) * PI / 180);

        dxi = dxo * 0.9;
        dyi = dyo * 0.9;
        canvas.drawLine(dxo + x, dyo + y, dxi + x, dyi + y, TFT_BLACK);

        dxo = dxo * 0.7;
        dyo = dyo * 0.7;
        dxi = dxo * 0.9;
        dyi = dyo * 0.9;
        canvas.drawLine(dxo + x, dyo + y, dxi + x, dyi + y, TFT_BLACK);
    }

    // Draw cardinal directions
    int labelOffset = radius + COMPASS_LABEL_OFFSET;
    canvas.setTextDatum(TC_DATUM);
    canvas.drawString("N", x, y - labelOffset);
    canvas.drawString("S", x, y + labelOffset - 8);

    canvas.setTextDatum(MC_DATUM);
    canvas.drawString("W", x - labelOffset, y);
    canvas.drawString("E", x + labelOffset, y);

    // Draw intercardinal directions
    int diagOffset = (int)(labelOffset * COMPASS_DIAG_FACTOR);
    canvas.setTextDatum(BR_DATUM);
    canvas.drawString("NE", x + diagOffset + 10, y - diagOffset);
    canvas.setTextDatum(TR_DATUM);
    canvas.drawString("SE", x + diagOffset + 10, y + diagOffset);
    canvas.setTextDatum(TL_DATUM);
    canvas.drawString("SW", x - diagOffset - 10, y + diagOffset);
    canvas.setTextDatum(BL_DATUM);
    canvas.drawString("NW", x - diagOffset - 10, y - diagOffset);

    // Draw wind speed
    String speedUnit = useCelsius ? "km/h" : "mph";
    canvas.setTextDatum(TC_DATUM);
    canvas.drawString(String(windspeed, 1), x, y - 20);
    canvas.drawString(speedUnit, x, y);
    canvas.setTextDatum(TL_DATUM);

    // Draw wind direction arrow
    drawArrow(x, y, radius - 17, angle, COMPASS_ARROW_SIZE, COMPASS_ARROW_LENGTH);
}

void drawHourlyForecast(int x, int y, int dx, int dy, int index) {
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) {
        timeinfo.tm_hour = 0;
    }
    int forecastHour = (timeinfo.tm_hour + index + 1) % 24;

    canvas.setTextSize(2);
    canvas.setTextDatum(TC_DATUM);
    char hourStr[6];
    sprintf(hourStr, "%02d:00", forecastHour);
    canvas.drawString(hourStr, x + dx / 2, y + 8);
    canvas.setTextDatum(TL_DATUM);

    bool isDay = isDaytime(forecastHour);
    int iconX = x + dx / 2 - 32;
    int iconY = y + 32;
    const uint8_t* weatherIcon = getWeatherIcon(currentWeather.hourly[index].weatherCode, isDay);
    drawIcon(iconX, iconY, weatherIcon, WEATHER_ICON_SIZE, WEATHER_ICON_SIZE, true);

    canvas.setTextDatum(TC_DATUM);
    canvas.setTextSize(3);
    canvas.drawString(formatTemp(currentWeather.hourly[index].temp), x + dx / 2, y + 98);

    canvas.setTextSize(1);
    canvas.drawString(String((int)currentWeather.hourly[index].precip) + "% rain", x + dx / 2, y + 120);
    canvas.setTextDatum(TL_DATUM);
}

void drawGraph(int x, int y, int dx, int dy, String title, int xMin, int xMax, float yMin, float yMax, float values[]) {
    String yMinString = String((int)yMin);
    String yMaxString = String((int)yMax);
    int textWidth = 5 + max(yMinString.length() * GRAPH_TEXT_WIDTH_FACTOR, yMaxString.length() * GRAPH_TEXT_WIDTH_FACTOR);

    int graphX = x + 5 + textWidth + 5;
    int graphY = y + GRAPH_AREA_Y_OFFSET;
    int graphDX = dx - textWidth - GRAPH_SIDE_MARGIN;
    int graphDY = dy - GRAPH_AREA_Y_OFFSET - GRAPH_BOTTOM_MARGIN;
    float xStep = graphDX / (float)(xMax - xMin);
    float yStep = graphDY / (yMax - yMin);

    // Draw title
    canvas.setTextSize(2);
    canvas.setTextDatum(TC_DATUM);
    canvas.drawString(title, x + dx / 2, y + GRAPH_TITLE_Y_OFFSET);
    canvas.setTextDatum(TL_DATUM);

    // Draw Y-axis labels
    canvas.setTextSize(1);
    canvas.drawString(yMaxString, x + 5, graphY - 5);
    canvas.drawString(yMinString, x + 5, graphY + graphDY - 3);

    // Draw X-axis labels
    for (int i = 0; i <= (xMax - xMin); i++) {
        canvas.drawString(String(i), graphX + i * xStep, graphY + graphDY + 5);
    }

    // Draw graph border
    canvas.drawRect(graphX, graphY, graphDX, graphDY, TFT_BLACK);

    // Draw zero line if applicable
    if (yMin < 0 && yMax > 0) {
        float yValueDX = (float)graphDY / (yMax - yMin);
        int yPos = graphY + graphDY - (0.0 - yMin) * yValueDX;
        if (yPos > graphY && yPos < graphY + graphDY) {
            canvas.drawString("0", graphX - 20, yPos);
            for (int xDash = graphX; xDash < graphX + graphDX - GRAPH_DASH_SPACING; xDash += GRAPH_DASH_SPACING) {
                canvas.drawLine(xDash, yPos, xDash + GRAPH_DASH_LENGTH, yPos, TFT_BLACK);
            }
        }
    }

    // Plot data points and lines
    int lastX = -1, lastY = -1;
    for (int i = xMin; i <= xMax; i++) {
        float yValue = values[i - xMin];
        float yValueDY = (float)graphDY / (yMax - yMin);
        int xPos = graphX + graphDX / (xMax - xMin) * i;
        int yPos = graphY + graphDY - (yValue - yMin) * yValueDY;

        // Clamp to graph bounds
        if (yPos > graphY + graphDY) yPos = graphY + graphDY;
        if (yPos < graphY) yPos = graphY;

        canvas.fillCircle(xPos, yPos, GRAPH_POINT_RADIUS, TFT_BLACK);
        if (i > xMin) {
            canvas.drawLine(lastX, lastY, xPos, yPos, TFT_BLACK);
        }
        lastX = xPos;
        lastY = yPos;
    }
}

void drawTempGraph(int x, int y, int dx, int dy, String title, int xMin, int xMax, float yMin, float yMax, float highValues[], float lowValues[]) {
    String yMinString = String((int)yMin);
    String yMaxString = String((int)yMax);
    int textWidth = 5 + max(yMinString.length() * GRAPH_TEXT_WIDTH_FACTOR, yMaxString.length() * GRAPH_TEXT_WIDTH_FACTOR);

    int graphX = x + 5 + textWidth + 5;
    int graphY = y + GRAPH_AREA_Y_OFFSET;
    int graphDX = dx - textWidth - GRAPH_SIDE_MARGIN;
    int graphDY = dy - GRAPH_AREA_Y_OFFSET - GRAPH_BOTTOM_MARGIN;
    float xStep = graphDX / (float)(xMax - xMin);
    float yStep = graphDY / (yMax - yMin);

    // Draw title
    canvas.setTextSize(2);
    canvas.setTextDatum(TC_DATUM);
    canvas.drawString(title, x + dx / 2, y + GRAPH_TITLE_Y_OFFSET);
    canvas.setTextDatum(TL_DATUM);

    // Draw Y-axis labels
    canvas.setTextSize(1);
    canvas.drawString(yMaxString, x + 5, graphY - 5);
    canvas.drawString(yMinString, x + 5, graphY + graphDY - 3);

    // Draw X-axis labels
    for (int i = 0; i <= (xMax - xMin); i++) {
        canvas.drawString(String(i), graphX + i * xStep, graphY + graphDY + 5);
    }

    // Draw graph border
    canvas.drawRect(graphX, graphY, graphDX, graphDY, TFT_BLACK);

    // Plot high temperatures
    int lastHighX = -1, lastHighY = -1;
    for (int i = xMin; i <= xMax; i++) {
        float yValue = highValues[i - xMin];
        float yValueDY = (float)graphDY / (yMax - yMin);
        int xPos = graphX + graphDX / (xMax - xMin) * i;
        int yPos = graphY + graphDY - (yValue - yMin) * yValueDY;

        if (yPos > graphY + graphDY) yPos = graphY + graphDY;
        if (yPos < graphY) yPos = graphY;

        canvas.fillCircle(xPos, yPos, GRAPH_POINT_RADIUS, TFT_BLACK);
        if (i > xMin) {
            canvas.drawLine(lastHighX, lastHighY, xPos, yPos, TFT_BLACK);
        }
        lastHighX = xPos;
        lastHighY = yPos;
    }

    // Plot low temperatures
    int lastLowX = -1, lastLowY = -1;
    for (int i = xMin; i <= xMax; i++) {
        float yValue = lowValues[i - xMin];
        float yValueDY = (float)graphDY / (yMax - yMin);
        int xPos = graphX + graphDX / (xMax - xMin) * i;
        int yPos = graphY + graphDY - (yValue - yMin) * yValueDY;

        if (yPos > graphY + graphDY) yPos = graphY + graphDY;
        if (yPos < graphY) yPos = graphY;

        canvas.fillCircle(xPos, yPos, GRAPH_POINT_RADIUS, TFT_BLACK);
        if (i > xMin) {
            canvas.drawLine(lastLowX, lastLowY, xPos, yPos, TFT_BLACK);
        }
        lastLowX = xPos;
        lastLowY = yPos;
    }

    // Plot average temperature (dotted line)
    int lastAvgX = -1, lastAvgY = -1;
    for (int i = xMin; i <= xMax; i++) {
        float avgValue = (highValues[i - xMin] + lowValues[i - xMin]) / 2.0;
        float yValueDY = (float)graphDY / (yMax - yMin);
        int xPos = graphX + graphDX / (xMax - xMin) * i;
        int yPos = graphY + graphDY - (avgValue - yMin) * yValueDY;

        if (yPos > graphY + graphDY) yPos = graphY + graphDY;
        if (yPos < graphY) yPos = graphY;

        canvas.fillCircle(xPos, yPos, 1, TFT_BLACK);
        if (i > xMin) {
            // Draw dotted line
            int dx = xPos - lastAvgX;
            int dy = yPos - lastAvgY;
            float len = sqrt(dx*dx + dy*dy);
            for (float t = 0; t < len; t += 5) {
                int px = lastAvgX + (dx * t / len);
                int py = lastAvgY + (dy * t / len);
                canvas.drawPixel(px, py, TFT_BLACK);
            }
        }
        lastAvgX = xPos;
        lastAvgY = yPos;
    }
}

void drawCurrentConditions(int x, int y, int dx, int dy) {
    canvas.setTextDatum(TL_DATUM);

    // Draw main temperature (large font)
    canvas.setFont(&fonts::FreeSansBold24pt7b);
    canvas.setTextSize(2);
    canvas.setTextDatum(TL_DATUM);

    String tempNum = String((int)currentWeather.temperature);
    int mainTempX = x + 26;
    int mainTempY = y + 42;
    canvas.drawString(tempNum, mainTempX, mainTempY);

    // Draw degree symbol for main temperature
    int degreeX = mainTempX + canvas.textWidth(tempNum) + 8;
    int degreeY = mainTempY + 11;
    drawDegreeSymbol(degreeX, degreeY, TEMP_DEGREE_RADIUS_LARGE);

    canvas.setFont(nullptr);
    canvas.setTextFont(1);

    // Draw weather icon
    struct tm timeinfo;
    bool hasTime = getLocalTime(&timeinfo);
    if (!hasTime) {
        timeinfo.tm_hour = 12;
    }
    bool isDay = isDaytime(timeinfo.tm_hour);

    int iconX = x + 215;
    int iconY = y + 43;
    const uint8_t* weatherIcon = getWeatherIcon(currentWeather.weatherCode, isDay);
    drawIcon(iconX, iconY, weatherIcon, WEATHER_ICON_SIZE, WEATHER_ICON_SIZE, true);

    // Draw condition text
    String condition = getWeatherConditionText(currentWeather.weatherCode);
    canvas.setTextSize(condition.length() > 14 ? 2 : 3);
    canvas.drawString(condition, x + 300, y + 50);

    canvas.setTextSize(2);
    canvas.drawString(String("Feels ") + formatTemp(currentWeather.apparentTemperature), x + 300, y + 84);
    canvas.drawString(String("Today ") + formatTemp(currentWeather.todayMinTemp) + " / " +
                      formatTemp(currentWeather.todayMaxTemp), x + 300, y + 110);

    int detailsX = x + 545;
    canvas.drawString("Humidity", detailsX, y + 46);
    canvas.drawString(String((int)currentWeather.humidity) + "%", detailsX + 120, y + 46);
    canvas.drawString("Wind", detailsX, y + 74);
    canvas.drawString(String(currentWeather.windSpeed, 1) + (useCelsius ? " km/h" : " mph"), detailsX + 120, y + 74);
    canvas.drawString("Rain", detailsX, y + 102);
    canvas.drawString(String(currentWeather.precipitation, 1) + " mm", detailsX + 120, y + 102);

    int sunX = x + 760;
    canvas.drawString("Sunrise", sunX, y + 46);
    canvas.drawString(currentWeather.sunriseTime.length() > 0 ? currentWeather.sunriseTime : "--:--", sunX + 105, y + 46);
    canvas.drawString("Sunset", sunX, y + 74);
    canvas.drawString(currentWeather.sunsetTime.length() > 0 ? currentWeather.sunsetTime : "--:--", sunX + 105, y + 74);

    if (hasTime) {
        char dateStr[12];
        sprintf(dateStr, "%02d/%02d", timeinfo.tm_mon + 1, timeinfo.tm_mday);
        const char* days[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
        canvas.drawString(String(days[timeinfo.tm_wday]) + " " + String(dateStr), sunX, y + 102);
    }

    canvas.setTextDatum(TL_DATUM);
}

String getForecastDateLabel(int dayOffset) {
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) {
        return "D+" + String(dayOffset);
    }

    timeinfo.tm_mday += dayOffset;
    mktime(&timeinfo);

    char dateStr[12];
    sprintf(dateStr, "%02d/%02d", timeinfo.tm_mon + 1, timeinfo.tm_mday);
    const char* days[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    return String(days[timeinfo.tm_wday]) + " " + String(dateStr);
}

void drawDailyForecast(int x, int y, int dx, int dy, int forecastIndex) {
    canvas.setTextDatum(TC_DATUM);
    canvas.setTextSize(3);
    canvas.drawString(getForecastDateLabel(forecastIndex), x + dx / 2, y + 8);

    const uint8_t* weatherIcon = getWeatherIcon(currentWeather.forecastWeatherCode[forecastIndex], true);
    drawIcon(x + dx / 2 - 32, y + 36, weatherIcon, WEATHER_ICON_SIZE, WEATHER_ICON_SIZE, true);

    canvas.setTextSize(4);
    canvas.drawString(formatTemp(currentWeather.forecastMinTemp[forecastIndex]) + " / " +
                      formatTemp(currentWeather.forecastMaxTemp[forecastIndex]),
                      x + dx / 2, y + 100);

    canvas.setTextSize(2);
    canvas.drawString(String("Rain ") + String(currentWeather.forecastRain[forecastIndex], 1) + " mm",
                      x + dx / 2, y + 132);
    canvas.setTextDatum(TL_DATUM);
}

void drawSunInfo(int x, int y, int dx, int dy) {
    canvas.setTextSize(3);
    canvas.setTextDatum(TC_DATUM);
    canvas.drawString("Sun & Moon", x + dx / 2, y + 7);
    canvas.setTextDatum(TL_DATUM);
    canvas.drawLine(x, y + PANEL_TITLE_HEIGHT, x + dx, y + PANEL_TITLE_HEIGHT, TFT_BLACK);

    // Draw sunrise
    canvas.setTextSize(3);
    drawIcon(x + 25, y + 50, SUNRISE64x64, WEATHER_ICON_SIZE, WEATHER_ICON_SIZE, false);
    if (currentWeather.sunriseTime.length() > 0) {
        canvas.drawString(currentWeather.sunriseTime, x + 100, y + 75);
    }

    // Draw sunset
    drawIcon(x + 25, y + 125, SUNSET64x64, WEATHER_ICON_SIZE, WEATHER_ICON_SIZE, false);
    if (currentWeather.sunsetTime.length() > 0) {
        canvas.drawString(currentWeather.sunsetTime, x + 100, y + 150);
    }

    // Draw moon phase
    float moonPhase = getMoonPhase();
    canvas.setTextSize(2);
    canvas.setTextDatum(TC_DATUM);
    String phaseText = "";
    if (moonPhase < MOON_PHASE_NEW_MIN || moonPhase > MOON_PHASE_NEW_MAX) phaseText = "New";
    else if (moonPhase < MOON_PHASE_WAXING_CRES) phaseText = "Waxing Cres";
    else if (moonPhase < MOON_PHASE_FIRST_QTR) phaseText = "First Qtr";
    else if (moonPhase < MOON_PHASE_WAXING_GIB) phaseText = "Waxing Gib";
    else if (moonPhase < MOON_PHASE_FULL) phaseText = "Full";
    else if (moonPhase < MOON_PHASE_WANING_GIB) phaseText = "Waning Gib";
    else if (moonPhase < MOON_PHASE_LAST_QTR) phaseText = "Last Qtr";
    else phaseText = "Waning Cres";

    canvas.drawString("Moon: " + phaseText, x + dx / 2, y + 210);
    canvas.setTextDatum(TL_DATUM);
}

void drawWindInfo(int x, int y, int dx, int dy) {
    canvas.setTextSize(3);
    canvas.drawString("Wind", x + dx / 2 - 40, y + 7);
    canvas.drawLine(x, y + PANEL_TITLE_HEIGHT, x + dx, y + PANEL_TITLE_HEIGHT, TFT_BLACK);

    drawWindCompass(x + dx / 2, y + dy / 2 + 20, currentWeather.windDir, currentWeather.windSpeed, COMPASS_RADIUS);
}

void drawM5PaperInfo(int x, int y, int dx, int dy) {
    canvas.setTextSize(3);
    canvas.setTextDatum(TC_DATUM);
    canvas.drawString("M5Paper S3", x + dx / 2, y + 7);
    canvas.setTextDatum(TL_DATUM);
    canvas.drawLine(x, y + PANEL_TITLE_HEIGHT, x + dx, y + PANEL_TITLE_HEIGHT, TFT_BLACK);

    // Draw date and time
    struct tm timeinfo;
    if (getLocalTime(&timeinfo)) {
        char dateStr[16], timeStr[16];
        sprintf(dateStr, "%02d.%02d.%04d", timeinfo.tm_mday, timeinfo.tm_mon + 1, timeinfo.tm_year + 1900);
        sprintf(timeStr, "%02d:%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);

        canvas.setTextSize(3);
        canvas.setTextDatum(TC_DATUM);
        canvas.drawString(dateStr, x + dx / 2, y + 55);
        canvas.drawString(timeStr, x + dx / 2, y + 95);
        canvas.setTextDatum(TL_DATUM);

        canvas.setTextSize(2);
        canvas.setTextDatum(TC_DATUM);
        canvas.drawString("updated", x + dx / 2, y + 120);
        canvas.setTextDatum(TL_DATUM);
    }

    // Draw internal temperature and humidity
    float sensorTemp = readInternalTemperature();
    float sensorHumid = readInternalHumidity();

    float displayTemp = (sensorTemp > SENSOR_ERROR_VALUE) ? sensorTemp : currentWeather.temperature;
    float displayHumid = (sensorHumid > SENSOR_ERROR_VALUE) ? sensorHumid : currentWeather.humidity;

    canvas.setTextSize(3);
    drawIcon(x + 35, y + 140, TEMPERATURE64x64, WEATHER_ICON_SIZE, WEATHER_ICON_SIZE, false);
    canvas.drawString(formatTemp(displayTemp), x + 35, y + 210);

    drawIcon(x + 145, y + 140, HUMIDITY64x64, WEATHER_ICON_SIZE, WEATHER_ICON_SIZE, false);
    canvas.drawString(String((int)displayHumid) + "%", x + 150, y + 210);
}

void displayWeather() {
    M5.Display.startWrite();

    if (!canvas.createSprite(SCREEN_WIDTH, SCREEN_HEIGHT)) {
        Serial.println("ERROR: Failed to allocate canvas memory!");
        M5.Display.endWrite();
        return;
    }

    canvas.fillSprite(TFT_WHITE);
    canvas.setTextColor(TFT_BLACK, TFT_WHITE);
    canvas.setTextDatum(TL_DATUM);
    canvas.setTextSize(2);

    // Draw header
    canvas.setTextSize(2);
    canvas.drawString(VERSION, 20, 10);
    canvas.setTextDatum(TC_DATUM);
    canvas.drawString(cityName, SCREEN_WIDTH / 2, 10);
    canvas.setTextDatum(TL_DATUM);

    // Draw WiFi signal strength
    int rssi = WiFi.RSSI();
    int quality = getRSSIQuality(rssi);
    canvas.setTextDatum(TR_DATUM);
    canvas.drawString(String(quality) + "%", SCREEN_WIDTH - 153, 10);
    canvas.setTextDatum(TL_DATUM);
    drawRSSI(SCREEN_WIDTH - 147, 23, rssi);

    // Draw battery level
    int batteryPercent = M5.Power.getBatteryLevel();
    if (batteryPercent < 0) batteryPercent = 0;
    if (batteryPercent > 100) batteryPercent = 100;

    canvas.setTextDatum(TR_DATUM);
    canvas.drawString(String(batteryPercent) + "%", SCREEN_WIDTH - 71, 10);
    canvas.setTextDatum(TL_DATUM);
    drawBattery(SCREEN_WIDTH - 60, 10, batteryPercent);

    // Draw config button indicator
    canvas.setTextSize(1);
    canvas.drawString("[CFG]", SCREEN_WIDTH - 50, SCREEN_HEIGHT - 20);

    // Draw main border
    canvas.drawRect(PANEL_BORDER, HEADER_HEIGHT, SCREEN_WIDTH - 28, SCREEN_HEIGHT - 43, TFT_BLACK);

    // Draw current weather panel
    const int contentX = PANEL_SPACING;
    const int contentW = SCREEN_WIDTH - 30;
    const int currentY = PANEL_TITLE_HEIGHT;
    const int currentH = 140;
    const int hourlyY = currentY + currentH;
    const int hourlyH = 165;
    const int dailyY = hourlyY + hourlyH;
    const int dailyH = 180;

    canvas.drawRect(contentX, currentY, contentW, currentH, TFT_BLACK);
    canvas.setTextDatum(TL_DATUM);
    canvas.setTextSize(2);
    canvas.drawString("Current Weather", contentX + 12, currentY + 10);
    canvas.drawLine(contentX, currentY + PANEL_TITLE_HEIGHT, contentX + contentW, currentY + PANEL_TITLE_HEIGHT, TFT_BLACK);
    drawCurrentConditions(contentX, currentY, contentW, currentH);

    // Draw next 8 hours panel
    canvas.drawRect(contentX, hourlyY, contentW, hourlyH, TFT_BLACK);
    canvas.drawString("Next 8 Hours", contentX + 12, hourlyY + 10);
    canvas.drawLine(contentX, hourlyY + PANEL_TITLE_HEIGHT, contentX + contentW, hourlyY + PANEL_TITLE_HEIGHT, TFT_BLACK);
    int hourlyCellW = contentW / MAX_HOURLY;
    for (int i = 0; i < MAX_HOURLY; i++) {
        int x = contentX + i * hourlyCellW;
        if (i > 0) {
            canvas.drawLine(x, hourlyY + PANEL_TITLE_HEIGHT, x, hourlyY + hourlyH, TFT_BLACK);
        }
        drawHourlyForecast(x, hourlyY + PANEL_TITLE_HEIGHT, hourlyCellW, hourlyH - PANEL_TITLE_HEIGHT, i);
    }

    // Draw next 3 days panel. Index 0 is today in the API arrays, so use 1-3.
    canvas.drawRect(contentX, dailyY, contentW, dailyH, TFT_BLACK);
    canvas.drawString("Next 3 Days", contentX + 12, dailyY + 10);
    canvas.drawLine(contentX, dailyY + PANEL_TITLE_HEIGHT, contentX + contentW, dailyY + PANEL_TITLE_HEIGHT, TFT_BLACK);
    int dailyCellW = contentW / 3;
    for (int i = 0; i < 3; i++) {
        int x = contentX + i * dailyCellW;
        if (i > 0) {
            canvas.drawLine(x, dailyY + PANEL_TITLE_HEIGHT, x, dailyY + dailyH, TFT_BLACK);
        }
        drawDailyForecast(x, dailyY + PANEL_TITLE_HEIGHT, dailyCellW, dailyH - PANEL_TITLE_HEIGHT, i + 1);
    }

    // Push to display
    canvas.pushSprite(0, 0);
    canvas.deleteSprite();

    M5.Display.endWrite();
    M5.Display.display();
}
