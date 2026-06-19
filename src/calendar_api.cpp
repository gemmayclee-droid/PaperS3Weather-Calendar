#include "calendar_api.h"
#include "constants.h"
#include "utils.h"
#include <HTTPClient.h>
#include <time.h>

String calendarEvents[MAX_CALENDAR_EVENTS];
int calendarEventCount = 0;
bool calendarFetchOk = true;

extern WeatherData currentWeather;

static String normalizeCalendarUrl(String url) {
    url.trim();
    if (url.startsWith("webcal://")) {
        url = "https://" + url.substring(9);
    }
    return url;
}

static String todayString() {
    if (currentWeather.localDateYmd.length() == 8) {
        return currentWeather.localDateYmd;
    }

    time_t now = time(nullptr);
    if (now <= 0) {
        return "";
    }

    int utcOffsetSeconds = currentWeather.utcOffsetSeconds != 0 ? currentWeather.utcOffsetSeconds : (TIMEZONE_OFFSET_HOURS * 3600);
    now += utcOffsetSeconds;

    struct tm timeinfo;
    gmtime_r(&now, &timeinfo);

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

static int dateToEpochDay(const String &date) {
    if (date.length() < 8) {
        return -1;
    }

    struct tm timeinfo = {};
    timeinfo.tm_year = date.substring(0, 4).toInt() - 1900;
    timeinfo.tm_mon = date.substring(4, 6).toInt() - 1;
    timeinfo.tm_mday = date.substring(6, 8).toInt();
    timeinfo.tm_isdst = -1;
    return (int)(mktime(&timeinfo) / 86400);
}

static int weekdayIndex(const String &date) {
    if (date.length() < 8) {
        return -1;
    }

    struct tm timeinfo = {};
    timeinfo.tm_year = date.substring(0, 4).toInt() - 1900;
    timeinfo.tm_mon = date.substring(4, 6).toInt() - 1;
    timeinfo.tm_mday = date.substring(6, 8).toInt();
    timeinfo.tm_isdst = -1;
    mktime(&timeinfo);
    return timeinfo.tm_wday;
}

static bool rruleIncludesToday(const String &rrule, const String &dtstart, const String &today) {
    if (rrule.length() == 0 || dtstart.length() < 8 || today.length() < 8) {
        return false;
    }

    String startDate = dtstart.substring(0, 8);
    int startDay = dateToEpochDay(startDate);
    int todayDay = dateToEpochDay(today);
    if (startDay < 0 || todayDay < startDay) {
        return false;
    }

    int untilPos = rrule.indexOf("UNTIL=");
    if (untilPos >= 0) {
        String untilDate = rrule.substring(untilPos + 6, untilPos + 14);
        if (untilDate.length() == 8 && today.compareTo(untilDate) > 0) {
            return false;
        }
    }

    int interval = 1;
    int intervalPos = rrule.indexOf("INTERVAL=");
    if (intervalPos >= 0) {
        int endPos = rrule.indexOf(';', intervalPos);
        String intervalStr = endPos >= 0 ? rrule.substring(intervalPos + 9, endPos) : rrule.substring(intervalPos + 9);
        interval = intervalStr.toInt();
        if (interval < 1) {
            interval = 1;
        }
    }

    if (rrule.indexOf("FREQ=DAILY") >= 0) {
        return ((todayDay - startDay) % interval) == 0;
    }

    if (rrule.indexOf("FREQ=WEEKLY") >= 0) {
        if (((todayDay - startDay) / 7) % interval != 0) {
            return false;
        }

        int bydayPos = rrule.indexOf("BYDAY=");
        if (bydayPos < 0) {
            return weekdayIndex(startDate) == weekdayIndex(today);
        }

        int endPos = rrule.indexOf(';', bydayPos);
        String byday = endPos >= 0 ? rrule.substring(bydayPos + 6, endPos) : rrule.substring(bydayPos + 6);
        const char* weekdays[] = {"SU", "MO", "TU", "WE", "TH", "FR", "SA"};
        int todayWeekday = weekdayIndex(today);
        return todayWeekday >= 0 && byday.indexOf(weekdays[todayWeekday]) >= 0;
    }

    return false;
}

static void clearCalendarEvents() {
    calendarFetchOk = true;
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
        calendarFetchOk = false;
        return true;
    }

    HTTPClient http;
    http.begin(url);
    http.setTimeout(HTTP_TIMEOUT_MS);

    int httpCode = http.GET();
    if (httpCode != HTTP_CODE_OK) {
        Serial.printf("Calendar HTTP error code: %d\n", httpCode);
        http.end();
        calendarFetchOk = false;
        return false;
    }

    String payload = http.getString();
    http.end();

    String today = todayString();
    if (today.length() == 0) {
        Serial.println("Calendar skipped: local date unavailable");
        calendarFetchOk = false;
        return false;
    }

    bool inEvent = false;
    String dtstart = "";
    String summary = "";
    String rrule = "";

    int pos = 0;
    while (pos < payload.length() && calendarEventCount < MAX_CALENDAR_EVENTS) {
        String line = unfoldLine(payload, pos);

        if (line == "BEGIN:VEVENT") {
            inEvent = true;
            dtstart = "";
            summary = "";
            rrule = "";
            continue;
        }

        if (line == "END:VEVENT") {
            bool isToday = dtstart.startsWith(today) || rruleIncludesToday(rrule, dtstart, today);
            if (inEvent && isToday && summary.length() > 0) {
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
        } else if (line.startsWith("RRULE")) {
            rrule = valueAfterColon(line);
        }
    }

    Serial.printf("Calendar events today: %d\n", calendarEventCount);
    return true;
}
