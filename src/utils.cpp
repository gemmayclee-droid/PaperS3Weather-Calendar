#include "utils.h"
#include "constants.h"
#include "Icons.h"
#include <Preferences.h>
#include <WiFi.h>
#include <sys/time.h>
#include <M5UnitENV.h>

extern Preferences preferences;
extern bool nightModeSleep;
extern bool useCelsius;
extern int displayFace;
extern WeatherData currentWeather;

static SHT3X sht3x;
static bool shtReady = false;

void initOnboardSensors() {
    shtReady = sht3x.begin(&Wire, SHT3X_I2C_ADDR, 21, 22, 400000U);
    if (shtReady) {
        Serial.println("SHT30 initialized");
    } else {
        Serial.println("SHT30 not found - using weather API values for local temp/humidity");
    }
}

void setupTime() {
    // Check if RTC already has a valid date (year > 2023 means it was set previously)
    auto dt = M5.Rtc.getDateTime();
    if (dt.date.year > 2023) {
        // RTC holds wall-clock local time. Load it with offset 0 so we do not
        // add a leftover city/Auckland offset on top until applyWeatherTimezone().
        setenv("TZ", "UTC0", 1);
        tzset();
        configTime(0, 0, NTP_SERVER_1, NTP_SERVER_2);

        struct tm tm = {};
        tm.tm_sec = dt.time.seconds;
        tm.tm_min = dt.time.minutes;
        tm.tm_hour = dt.time.hours;
        tm.tm_mday = dt.date.date;
        tm.tm_mon = dt.date.month - 1;
        tm.tm_year = dt.date.year - 1900;
        time_t t = mktime(&tm);
        struct timeval now = { .tv_sec = t };
        settimeofday(&now, NULL);
        Serial.printf("Time set from RTC: %04d-%02d-%02d %02d:%02d:%02d\n",
                      dt.date.year, dt.date.month, dt.date.date,
                      dt.time.hours, dt.time.minutes, dt.time.seconds);
    } else if (WiFi.status() == WL_CONNECTED) {
        // RTC not set yet - temporary default until weather city offset is applied
        configTime(TIMEZONE_OFFSET_HOURS * 3600, 0, NTP_SERVER_1, NTP_SERVER_2);
        struct tm tm;
        if (getLocalTime(&tm)) {
            M5.Rtc.setDateTime(tm);
            Serial.println("Time configured via NTP and saved to RTC");
        } else {
            Serial.println("NTP time sync failed");
        }
    } else {
        Serial.println("No valid RTC time and no WiFi - time unavailable");
    }
}

void applyWeatherTimezone() {
    // Open-Meteo utc_offset_seconds already includes DST for the selected city.
    if (WiFi.status() != WL_CONNECTED) {
        return;
    }

    configTime(currentWeather.utcOffsetSeconds, 0, NTP_SERVER_1, NTP_SERVER_2);

    // Invalidate system time so getLocalTime waits for a fresh NTP UTC base.
    // Otherwise a wall-clock value loaded from RTC is treated as UTC and the
    // city offset is applied twice (typically +1h or +2h too fast).
    struct timeval tv = { .tv_sec = 0, .tv_usec = 0 };
    settimeofday(&tv, nullptr);

    struct tm tm;
    if (getLocalTime(&tm, 15000)) {
        M5.Rtc.setDateTime(tm);
        preferences.begin("weather", false);
        preferences.putInt("utc_offset", currentWeather.utcOffsetSeconds);
        preferences.end();
        Serial.printf("Weather timezone applied: UTC%+ld sec (%02d:%02d local)\n",
                      (long)currentWeather.utcOffsetSeconds, tm.tm_hour, tm.tm_min);
    } else {
        Serial.println("Weather timezone NTP sync failed");
    }
}

void applyStoredTimezone() {
    preferences.begin("weather", true);
    int offset = preferences.getInt("utc_offset", TIMEZONE_OFFSET_HOURS * 3600);
    preferences.end();
    currentWeather.utcOffsetSeconds = offset;
    // RTC already stores city-local wall clock from the last successful sync.
    setenv("TZ", "UTC0", 1);
    tzset();
    configTime(0, 0, NTP_SERVER_1, NTP_SERVER_2);
}

float convertTemp(float tempCelsius) {
    if (useCelsius) {
        return tempCelsius;
    }
    return tempCelsius * 9.0f / 5.0f + 32.0f;
}

String formatTemp(float temp) {
    return String((int)temp) + (useCelsius ? "C" : "F");
}

const uint8_t* getWeatherIcon(int weatherCode, bool isDaytime) {
    if (weatherCode == 0) {
        return isDaytime ? image_data_01d : image_data_01n;
    }
    if (weatherCode <= 3) {
        return isDaytime ? image_data_02d : image_data_02n;
    }
    if (weatherCode >= 45 && weatherCode <= 48) {
        return isDaytime ? image_data_50d : image_data_50n;
    }
    if (weatherCode >= 51 && weatherCode <= 67) {
        return isDaytime ? image_data_10d : image_data_10n;
    }
    if (weatherCode >= 71 && weatherCode <= 86) {
        return isDaytime ? image_data_13d : image_data_13n;
    }
    if (weatherCode >= 95) {
        return isDaytime ? image_data_11d : image_data_11n;
    }
    return image_data_unknown;
}

