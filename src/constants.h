#ifndef CONSTANTS_H
#define CONSTANTS_H

// App identity
#define APP_NAME "PaperWeather-Calendar"
#define VERSION "Version 1.1"

// WiFi Configuration
#define WIFI_TIMEOUT_MS 20000
#define WIFI_RETRY_ATTEMPTS 3
#define WIFI_RETRY_DELAY_MS 2000

// API Configuration
#define HTTP_TIMEOUT_MS 10000
#define HTTP_RETRY_ATTEMPTS 3
#define HTTP_RETRY_DELAY_MS 2000

// Refresh Intervals (minutes) — per-face defaults
#define DEFAULT_FACE0_DAY_MIN 10
#define DEFAULT_FACE0_NIGHT_MIN 480
#define DEFAULT_FACE1_DAY_MIN 1
#define DEFAULT_FACE1_NIGHT_MIN 15
#define REFRESH_INTERVAL_DAY_MS (DEFAULT_FACE0_DAY_MIN * 60000UL)
#define REFRESH_INTERVAL_NIGHT_MS (DEFAULT_FACE0_NIGHT_MIN * 60000UL)
#define NIGHT_START_HOUR 22
#define NIGHT_END_HOUR 5
#define CLOCK_PARTIAL_FULL_EVERY 30  // full Face 1 redraw every N minute ticks (ghosting)
#define CLOCK_WEATHER_FETCH_MS (60UL * 60UL * 1000UL)  // hourly weather while clock stays awake
#define CLOCK_LOOP_POLL_MS 200

// User Interaction
#define USER_INTERACTION_TIMEOUT_MS 30000   // 30 seconds to tap screen before sleep
#define CFG_BUTTON_TOUCH_WIDTH 100          // Width of touchable CFG area
#define CFG_BUTTON_TOUCH_HEIGHT 40          // Height of touchable CFG area

// Weather Data Limits
#define MAX_HOURLY 8
#define MAX_FORECAST 7

// Screen Dimensions
#define SCREEN_WIDTH 960
#define SCREEN_HEIGHT 540

// Display Layout Constants
#define HEADER_HEIGHT 34
#define PANEL_BORDER 14
#define PANEL_SPACING 15
#define PANEL_TITLE_HEIGHT 35

// Month calendar grid (bottom-right panel content area)
#define MONTH_CAL_WEEKDAY_HEADER_H 18
#define MONTH_CAL_WEEK_ROWS 6
#define MONTH_CAL_WEEK_COLS 7
#define DEFAULT_CALENDAR_MODE "month"
#define DEFAULT_DISPLAY_FACE 0

// Face 1 (clock / calendar) layout
#define FACE1_CONTENT_TOP HEADER_HEIGHT
#define FACE1_MARGIN 14
#define FACE1_GAP 12
#define FACE1_LEFT_W 440
#define FACE1_RIGHT_W (SCREEN_WIDTH - FACE1_MARGIN * 2 - FACE1_LEFT_W - FACE1_GAP)
#define FACE1_TOP_H 180
#define FACE1_BOTTOM_H (SCREEN_HEIGHT - FACE1_CONTENT_TOP - FACE1_MARGIN - FACE1_TOP_H - FACE1_GAP)
#define FACE1_WEATHER_ICON_SIZE (WEATHER_ICON_SIZE * 4)  // 256px

// Current Conditions Panel
#define CURRENT_PANEL_SPACING 54
#define MAIN_TEMP_Y_OFFSET 50
#define TEMP_DEGREE_RADIUS_LARGE 8
#define TEMP_DEGREE_OFFSET 42
#define WEATHER_ICON_SIZE 64
#define CONDITION_Y_OFFSET 90
#define FEELS_LIKE_Y_OFFSET 185
#define FEELS_LIKE_DEGREE_RADIUS 5
#define TODAY_TEMP_Y_OFFSET 220
#define TODAY_TEMP_DEGREE_RADIUS 3

// Compass Constants
#define COMPASS_RADIUS 75
#define COMPASS_LABEL_OFFSET 18
#define COMPASS_DIAG_FACTOR 0.707
#define COMPASS_ARROW_SIZE 15
#define COMPASS_ARROW_LENGTH 27

// Graph Constants
#define GRAPH_TITLE_Y_OFFSET 10
#define GRAPH_AREA_Y_OFFSET 35
#define GRAPH_BOTTOM_MARGIN 20
#define GRAPH_SIDE_MARGIN 20
#define GRAPH_TEXT_WIDTH_FACTOR 3.5
#define GRAPH_POINT_RADIUS 2
#define GRAPH_DASH_LENGTH 5
#define GRAPH_DASH_SPACING 10

// Icon Rendering
#define ICON_GRAYSCALE_DIVISOR 4096
#define ICON_COLOR_MULTIPLIER 0x1111

// Battery and Signal
#define BATTERY_WIDTH 40
#define BATTERY_HEIGHT 16
#define BATTERY_TIP_WIDTH 4
#define BATTERY_TIP_HEIGHT 10
#define BATTERY_TIP_OFFSET 3
#define RSSI_QUALITY_MULTIPLIER 2
#define RSSI_QUALITY_OFFSET 100

// Sun/Moon Constants
#define MOON_PHASE_NEW_MIN 0.05
#define MOON_PHASE_NEW_MAX 0.95
#define MOON_PHASE_WAXING_CRES 0.20
#define MOON_PHASE_FIRST_QTR 0.30
#define MOON_PHASE_WAXING_GIB 0.45
#define MOON_PHASE_FULL 0.55
#define MOON_PHASE_WANING_GIB 0.70
#define MOON_PHASE_LAST_QTR 0.80

// Default Coordinates (Auckland)
#define DEFAULT_LATITUDE -36.8485
#define DEFAULT_LONGITUDE 174.7633
#define DEFAULT_CITY "Auckland"

// Special Values
#define SENSOR_ERROR_VALUE -999.0
#define COORD_NOT_SET -9999.0

// Config Portal
#define CONFIG_AP_SSID APP_NAME
#define CONFIG_AP_PASSWORD "configure"
#define CONFIG_AP_IP "192.168.4.1"

// Time Constants
#define JULIAN_REF_DATE 2451550.26
#define LUNAR_CYCLE_DAYS 29.53058867
#define DEFAULT_SUNRISE_HOUR 6
#define DEFAULT_SUNSET_HOUR 18

// NTP Configuration
#define NTP_SERVER_1 "pool.ntp.org"
#define NTP_SERVER_2 "time.nist.gov"
#define TIMEZONE_OFFSET_HOURS 13  // Auckland default

// Color Definitions
#ifndef TFT_WHITE
#define TFT_WHITE 0xFFFF
#endif
#ifndef TFT_BLACK
#define TFT_BLACK 0x0000
#endif

#endif // CONSTANTS_H
