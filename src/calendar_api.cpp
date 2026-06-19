#include "calendar_api.h"
#include "constants.h"
#include "utils.h"
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <time.h>

String calendarEvents[MAX_CALENDAR_EVENTS];
int calendarEventCount = 0;
bool calendarFetchOk = true;
String calendarStatusMessage = "";

extern WeatherData currentWeather;

static String urlDecode(String value) {
    String decoded = "";
    for (int i = 0; i < value.length(); i++) {
        char c = value[i];
        if (c == '%' && i + 2 < value.length()) {
            char hex[3] = {value[i + 1], value[i + 2], '\0'};
            decoded += (char)strtol(hex, nullptr, 16);
            i += 2;
        } else if (c == '+') {
            decoded += ' ';
        } else {
            decoded += c;
        }
    }
    return decoded;
}

static String queryParamValue(const String &url, const String &param) {
    String marker = param + "=";
    int start = url.indexOf("?" + marker);
    if (start < 0) {
        start = url.indexOf("&" + marker);
    }
    if (start < 0) {
        return "";
    }

    start += marker.length() + 1;
    int end = url.indexOf('&', start);
    return end >= 0 ? url.substring(start, end) : url.substring(start);
}

static String normalizeCalendarUrl(String url) {
    url.trim();
    if (url.startsWith("webcal://")) {
        url = "https://" + url.substring(9);
    }

    if (url.indexOf("calendar.google.com/calendar/embed") >= 0) {
        String src = queryParamValue(url, "src");
        if (src.length() > 0) {
            return "https://calendar.google.com/calendar/ical/" + urlEncode(urlDecode(src)) + "/public/basic.ics";
        }
    }

    if (url.indexOf("calendar.google.com/calendar/") >= 0) {
        String cid = queryParamValue(url, "cid");
        if (cid.length() > 0) {
            return "https://calendar.google.com/calendar/ical/" + urlEncode(urlDecode(cid)) + "/public/basic.ics";
        }
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

static long daysFromCivil(int year, unsigned month, unsigned day) {
    year -= month <= 2;
    const int era = (year >= 0 ? year : year - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(year - era * 400);
    const unsigned doy = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097L + static_cast<long>(doe) - 719468L;
}

static void civilFromDays(long z, int &year, unsigned &month, unsigned &day) {
    z += 719468L;
    const long era = (z >= 0 ? z : z - 146096L) / 146097L;
    const unsigned doe = static_cast<unsigned>(z - era * 146097L);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    year = static_cast<int>(yoe) + era * 400;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    day = doy - (153 * mp + 2) / 5 + 1;
    month = mp + (mp < 10 ? 3 : -9);
    year += month <= 2;
}

static String twoDigits(int value) {
    return value < 10 ? "0" + String(value) : String(value);
}

static bool parseIcsDateTime(String value, String &localDate, String &localTime, bool &dateOnly) {
    value.trim();
    if (value.length() < 8) {
        return false;
    }

    dateOnly = value.indexOf('T') < 0;
    int year = value.substring(0, 4).toInt();
    unsigned month = value.substring(4, 6).toInt();
    unsigned day = value.substring(6, 8).toInt();

    if (year < 1970 || month < 1 || month > 12 || day < 1 || day > 31) {
        return false;
    }

    localDate = value.substring(0, 8);
    localTime = "";

    if (dateOnly) {
        return true;
    }

    int timePos = value.indexOf('T');
    if (timePos < 0 || timePos + 4 >= value.length()) {
        return false;
    }

    int hour = value.substring(timePos + 1, timePos + 3).toInt();
    int minute = value.substring(timePos + 3, timePos + 5).toInt();
    int second = (timePos + 6 < value.length()) ? value.substring(timePos + 5, timePos + 7).toInt() : 0;

    if (value.endsWith("Z")) {
        long dayNumber = daysFromCivil(year, month, day);
        long secondsOfDay = hour * 3600L + minute * 60L + second + currentWeather.utcOffsetSeconds;
        while (secondsOfDay < 0) {
            secondsOfDay += 86400L;
            dayNumber--;
        }
        while (secondsOfDay >= 86400L) {
            secondsOfDay -= 86400L;
            dayNumber++;
        }

        int localYear;
        unsigned localMonth;
        unsigned localDay;
        civilFromDays(dayNumber, localYear, localMonth, localDay);
        hour = secondsOfDay / 3600L;
        minute = (secondsOfDay % 3600L) / 60L;
        localDate = String(localYear) + twoDigits(localMonth) + twoDigits(localDay);
    }

    localTime = twoDigits(hour) + ":" + twoDigits(minute);
    return true;
}

static String eventTimeLabel(const String &dtstart) {
    String localDate;
    String localTime;
    bool dateOnly = false;
    if (!parseIcsDateTime(dtstart, localDate, localTime, dateOnly) || dateOnly) {
        return "All day";
    }

    return localTime;
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

static bool eventIncludesToday(const String &dtstart, const String &dtend, const String &rrule, const String &today) {
    String startDate;
    String startTime;
    bool startDateOnly = false;
    if (!parseIcsDateTime(dtstart, startDate, startTime, startDateOnly)) {
        return false;
    }

    if (rruleIncludesToday(rrule, startDate, today)) {
        return true;
    }

    if (dtend.length() == 0) {
        return startDate == today;
    }

    String endDate;
    String endTime;
    bool endDateOnly = false;
    if (!parseIcsDateTime(dtend, endDate, endTime, endDateOnly)) {
        return startDate == today;
    }

    int todayDay = dateToEpochDay(today);
    int startDay = dateToEpochDay(startDate);
    int endDay = dateToEpochDay(endDate);
    if (todayDay < 0 || startDay < 0 || endDay < 0) {
        return startDate == today;
    }

    if (endDateOnly) {
        return todayDay >= startDay && todayDay < endDay;
    }

    return todayDay >= startDay && todayDay <= endDay;
}

static void clearCalendarEvents() {
    calendarFetchOk = true;
    calendarStatusMessage = "";
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
        calendarStatusMessage = "No ICS URL";
        return true;
    }

    HTTPClient http;
    WiFiClientSecure secureClient;
    WiFiClient plainClient;
    bool isHttps = url.startsWith("https://");

    if (isHttps) {
        secureClient.setInsecure();
        if (!http.begin(secureClient, url)) {
            Serial.println("Calendar HTTPS begin failed");
            calendarFetchOk = false;
            calendarStatusMessage = "HTTPS begin failed";
            return false;
        }
    } else if (!http.begin(plainClient, url)) {
        Serial.println("Calendar HTTP begin failed");
        calendarFetchOk = false;
        calendarStatusMessage = "HTTP begin failed";
        return false;
    }

    http.setTimeout(HTTP_TIMEOUT_MS);
    http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
    http.setUserAgent(APP_NAME);

    int httpCode = http.GET();
    if (httpCode != HTTP_CODE_OK) {
        String errorText = http.errorToString(httpCode);
        Serial.printf("Calendar HTTP error code: %d (%s)\n", httpCode, errorText.c_str());
        http.end();
        calendarFetchOk = false;
        calendarStatusMessage = httpCode == HTTP_CODE_NOT_FOUND ? "HTTP 404 Check ICS URL" : "HTTP " + String(httpCode);
        if (errorText.length() > 0) {
            calendarStatusMessage += " " + errorText;
        }
        return false;
    }

    String payload = http.getString();
    http.end();
    Serial.printf("Calendar ICS payload bytes: %d\n", payload.length());
    if (payload.length() == 0) {
        Serial.println("Calendar ICS payload is empty");
        calendarFetchOk = false;
        calendarStatusMessage = "Empty ICS";
        return false;
    }

    String today = todayString();
    if (today.length() == 0) {
        Serial.println("Calendar skipped: local date unavailable");
        calendarFetchOk = false;
        calendarStatusMessage = "No local date";
        return false;
    }

    bool inEvent = false;
    String dtstart = "";
    String dtend = "";
    String summary = "";
    String rrule = "";

    int pos = 0;
    while (pos < payload.length() && calendarEventCount < MAX_CALENDAR_EVENTS) {
        String line = unfoldLine(payload, pos);

        if (line == "BEGIN:VEVENT") {
            inEvent = true;
            dtstart = "";
            dtend = "";
            summary = "";
            rrule = "";
            continue;
        }

        if (line == "END:VEVENT") {
            bool isToday = eventIncludesToday(dtstart, dtend, rrule, today);
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
        } else if (line.startsWith("DTEND")) {
            dtend = valueAfterColon(line);
        } else if (line.startsWith("SUMMARY")) {
            summary = valueAfterColon(line);
        } else if (line.startsWith("RRULE")) {
            rrule = valueAfterColon(line);
        }
    }

    Serial.printf("Calendar events today: %d\n", calendarEventCount);
    calendarStatusMessage = calendarEventCount == 0 ? "No events today" : "OK";
    return true;
}