String getWeatherConditionText(int weatherCode) {
    if (weatherCode == 0) return "Clear";
    if (weatherCode == 1) return "Mainly Clear";
    if (weatherCode == 2) return "Partly Cloudy";
    if (weatherCode == 3) return "Overcast";
    if (weatherCode >= 45 && weatherCode <= 48) return "Foggy";
    if (weatherCode >= 51 && weatherCode <= 55) return "Drizzle";
    if (weatherCode >= 56 && weatherCode <= 57) return "Freezing Drizzle";
    if (weatherCode >= 61 && weatherCode <= 65) return "Rain";
    if (weatherCode >= 66 && weatherCode <= 67) return "Freezing Rain";
    if (weatherCode >= 71 && weatherCode <= 75) return "Snow";
    if (weatherCode >= 77 && weatherCode <= 77) return "Snow Grains";
    if (weatherCode >= 80 && weatherCode <= 82) return "Rain Showers";
    if (weatherCode >= 85 && weatherCode <= 86) return "Snow Showers";
    if (weatherCode >= 95 && weatherCode <= 96) return "Thunderstorm";
    if (weatherCode >= 99) return "Thunderstorm Hail";
    return "Unknown";
}

bool isDaytime(int hour) {
    int sunriseHour = DEFAULT_SUNRISE_HOUR;
    int sunsetHour = DEFAULT_SUNSET_HOUR;

    if (currentWeather.sunriseTime.length() >= 2) {
        sunriseHour = currentWeather.sunriseTime.substring(0, 2).toInt();
    }
    if (currentWeather.sunsetTime.length() >= 2) {
        sunsetHour = currentWeather.sunsetTime.substring(0, 2).toInt();
    }
    return (hour >= sunriseHour && hour < sunsetHour);
}

float getMoonPhase() {
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) {
        return 0.0;
    }

    int year = timeinfo.tm_year + 1900;
    int month = timeinfo.tm_mon + 1;
    int day = timeinfo.tm_mday;

    if (month <= 2) {
        year -= 1;
        month += 12;
    }

    int a = year / 100;
    int b = a / 4;
    int c = 2 - a + b;
    double e = floor(365.25 * (year + 4716));
    double f = floor(30.6001 * (month + 1));
    double jd = c + day + e + f - 1524.5;

    double daysSinceNew = jd - JULIAN_REF_DATE;
    double newMoons = daysSinceNew / LUNAR_CYCLE_DAYS;
    double phase = newMoons - floor(newMoons);

    return phase;
}

bool isNightTime() {
    if (!nightModeSleep) return false;

    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) return false;

    int currentHour = timeinfo.tm_hour;

    preferences.begin("weather", true);
    int nightStart = preferences.getInt("night_start", NIGHT_START_HOUR);
    int nightEnd = preferences.getInt("night_end", NIGHT_END_HOUR);
    preferences.end();

    if (nightStart > nightEnd) {
        return (currentHour >= nightStart || currentHour < nightEnd);
    } else {
        return (currentHour >= nightStart && currentHour < nightEnd);
    }
}

unsigned long getRefreshInterval() {
    int minutes = getFaceRefreshMinutes(displayFace, isNightTime());
    return (unsigned long)minutes * 60000UL;
}

static const int REFRESH_MINUTE_OPTIONS[] = {1, 5, 10, 15, 30, 60, 120, 240, 480};
static const int REFRESH_MINUTE_OPTION_COUNT =
    (int)(sizeof(REFRESH_MINUTE_OPTIONS) / sizeof(REFRESH_MINUTE_OPTIONS[0]));

int normalizeRefreshMinutes(int value, int defaultMinutes) {
    for (int i = 0; i < REFRESH_MINUTE_OPTION_COUNT; i++) {
        if (REFRESH_MINUTE_OPTIONS[i] == value) {
            return value;
        }
    }
    for (int i = 0; i < REFRESH_MINUTE_OPTION_COUNT; i++) {
        if (REFRESH_MINUTE_OPTIONS[i] == defaultMinutes) {
            return defaultMinutes;
        }
    }
    return DEFAULT_FACE0_DAY_MIN;
}

