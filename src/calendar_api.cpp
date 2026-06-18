#include "calendar_api.h"
#include "constants.h"
#include <HTTPClient.h>

String calendarEvents[MAX_CALENDAR_EVENTS];
int calendarEventCount = 0;

static String normalizeCalendarUrl(String url) {
    url.trim();
    if (url.startsWith("webcal://")) {
        url = "https://" + url.substring(9);
    }
    return url;
}

static String todayString() {
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) {
        return "";
    }

    char dateStr[9];
    sprintf(dateStr, "%04d%02d%02d", timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday);
    return String(dateStr);
}

static String unfoldLine(const String &payload, int &pos) {
    String line = "";

    while (pos < payload.length()) {
        char c = payload[pos++];
        if (c == '\r') {
            continue;
        }
        if (c == '\n') {
            if (pos < payload.length() && (payload[pos] == ' ' || payload[pos] == '\t')) {
                pos++;
                continue;
            }
            break;
        }
        line += c;
    }

    return line;
}

static String valueAfterColon(const String &line) {
    int colon = line.indexOf(':');
    if (colon < 0 || colon >= line.length() - 1) {
        return "";
    }
    return line.substring(colon + 1);
}

static String cleanSummary(String summary) {
    summary.replace("\\n", " ");
    summary.replace("\\,", ",");
    summary.replace("\\;", ";");
    summary.replace("\\\\", "\\");
    summary.trim();
    return summary;
}

static String eventTimeLabel(const String &dtstart) {
    int timePos = dtstart.indexOf('T');
    if (timePos < 0 || timePos + 4 >= dtstart.length()) {
        return "All day";
    }

    return dtstart.substring(timePos + 1, timePos + 3) + ":" + dtstart.substring(timePos + 3, timePos + 5);
}

static void clearCalendarEvents() {
    calendarEventCount = 0;
    for (int i = 0; i < MAX_CALENDAR_EVENTS; i++) {
        calendarEvents[i] = "";
    }
}

bool fetchCalendarData(const String &icsUrl) {
    clearCalendarEvents();

    String url = normalizeCalendarUrl(icsUrl);
    if (url.length() == 0) {
        Serial.println("No Google Calendar ICS URL configured");
        return true;
    }

    HTTPClient http;
    http.begin(url);
    http.setTimeout(HTTP_TIMEOUT_MS);

    int httpCode = http.GET();
    if (httpCode != HTTP_CODE_OK) {
        Serial.printf("Calendar HTTP error code: %d\n", httpCode);
        http.end();
        return false;
    }

    String payload = http.getString();
    http.end();

    String today = todayString();
    if (today.length() == 0) {
        Serial.println("Calendar skipped: local date unavailable");
        return false;
    }

    bool inEvent = false;
    String dtstart = "";
    String summary = "";

    int pos = 0;
    while (pos < payload.length() && calendarEventCount < MAX_CALENDAR_EVENTS) {
        String line = unfoldLine(payload, pos);

        if (line == "BEGIN:VEVENT") {
            inEvent = true;
            dtstart = "";
            summary = "";
            continue;
        }

        if (line == "END:VEVENT") {
            if (inEvent && dtstart.startsWith(today) && summary.length() > 0) {
                calendarEvents[calendarEventCount++] = eventTimeLabel(dtstart) + " " + cleanSummary(summary);
            }
            inEvent = false;
            continue;
        }

        if (!inEvent) {
            continue;
        }

        if (line.startsWith("DTSTART")) {
            dtstart = valueAfterColon(line);
        } else if (line.startsWith("SUMMARY")) {
            summary = valueAfterColon(line);
        }
    }

    Serial.printf("Calendar events today: %d\n", calendarEventCount);
    return true;
}
