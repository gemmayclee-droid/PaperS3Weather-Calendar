#include "display.h"
#include "constants.h"
#include "utils.h"
#include "calendar_api.h"
#include "Icons.h"
#include <WiFi.h>

extern WeatherData currentWeather;
extern M5Canvas canvas;
extern String cityName;
extern bool useCelsius;
extern String calendarMode;
extern int displayFace;
extern String lastDataRefreshClock;

void useDefaultFont(int size = 2) {
    canvas.setFont(nullptr);
    canvas.setTextFont(1);
    canvas.setTextSize(size);
}

void useDisplayFont(int size = 2) {
    useDefaultFont(size);
}

void useCompactDisplayFont(int englishSize = 2) {
    useDefaultFont(englishSize);
}

String fitText(String text, int maxWidth);

String getDisplayDateLabel(int dayOffset) {
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) {
        return "";
    }

    timeinfo.tm_mday += dayOffset;
    mktime(&timeinfo);

    char dateStr[12];
    sprintf(dateStr, "%02d/%02d", timeinfo.tm_mon + 1, timeinfo.tm_mday);
    const char* daysEn[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    return String(daysEn[timeinfo.tm_wday]) + " " + String(dateStr);
}

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

void drawIconScaled(int x, int y, const uint8_t *icon, int sourceSize, int targetSize, bool highContrast) {
    const uint16_t *icon16 = (const uint16_t *)icon;

    for (int yi = 0; yi < targetSize; yi++) {
        int sourceY = yi * sourceSize / targetSize;
        for (int xi = 0; xi < targetSize; xi++) {
            int sourceX = xi * sourceSize / targetSize;
            uint16_t pixel = icon16[sourceY * sourceSize + sourceX];
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
    String timeLabel = currentWeather.hourly[index].timeLabel.length() > 0 ?
                       currentWeather.hourly[index].timeLabel : "--:--";
    int forecastHour = timeLabel.length() >= 2 ? timeLabel.substring(0, 2).toInt() : 12;

    useDefaultFont(3);
    canvas.setTextDatum(TC_DATUM);
    canvas.drawString(timeLabel, x + dx / 2, y + 6);
    canvas.setTextDatum(TL_DATUM);

    bool isDay = isDaytime(forecastHour);
    const int iconSize = 48;
    int iconX = x + dx / 2 - iconSize / 2;
    int iconY = y + 34;
    const uint8_t* weatherIcon = getWeatherIcon(currentWeather.hourly[index].weatherCode, isDay);
    drawIconScaled(iconX, iconY, weatherIcon, WEATHER_ICON_SIZE, iconSize, true);

    canvas.setTextDatum(TC_DATUM);
    useDefaultFont(3);
    canvas.drawString(formatTemp(currentWeather.hourly[index].temp), x + dx / 2, y + 90);

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

    int tempFontH = canvas.fontHeight();
    int degreeX = mainTempX + canvas.textWidth(tempNum) + 8;
    int degreeY = mainTempY + 11;
    drawDegreeSymbol(degreeX, degreeY, TEMP_DEGREE_RADIUS_LARGE);

    // Weather icon: match temperature font height, tight gap after degree
    struct tm timeinfo;
    bool hasTime = getLocalTime(&timeinfo);
    if (!hasTime) {
        timeinfo.tm_hour = 12;
    }
    bool isDay = isDaytime(timeinfo.tm_hour);
    const uint8_t* weatherIcon = getWeatherIcon(currentWeather.weatherCode, isDay);

    int iconSize = tempFontH > 0 ? tempFontH : 72;
    if (iconSize < 64) {
        iconSize = 64;
    }
    if (iconSize > 96) {
        iconSize = 96;
    }
    int iconX = degreeX + TEMP_DEGREE_RADIUS_LARGE * 2 + 14;
    int iconY = mainTempY + (tempFontH - iconSize) / 2;
    if (iconY < y + PANEL_TITLE_HEIGHT + 4) {
        iconY = y + PANEL_TITLE_HEIGHT + 4;
    }
    drawIconScaled(iconX, iconY, weatherIcon, WEATHER_ICON_SIZE, iconSize, true);

    // Condition text keeps the larger Face-0 size
    useDisplayFont(2);
    String condition = getWeatherConditionText(currentWeather.weatherCode);
    int conditionX = iconX + iconSize + 16;
    canvas.setTextSize(condition.length() > 14 ? 2 : 3);
    canvas.drawString(condition, conditionX, y + 48);

    useDisplayFont(2);
    canvas.drawString(String("Feels ") + formatTemp(currentWeather.apparentTemperature), conditionX, y + 84);
    canvas.drawString(String("Today ") + formatTemp(currentWeather.todayMinTemp) + " / " +
                      formatTemp(currentWeather.todayMaxTemp), conditionX, y + 110);

    int detailsX = x + 545;
    useDisplayFont(2);
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
        canvas.drawString(getDisplayDateLabel(0), sunX, y + 102);
    }

    canvas.setTextDatum(TL_DATUM);
}

String getForecastDateLabel(int dayOffset) {
    String dateLabel = getDisplayDateLabel(dayOffset);
    return dateLabel.length() > 0 ? dateLabel : "D+" + String(dayOffset);
}

void drawDailyForecast(int x, int y, int dx, int dy, int forecastIndex) {
    canvas.setTextDatum(TC_DATUM);
    int textW = dx - 14;
    useCompactDisplayFont(3);
    canvas.drawString(fitText(getForecastDateLabel(forecastIndex), textW), x + dx / 2, y + 6);

    const uint8_t* weatherIcon = getWeatherIcon(currentWeather.forecastWeatherCode[forecastIndex], true);
    drawIconScaled(x + dx / 2 - 26, y + 34, weatherIcon, WEATHER_ICON_SIZE, 52, true);

    useCompactDisplayFont(3);
    String tempText = formatTemp(currentWeather.forecastMinTemp[forecastIndex]) + "/" +
                      formatTemp(currentWeather.forecastMaxTemp[forecastIndex]);
    canvas.drawString(fitText(tempText, textW), x + dx / 2, y + 96);
    canvas.drawString(fitText(String("Rain ") + String((int)currentWeather.forecastRain[forecastIndex]) + "%", textW),
                      x + dx / 2, y + 122);
    canvas.setTextDatum(TL_DATUM);
}

String fitText(String text, int maxWidth) {
    if (canvas.textWidth(text) <= maxWidth) {
        return text;
    }

    while (text.length() > 3 && canvas.textWidth(text + "...") > maxWidth) {
        text.remove(text.length() - 1);
    }
    return text + "...";
}

void drawCalendarEvents(int x, int y, int dx, int dy, int textSize, int lineSpacing) {
    canvas.setTextDatum(TL_DATUM);
    useDisplayFont(textSize);

    if (!calendarFetchOk) {
        canvas.drawString("Calendar sync failed", x + 14, y + 20);
        if (calendarStatusMessage.length() > 0) {
            canvas.drawString(fitText(calendarStatusMessage, dx - 28), x + 14, y + 20 + lineSpacing);
        }
        return;
    }

    if (calendarEventCount == 0) {
        canvas.drawString("No events today", x + 14, y + 20);
        return;
    }

    int lineY = y + 14;
    int maxTextWidth = dx - 28;
    for (int i = 0; i < calendarEventCount; i++) {
        canvas.drawString(fitText(calendarEvents[i], maxTextWidth), x + 14, lineY);
        lineY += lineSpacing;
    }
}

void drawMonthCalendar(int x, int y, int dx, int dy, int textSize) {
    if (textSize < 1) {
        textSize = 1;
    }

    int year = 0, month = 0, todayDay = 0;
    if (!resolveCalendarDate(year, month, todayDay)) {
        canvas.setTextDatum(TL_DATUM);
        useDisplayFont(textSize > 1 ? textSize : 2);
        canvas.setTextColor(TFT_BLACK, TFT_WHITE);
        canvas.drawString("Date unavailable", x + 14, y + 20);
        return;
    }

    const int headerH = MONTH_CAL_WEEKDAY_HEADER_H + (textSize - 1) * 12;
    const int gridH = dy - headerH;
    if (gridH < MONTH_CAL_WEEK_ROWS || dx < MONTH_CAL_WEEK_COLS) {
        return;
    }

    const int cellW = dx / MONTH_CAL_WEEK_COLS;
    const int cellH = gridH / MONTH_CAL_WEEK_ROWS;
    const int gridW = cellW * MONTH_CAL_WEEK_COLS;
    const int gridTop = y + headerH;
    const int firstWeekday = weekdaySundayZero(year, month, 1);
    const int dim = daysInMonth(year, month);

    static const char* WEEKDAYS[] = {"S", "M", "T", "W", "T", "F", "S"};
    useDisplayFont(textSize);
    canvas.setTextDatum(TC_DATUM);
    canvas.setTextColor(TFT_BLACK, TFT_WHITE);
    for (int col = 0; col < MONTH_CAL_WEEK_COLS; col++) {
        int cx = x + col * cellW + cellW / 2;
        canvas.drawString(WEEKDAYS[col], cx, y + 2);
    }

    canvas.setTextDatum(MC_DATUM);
    for (int d = 1; d <= dim; d++) {
        int index = firstWeekday + d - 1;
        int row = index / MONTH_CAL_WEEK_COLS;
        int col = index % MONTH_CAL_WEEK_COLS;
        if (row >= MONTH_CAL_WEEK_ROWS) {
            break;
        }

        int cellX = x + col * cellW;
        int cellY = gridTop + row * cellH;
        int textX = cellX + cellW / 2;
        int textY = cellY + cellH / 2;
        String dayStr = String(d);

        if (d == todayDay) {
            canvas.fillRect(cellX + 1, cellY + 1, cellW - 1, cellH - 1, TFT_BLACK);
            canvas.setTextColor(TFT_WHITE, TFT_BLACK);
            canvas.drawString(dayStr, textX, textY);
            canvas.setTextColor(TFT_BLACK, TFT_WHITE);
        } else {
            canvas.setTextColor(TFT_BLACK, TFT_WHITE);
            canvas.drawString(dayStr, textX, textY);
        }
    }

    // Grid lines after fills so today cell stays crisp
    for (int col = 0; col <= MONTH_CAL_WEEK_COLS; col++) {
        int lx = x + col * cellW;
        canvas.drawLine(lx, gridTop, lx, gridTop + cellH * MONTH_CAL_WEEK_ROWS, TFT_BLACK);
    }
    for (int row = 0; row <= MONTH_CAL_WEEK_ROWS; row++) {
        int ly = gridTop + row * cellH;
        canvas.drawLine(x, ly, x + gridW, ly, TFT_BLACK);
    }

    canvas.setTextDatum(TL_DATUM);
    canvas.setTextColor(TFT_BLACK, TFT_WHITE);
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
    canvas.drawString("M5Paper", x + dx / 2, y + 7);
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

static void drawStatusHeader() {
    useDisplayFont(3);
    canvas.setTextDatum(TL_DATUM);

    char nowClock[8] = "--:--";
    struct tm nowInfo;
    if (getLocalTime(&nowInfo)) {
        sprintf(nowClock, "%02d:%02d", nowInfo.tm_hour, nowInfo.tm_min);
    }
    String headerLeft = String(VERSION) + "  " + nowClock + "  upd " + lastDataRefreshClock + " " + cityName;
    canvas.drawString(headerLeft, 8, 6);

    int rssi = WiFi.RSSI();
    int quality = getRSSIQuality(rssi);
    canvas.setTextDatum(TR_DATUM);
    canvas.drawString(String(quality) + "%", SCREEN_WIDTH - 153, 8);
    canvas.setTextDatum(TL_DATUM);
    drawRSSI(SCREEN_WIDTH - 147, 23, rssi);

    int batteryPercent = M5.Power.getBatteryLevel();
    if (batteryPercent < 0) batteryPercent = 0;
    if (batteryPercent > 100) batteryPercent = 100;

    canvas.setTextDatum(TR_DATUM);
    canvas.drawString(String(batteryPercent) + "%", SCREEN_WIDTH - 71, 8);
    canvas.setTextDatum(TL_DATUM);
    drawBattery(SCREEN_WIDTH - 60, 10, batteryPercent);
}

void displayWeather() {
    M5.Display.startWrite();

    canvas.setColorDepth(8);
    if (!canvas.createSprite(SCREEN_WIDTH, SCREEN_HEIGHT)) {
        Serial.println("ERROR: Failed to allocate canvas memory!");
        M5.Display.endWrite();
        return;
    }

    canvas.fillSprite(TFT_WHITE);
    canvas.setTextColor(TFT_BLACK, TFT_WHITE);
    canvas.setTextDatum(TL_DATUM);

    drawStatusHeader();

    useDisplayFont(2);
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
    useDisplayFont(3);
    canvas.drawString("Current Weather", contentX + 12, currentY + 8);
    canvas.drawLine(contentX, currentY + PANEL_TITLE_HEIGHT, contentX + contentW, currentY + PANEL_TITLE_HEIGHT, TFT_BLACK);
    drawCurrentConditions(contentX, currentY, contentW, currentH);

    // Draw next 8 hours panel
    canvas.drawRect(contentX, hourlyY, contentW, hourlyH, TFT_BLACK);
    useDisplayFont(3);
    canvas.drawString("Next 8 Hours", contentX + 12, hourlyY + 8);
    canvas.drawLine(contentX, hourlyY + PANEL_TITLE_HEIGHT, contentX + contentW, hourlyY + PANEL_TITLE_HEIGHT, TFT_BLACK);
    int hourlyCellW = contentW / MAX_HOURLY;
    for (int i = 0; i < MAX_HOURLY; i++) {
        int x = contentX + i * hourlyCellW;
        if (i > 0) {
            canvas.drawLine(x, hourlyY + PANEL_TITLE_HEIGHT, x, hourlyY + hourlyH, TFT_BLACK);
        }
        drawHourlyForecast(x, hourlyY + PANEL_TITLE_HEIGHT, hourlyCellW, hourlyH - PANEL_TITLE_HEIGHT, i);
    }

    // Draw next 3 days panel and reserve space for Google Calendar.
    const int calendarW = 360;
    const int forecastW = contentW - calendarW;
    const int calendarX = contentX + forecastW;

    canvas.drawRect(contentX, dailyY, forecastW, dailyH, TFT_BLACK);
    useDisplayFont(3);
    canvas.drawString("Next 3 Days", contentX + 12, dailyY + 8);
    canvas.drawLine(contentX, dailyY + PANEL_TITLE_HEIGHT, contentX + forecastW, dailyY + PANEL_TITLE_HEIGHT, TFT_BLACK);
    int dailyCellW = forecastW / 3;
    for (int i = 0; i < 3; i++) {
        int x = contentX + i * dailyCellW;
        if (i > 0) {
            canvas.drawLine(x, dailyY + PANEL_TITLE_HEIGHT, x, dailyY + dailyH, TFT_BLACK);
        }
        drawDailyForecast(x, dailyY + PANEL_TITLE_HEIGHT, dailyCellW, dailyH - PANEL_TITLE_HEIGHT, i + 1);
    }

    canvas.drawRect(calendarX, dailyY, calendarW, dailyH, TFT_BLACK);
    useDisplayFont(3);
    if (calendarMode == "google") {
        canvas.drawString("Google Calendar", calendarX + 12, dailyY + 8);
        canvas.drawLine(calendarX, dailyY + PANEL_TITLE_HEIGHT, calendarX + calendarW, dailyY + PANEL_TITLE_HEIGHT, TFT_BLACK);
        drawCalendarEvents(calendarX, dailyY + PANEL_TITLE_HEIGHT, calendarW, dailyH - PANEL_TITLE_HEIGHT, 3, 40);
    } else {
        int year = 0, month = 0, day = 0;
        String title = "Month Calendar";
        if (resolveCalendarDate(year, month, day)) {
            title = String(monthNameEnglish(month)) + " " + String(year);
        }
        canvas.drawString(title, calendarX + 12, dailyY + 8);
        canvas.drawLine(calendarX, dailyY + PANEL_TITLE_HEIGHT, calendarX + calendarW, dailyY + PANEL_TITLE_HEIGHT, TFT_BLACK);
        drawMonthCalendar(calendarX, dailyY + PANEL_TITLE_HEIGHT, calendarW, dailyH - PANEL_TITLE_HEIGHT, 2);
    }

    // Push to display
    canvas.pushSprite(0, 0);
    canvas.deleteSprite();

    M5.Display.endWrite();
    M5.Display.display();
}

void drawFace1WeatherSummary(int x, int y, int dx, int dy) {
    canvas.setTextDatum(TL_DATUM);
    useDisplayFont(3);
    canvas.drawString("Current Weather", x + 12, y + 8);
    canvas.drawLine(x, y + PANEL_TITLE_HEIGHT, x + dx, y + PANEL_TITLE_HEIGHT, TFT_BLACK);

    const int contentY = y + PANEL_TITLE_HEIGHT;
    const int iconSize = FACE1_WEATHER_ICON_SIZE;

    // Temperature at 2x previous Face 1 size (FreeSansBold24pt size 2)
    canvas.setFont(&fonts::FreeSansBold24pt7b);
    canvas.setTextSize(2);
    String tempNum = String((int)currentWeather.temperature);
    int mainTempX = x + 12;
    int mainTempY = contentY + 16;
    canvas.drawString(tempNum, mainTempX, mainTempY);
    int degreeX = mainTempX + canvas.textWidth(tempNum) + 8;
    int degreeY = mainTempY + 14;
    drawDegreeSymbol(degreeX, degreeY, 10);
    useDisplayFont(3);
    canvas.drawString(useCelsius ? "C" : "F", degreeX + 18, mainTempY + 48);

    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) {
        timeinfo.tm_hour = 12;
    }
    bool isDay = isDaytime(timeinfo.tm_hour);
    const uint8_t* weatherIcon = getWeatherIcon(currentWeather.weatherCode, isDay);

    // Icon 4x source size (256px), right side of weather panel
    int iconX = x + dx - iconSize - 8;
    int iconY = contentY + 4;
    if (iconY + iconSize > y + dy - 4) {
        iconY = y + dy - iconSize - 4;
    }
    drawIconScaled(iconX, iconY, weatherIcon, WEATHER_ICON_SIZE, iconSize, true);

    // Details under the temperature, left of the large icon
    int textMaxW = iconX - mainTempX - 8;
    if (textMaxW < 80) {
        textMaxW = dx / 2;
    }
    useDisplayFont(3);
    String condition = getWeatherConditionText(currentWeather.weatherCode);
    canvas.drawString(fitText(condition, textMaxW), mainTempX, contentY + 120);
    canvas.drawString(String("Feels ") + formatTemp(currentWeather.apparentTemperature), mainTempX, contentY + 158);
    canvas.drawString(String("Today ") + formatTemp(currentWeather.todayMinTemp) + " / " +
                      formatTemp(currentWeather.todayMaxTemp), mainTempX, contentY + 196);

    canvas.setTextDatum(TL_DATUM);
}

static void alignEpdRect(int &x, int &w) {
    int x2 = x + w;
    x = x & ~3;
    x2 = (x2 + 3) & ~3;
    w = x2 - x;
    if (w < 4) {
        w = 4;
    }
}

static void refreshEpdRegion(int x, int y, int w, int h) {
    alignEpdRect(x, w);
    M5.Display.setEpdMode(epd_mode_t::epd_fast);
    M5.Display.setClipRect(x, y, w, h);
    M5.Display.display();
    M5.Display.waitDisplay();
    M5.Display.clearClipRect();
    M5.Display.setEpdMode(epd_mode_t::epd_quality);
}

static void drawFace1ClockContent(M5Canvas &spr, int w, int h) {
    spr.fillSprite(TFT_WHITE);
    spr.setTextColor(TFT_BLACK, TFT_WHITE);
    spr.drawRect(0, 0, w, h, TFT_BLACK);

    char timeStr[8] = "--:--";
    struct tm timeinfo;
    bool hasLocalTime = getLocalTime(&timeinfo);
    if (hasLocalTime) {
        sprintf(timeStr, "%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);
    }

    spr.setFont(&fonts::FreeSansBold24pt7b);
    spr.setTextSize(2);
    spr.setTextDatum(TC_DATUM);
    spr.drawString(timeStr, w / 2, 28);

    spr.setFont(nullptr);
    spr.setTextFont(1);
    spr.setTextSize(3);
    spr.setTextDatum(TC_DATUM);
    int year = 0, month = 0, day = 0;
    String dateLine = "Date unavailable";
    if (resolveCalendarDate(year, month, day)) {
        static const char* daysEn[] = {"Sunday", "Monday", "Tuesday", "Wednesday",
                                       "Thursday", "Friday", "Saturday"};
        int wday = weekdaySundayZero(year, month, day);
        char buf[48];
        sprintf(buf, "%s %02d %s %04d", daysEn[wday], day, monthNameEnglish(month), year);
        dateLine = String(buf);
    } else if (hasLocalTime) {
        dateLine = getDisplayDateLabel(0);
    }
    spr.drawString(dateLine, w / 2, 130);
    spr.setTextDatum(TL_DATUM);
}

void updateClockFacePartial(bool updateWeather) {
    const int leftX = FACE1_MARGIN;
    const int clockY = FACE1_CONTENT_TOP;
    const int weatherY = FACE1_CONTENT_TOP + FACE1_TOP_H + FACE1_GAP;

    M5.Display.startWrite();

    // Refresh status header (city / wifi / battery) on each clock tick
    canvas.setColorDepth(8);
    if (canvas.createSprite(SCREEN_WIDTH, HEADER_HEIGHT)) {
        canvas.fillSprite(TFT_WHITE);
        canvas.setTextColor(TFT_BLACK, TFT_WHITE);
        drawStatusHeader();
        canvas.pushSprite(0, 0);
        canvas.deleteSprite();
        refreshEpdRegion(0, 0, SCREEN_WIDTH, HEADER_HEIGHT);
    }

    M5Canvas clockSpr(&M5.Display);
    clockSpr.setColorDepth(8);
    if (clockSpr.createSprite(FACE1_LEFT_W, FACE1_TOP_H)) {
        drawFace1ClockContent(clockSpr, FACE1_LEFT_W, FACE1_TOP_H);
        clockSpr.pushSprite(leftX, clockY);
        clockSpr.deleteSprite();
        refreshEpdRegion(leftX, clockY, FACE1_LEFT_W, FACE1_TOP_H);
    }

    if (updateWeather) {
        canvas.setColorDepth(8);
        if (canvas.createSprite(FACE1_LEFT_W, FACE1_BOTTOM_H)) {
            canvas.fillSprite(TFT_WHITE);
            canvas.setTextColor(TFT_BLACK, TFT_WHITE);
            canvas.drawRect(0, 0, FACE1_LEFT_W, FACE1_BOTTOM_H, TFT_BLACK);
            drawFace1WeatherSummary(0, 0, FACE1_LEFT_W, FACE1_BOTTOM_H);
            canvas.pushSprite(leftX, weatherY);
            canvas.deleteSprite();
            refreshEpdRegion(leftX, weatherY, FACE1_LEFT_W, FACE1_BOTTOM_H);
        }
    }

    M5.Display.endWrite();
}

void displayClockFace() {
    M5.Display.setEpdMode(epd_mode_t::epd_quality);
    M5.Display.startWrite();

    canvas.setColorDepth(8);
    if (!canvas.createSprite(SCREEN_WIDTH, SCREEN_HEIGHT)) {
        Serial.println("ERROR: Failed to allocate canvas memory!");
        M5.Display.endWrite();
        return;
    }

    canvas.fillSprite(TFT_WHITE);
    canvas.setTextColor(TFT_BLACK, TFT_WHITE);
    canvas.setTextDatum(TL_DATUM);

    drawStatusHeader();

    useDisplayFont(3);

    const int leftX = FACE1_MARGIN;
    const int clockY = FACE1_CONTENT_TOP;
    const int weatherY = FACE1_CONTENT_TOP + FACE1_TOP_H + FACE1_GAP;
    const int rightX = FACE1_MARGIN + FACE1_LEFT_W + FACE1_GAP;
    const int rightH = SCREEN_HEIGHT - FACE1_CONTENT_TOP - FACE1_MARGIN;

    // Clock + date panel
    canvas.drawRect(leftX, clockY, FACE1_LEFT_W, FACE1_TOP_H, TFT_BLACK);

    char timeStr[8] = "--:--";
    struct tm timeinfo;
    bool hasLocalTime = getLocalTime(&timeinfo);
    if (hasLocalTime) {
        sprintf(timeStr, "%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);
    }

    canvas.setFont(&fonts::FreeSansBold24pt7b);
    canvas.setTextSize(2);
    canvas.setTextDatum(TC_DATUM);
    canvas.drawString(timeStr, leftX + FACE1_LEFT_W / 2, clockY + 28);

    useDisplayFont(3);
    canvas.setTextDatum(TC_DATUM);
    int year = 0, month = 0, day = 0;
    String dateLine = "Date unavailable";
    if (resolveCalendarDate(year, month, day)) {
        static const char* daysEn[] = {"Sunday", "Monday", "Tuesday", "Wednesday",
                                       "Thursday", "Friday", "Saturday"};
        int wday = weekdaySundayZero(year, month, day);
        char buf[48];
        sprintf(buf, "%s %02d %s %04d", daysEn[wday], day, monthNameEnglish(month), year);
        dateLine = String(buf);
    } else if (hasLocalTime) {
        dateLine = getDisplayDateLabel(0);
    }
    canvas.drawString(dateLine, leftX + FACE1_LEFT_W / 2, clockY + 130);
    canvas.setTextDatum(TL_DATUM);

    // Weather summary panel
    canvas.drawRect(leftX, weatherY, FACE1_LEFT_W, FACE1_BOTTOM_H, TFT_BLACK);
    drawFace1WeatherSummary(leftX, weatherY, FACE1_LEFT_W, FACE1_BOTTOM_H);

    // Large calendar panel
    canvas.drawRect(rightX, FACE1_CONTENT_TOP, FACE1_RIGHT_W, rightH, TFT_BLACK);
    useDisplayFont(3);
    if (calendarMode == "google") {
        canvas.drawString("Google Calendar", rightX + 12, FACE1_CONTENT_TOP + 8);
        canvas.drawLine(rightX, FACE1_CONTENT_TOP + PANEL_TITLE_HEIGHT,
                        rightX + FACE1_RIGHT_W, FACE1_CONTENT_TOP + PANEL_TITLE_HEIGHT, TFT_BLACK);
        drawCalendarEvents(rightX, FACE1_CONTENT_TOP + PANEL_TITLE_HEIGHT,
                           FACE1_RIGHT_W, rightH - PANEL_TITLE_HEIGHT, 3, 48);
    } else {
        String title = "Month Calendar";
        if (year > 0 && month > 0) {
            title = String(monthNameEnglish(month)) + " " + String(year);
        } else {
            int y2 = 0, m2 = 0, d2 = 0;
            if (resolveCalendarDate(y2, m2, d2)) {
                title = String(monthNameEnglish(m2)) + " " + String(y2);
            }
        }
        canvas.drawString(title, rightX + 12, FACE1_CONTENT_TOP + 8);
        canvas.drawLine(rightX, FACE1_CONTENT_TOP + PANEL_TITLE_HEIGHT,
                        rightX + FACE1_RIGHT_W, FACE1_CONTENT_TOP + PANEL_TITLE_HEIGHT, TFT_BLACK);
        drawMonthCalendar(rightX, FACE1_CONTENT_TOP + PANEL_TITLE_HEIGHT,
                          FACE1_RIGHT_W, rightH - PANEL_TITLE_HEIGHT, 3);
    }

    useDisplayFont(2);
    canvas.setTextDatum(TL_DATUM);
    canvas.drawString("[CFG]", SCREEN_WIDTH - 50, SCREEN_HEIGHT - 20);

    canvas.pushSprite(0, 0);
    canvas.deleteSprite();

    M5.Display.endWrite();
    M5.Display.display();
}

void showActiveFace() {
    if (displayFace == 1) {
        displayClockFace();
    } else {
        displayWeather();
    }
}
