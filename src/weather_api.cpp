#include "weather_api.h"
#include "constants.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

extern bool useCelsius;
extern bool useChineseDisplay;
extern WeatherData currentWeather;

static int findHourlyIndex(JsonArray hourlyTime, const String &currentTime) {
    if (currentTime.length() < 13) {
        return 0;
    }

    String currentHour = currentTime.substring(0, 13);
    for (int i = 0; i < hourlyTime.size(); i++) {
        String hourValue = hourlyTime[i].as<String>();
        if (hourValue.startsWith(currentHour)) {
            return i;
        }
    }

    return 0;
}

bool fetchWeatherData(float latitude, float longitude) {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("WiFi not connected");
        return false;
    }

    HTTPClient http;
    String url = "https://api.open-meteo.com/v1/forecast?";
    url += "latitude=" + String(latitude, 4);
    url += "&longitude=" + String(longitude, 4);
    url += "&current=temperature_2m,apparent_temperature,relative_humidity_2m,precipitation,wind_speed_10m,wind_direction_10m,weather_code";
    url += "&hourly=temperature_2m,precipitation_probability,relative_humidity_2m,pressure_msl,uv_index,weather_code";
    url += "&daily=weather_code,temperature_2m_max,temperature_2m_min,precipitation_probability_max,relative_humidity_2m_mean,pressure_msl_mean,sunrise,sunset";
    url += useCelsius ? "&temperature_unit=celsius&wind_speed_unit=kmh&precipitation_unit=mm" :
                        "&temperature_unit=fahrenheit&wind_speed_unit=mph&precipitation_unit=inch";
    url += "&timezone=auto&forecast_days=7";

    for (int retry = 0; retry < HTTP_RETRY_ATTEMPTS; retry++) {
        if (retry > 0) {
            Serial.printf("Retry attempt %d/%d...\n", retry + 1, HTTP_RETRY_ATTEMPTS);
            delay(HTTP_RETRY_DELAY_MS);
        }

        http.begin(url);
        http.setTimeout(HTTP_TIMEOUT_MS);

        int httpCode = http.GET();

        if (httpCode == HTTP_CODE_OK) {
            String payload = http.getString();

            // Parse JSON with properly sized document
            JsonDocument doc;
            DeserializationError error = deserializeJson(doc, payload);

            if (!error) {
                // Extract current conditions
                currentWeather.temperature = doc["current"]["temperature_2m"];
                currentWeather.apparentTemperature = doc["current"]["apparent_temperature"];
                currentWeather.humidity = doc["current"]["relative_humidity_2m"];
                currentWeather.windSpeed = doc["current"]["wind_speed_10m"];
                currentWeather.windDir = doc["current"]["wind_direction_10m"];
                currentWeather.precipitation = doc["current"]["precipitation"];
                currentWeather.weatherCode = doc["current"]["weather_code"];
                currentWeather.utcOffsetSeconds = doc["utc_offset_seconds"].is<int>() ?
                                                  doc["utc_offset_seconds"].as<int>() :
                                                  (TIMEZONE_OFFSET_HOURS * 3600);
                String currentTime = doc["current"]["time"].as<String>();

                Serial.println("\n=== Current Conditions from API ===");
                Serial.printf("Temperature: %.1f\n", currentWeather.temperature);
                Serial.printf("Feels Like: %.1f\n", currentWeather.apparentTemperature);
                Serial.printf("Humidity: %.0f%%\n", currentWeather.humidity);
                Serial.printf("Wind: %.1f @ %.0f°\n", currentWeather.windSpeed, currentWeather.windDir);
                Serial.printf("Weather Code: %d\n", currentWeather.weatherCode);
                Serial.printf("UTC offset seconds: %d\n", currentWeather.utcOffsetSeconds);

                // Extract sunrise/sunset
                String localDateStr = doc["daily"]["time"][0].as<String>();
                if (localDateStr.length() >= 10) {
                    currentWeather.localDateYmd = localDateStr.substring(0, 4) +
                                                  localDateStr.substring(5, 7) +
                                                  localDateStr.substring(8, 10);
                }

                String sunriseStr = doc["daily"]["sunrise"][0].as<String>();
                String sunsetStr = doc["daily"]["sunset"][0].as<String>();
                if (sunriseStr.length() > 10) {
                    currentWeather.sunriseTime = sunriseStr.substring(11, 16);
                }
                if (sunsetStr.length() > 10) {
                    currentWeather.sunsetTime = sunsetStr.substring(11, 16);
                }

                // Extract hourly data
                JsonArray hourlyTime = doc["hourly"]["time"];
                JsonArray hourlyTemp = doc["hourly"]["temperature_2m"];
                JsonArray hourlyPrecip = doc["hourly"]["precipitation_probability"];
                JsonArray hourlyHumidity = doc["hourly"]["relative_humidity_2m"];
                JsonArray hourlyPressure = doc["hourly"]["pressure_msl"];
                JsonArray hourlyUV = doc["hourly"]["uv_index"];
                JsonArray hourlyWeatherCode = doc["hourly"]["weather_code"];

                int currentHourIndex = findHourlyIndex(hourlyTime, currentTime);
                int forecastStartIndex = currentHourIndex + 2;

                Serial.printf("Current API time: %s, index: %d, forecast starts at index: %d\n",
                              currentTime.c_str(), currentHourIndex, forecastStartIndex);

                for (int i = 0; i < MAX_HOURLY; i++) {
                    currentWeather.hourly[i].timeLabel = "--:--";
                    currentWeather.hourly[i].temp = currentWeather.temperature;
                    currentWeather.hourly[i].precip = 0;
                    currentWeather.hourly[i].humidity = currentWeather.humidity;
                    currentWeather.hourly[i].pressure = 0;
                    currentWeather.hourly[i].uvIndex = 0;
                    currentWeather.hourly[i].weatherCode = currentWeather.weatherCode;
                }

                for (int i = 0; i < MAX_HOURLY && (forecastStartIndex + i) < hourlyTemp.size(); i++) {
                    int apiIndex = forecastStartIndex + i;
                    String hourValue = hourlyTime[apiIndex].as<String>();
                    currentWeather.hourly[i].timeLabel = hourValue.length() >= 16 ? hourValue.substring(11, 16) : "--:--";
                    currentWeather.hourly[i].temp = hourlyTemp[apiIndex];
                    currentWeather.hourly[i].precip = hourlyPrecip[apiIndex];
                    currentWeather.hourly[i].humidity = hourlyHumidity[apiIndex];
                    currentWeather.hourly[i].pressure = hourlyPressure[apiIndex];
                    currentWeather.hourly[i].uvIndex = (apiIndex < hourlyUV.size()) ? hourlyUV[apiIndex].as<float>() : 0.0f;
                    currentWeather.hourly[i].weatherCode = hourlyWeatherCode[apiIndex];
                    Serial.printf("Hour %d (%s, API index %d) UV: %.1f\n",
                                  i, currentWeather.hourly[i].timeLabel.c_str(), apiIndex, currentWeather.hourly[i].uvIndex);
                }

                // Extract daily forecast
                JsonArray dailyMax = doc["daily"]["temperature_2m_max"];
                JsonArray dailyMin = doc["daily"]["temperature_2m_min"];
                JsonArray dailyRain = doc["daily"]["precipitation_probability_max"];
                JsonArray dailyHumid = doc["daily"]["relative_humidity_2m_mean"];
                JsonArray dailyPressure = doc["daily"]["pressure_msl_mean"];
                JsonArray dailyWeatherCode = doc["daily"]["weather_code"];

                if (dailyMax.size() > 0 && dailyMin.size() > 0) {
                    currentWeather.todayMinTemp = dailyMin[0];
                    currentWeather.todayMaxTemp = dailyMax[0];
                }

                for (int i = 0; i < MAX_FORECAST && i < dailyMax.size(); i++) {
                    currentWeather.forecastMaxTemp[i] = dailyMax[i];
                    currentWeather.forecastMinTemp[i] = dailyMin[i];
                    currentWeather.forecastRain[i] = dailyRain[i];
                    currentWeather.forecastHumidity[i] = dailyHumid[i];
                    currentWeather.forecastWeatherCode[i] = (i < dailyWeatherCode.size()) ? dailyWeatherCode[i].as<int>() : currentWeather.weatherCode;
                    if (i < dailyPressure.size()) {
                        currentWeather.forecastPressure[i] = dailyPressure[i];
                    }
                }

                http.end();
                Serial.println("Weather fetch successful!");
                return true;
            } else {
                Serial.printf("JSON parsing error: %s\n", error.c_str());
            }
        } else {
            Serial.printf("HTTP error code: %d\n", httpCode);
        }

        http.end();

        // If this was the last retry, break
        if (retry == HTTP_RETRY_ATTEMPTS - 1) {
            Serial.println("All retry attempts failed");
        }
    }

    return false;
}