int getFaceRefreshMinutes(int face, bool night) {
    preferences.begin("weather", true);
    int minutes = -1;
    if (face == 1) {
        if (night) {
            minutes = preferences.getInt("face1_night", DEFAULT_FACE1_NIGHT_MIN);
            minutes = normalizeRefreshMinutes(minutes, DEFAULT_FACE1_NIGHT_MIN);
        } else {
            minutes = preferences.getInt("face1_day", DEFAULT_FACE1_DAY_MIN);
            minutes = normalizeRefreshMinutes(minutes, DEFAULT_FACE1_DAY_MIN);
        }
    } else if (night) {
        minutes = preferences.getInt("face0_night", -1);
        if (minutes < 0) {
            minutes = preferences.getInt("night_interval", DEFAULT_FACE0_NIGHT_MIN);
        }
        minutes = normalizeRefreshMinutes(minutes, DEFAULT_FACE0_NIGHT_MIN);
    } else {
        minutes = preferences.getInt("face0_day", -1);
        if (minutes < 0) {
            minutes = preferences.getInt("day_interval", DEFAULT_FACE0_DAY_MIN);
        }
        minutes = normalizeRefreshMinutes(minutes, DEFAULT_FACE0_DAY_MIN);
    }
    preferences.end();
    return minutes;
}

String weatherDataSignature() {
    char buf[96];
    snprintf(buf, sizeof(buf), "%d|%.1f|%.1f|%d|%.1f|%.1f|%s",
             currentWeather.weatherCode,
             currentWeather.temperature,
             currentWeather.apparentTemperature,
             (int)currentWeather.humidity,
             currentWeather.todayMinTemp,
             currentWeather.todayMaxTemp,
             currentWeather.localDateYmd.c_str());
    return String(buf);
}

void storeWeatherFetchState() {
    time_t now = time(nullptr);
    preferences.begin("weather", false);
    preferences.putString("wx_sig", weatherDataSignature());
    preferences.putString("wx_date", currentWeather.localDateYmd);
    if (now > 0) {
        preferences.putULong("wx_epoch", (unsigned long)now);
    }
    preferences.end();
}

bool weatherDataChangedSinceLastStore() {
    preferences.begin("weather", true);
    String prev = preferences.getString("wx_sig", "");
    preferences.end();
    String cur = weatherDataSignature();
    return prev.length() == 0 || prev != cur;
}

bool isWeatherFetchDue() {
    int intervalMin = getFaceRefreshMinutes(0, isNightTime());
    preferences.begin("weather", true);
    unsigned long lastEpoch = preferences.getULong("wx_epoch", 0);
    preferences.end();

    time_t now = time(nullptr);
    if (now <= 0 || lastEpoch == 0) {
        return true;
    }
    return ((unsigned long)now - lastEpoch) >= (unsigned long)intervalMin * 60UL;
}

float readInternalTemperature() {
    if (!shtReady) {
        return SENSOR_ERROR_VALUE;
    }
    sht3x.update();
    return convertTemp(sht3x.cTemp);
}

float readInternalHumidity() {
    if (!shtReady) {
        return SENSOR_ERROR_VALUE;
    }
    sht3x.update();
    return sht3x.humidity;
}

String urlEncode(String str) {
    String encoded = "";
    char c;
    for (int i = 0; i < str.length(); i++) {
        c = str.charAt(i);
        if (c == ' ') {
            encoded += '+';
        } else if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            encoded += c;
        } else {
            encoded += '%';
            char hex[3];
            sprintf(hex, "%02X", c);
            encoded += hex;
        }
    }
    return encoded;
}

void drawDegreeSymbol(int x, int y, int radius) {
    for (int i = 0; i < 2; i++) {
        canvas.drawCircle(x, y, radius - i, TFT_BLACK);
    }
}

int getRSSIQuality(int rssi) {
    int quality = RSSI_QUALITY_MULTIPLIER * (rssi + RSSI_QUALITY_OFFSET);
    if (quality > 100) quality = 100;
    if (quality < 0) quality = 0;
    return quality;
}

bool resolveCalendarDate(int &year, int &month, int &day) {
    if (currentWeather.localDateYmd.length() == 8) {
        year = currentWeather.localDateYmd.substring(0, 4).toInt();
        month = currentWeather.localDateYmd.substring(4, 6).toInt();
        day = currentWeather.localDateYmd.substring(6, 8).toInt();
        if (year >= 2000 && month >= 1 && month <= 12 && day >= 1 && day <= 31) {
            return true;
        }
    }

    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) {
        return false;
    }
    year = timeinfo.tm_year + 1900;
    month = timeinfo.tm_mon + 1;
    day = timeinfo.tm_mday;
    return true;
}

int daysInMonth(int year, int month) {
    static const int DAYS[] = {0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month < 1 || month > 12) {
        return 30;
    }
    if (month == 2) {
        bool leap = (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
        return leap ? 29 : 28;
    }
    return DAYS[month];
}

int weekdaySundayZero(int year, int month, int day) {
    struct tm tm = {};
    tm.tm_year = year - 1900;
    tm.tm_mon = month - 1;
    tm.tm_mday = day;
    tm.tm_hour = 12;
    mktime(&tm);
    return tm.tm_wday;  // Sunday = 0
}

const char* monthNameEnglish(int month) {
    static const char* NAMES[] = {
        "", "January", "February", "March", "April", "May", "June",
        "July", "August", "September", "October", "November", "December"
    };
    if (month < 1 || month > 12) {
        return "";
    }
    return NAMES[month];
}