bool geocodeCity(String cityName, float &latitude, float &longitude, String *resolvedName) {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("WiFi not connected");
        return false;
    }

    HTTPClient http;
    String url = "https://geocoding-api.open-meteo.com/v1/search?name=";
    url += urlEncode(cityName);
    url += "&count=1&language=";
    url += useChineseDisplay ? "zh" : "en";
    url += "&format=json";

    Serial.println("Geocoding city: " + cityName);

    for (int retry = 0; retry < HTTP_RETRY_ATTEMPTS; retry++) {
        if (retry > 0) {
            Serial.printf("Geocode retry %d/%d...\n", retry + 1, HTTP_RETRY_ATTEMPTS);
            delay(HTTP_RETRY_DELAY_MS);
        }

        http.begin(url);
        http.setTimeout(HTTP_TIMEOUT_MS);

        int httpCode = http.GET();

        if (httpCode == HTTP_CODE_OK) {
            String payload = http.getString();

            // Parse JSON
            JsonDocument doc;
            DeserializationError error = deserializeJson(doc, payload);

            if (!error && doc["results"].size() > 0) {
                latitude = doc["results"][0]["latitude"];
                longitude = doc["results"][0]["longitude"];
                String foundCity = doc["results"][0]["name"].as<String>();
                String country = doc["results"][0]["country"].as<String>();
                if (resolvedName != nullptr && foundCity.length() > 0) {
                    *resolvedName = foundCity;
                }

                Serial.printf("Found: %s, %s at %.4f, %.4f\n",
                             foundCity.c_str(), country.c_str(), latitude, longitude);

                http.end();
                return true;
            } else if (error) {
                Serial.printf("JSON parsing error: %s\n", error.c_str());
            } else {
                Serial.println("No results found for city");
            }
        } else {
            Serial.printf("HTTP error code: %d\n", httpCode);
        }

        http.end();
    }

    Serial.println("Geocoding failed after all retries");
    return false;
}
