#include "settings_ui.h"
#include "onscreen_keyboard.h"
#include "constants.h"
#include "utils.h"
#include <M5Unified.h>
#include <WiFi.h>
#include <Preferences.h>

extern Preferences preferences;
extern int displayFace;

static const char *SECTION_TITLES[] = {"WiFi", "Location", "Calendar", "Schedule", "Night"};

enum SubScreen {
    SUB_NONE,
    SUB_WIFI_SCAN,
    SUB_WIFI_PASSWORD,
    SUB_KEYBOARD,
    SUB_CONFIRM_WEB
};

struct UiRect {
    int x, y, w, h;
};

struct SettingsUiState {
    WeatherSettings draft;
    int pageIndex;
    SubScreen subScreen;
    int listScroll;
    int scanCount;
    int pendingEncrypt;
    String pendingSsid;
    String editBuffer;
    int editTarget;  // 0=city, 1=lat, 2=lon, 3=wifi password
    bool showPassword;
    unsigned long lastCharRevealMs;
    bool shift;
    bool symbols;
    String statusMsg;
    String wifiStatus;
    unsigned long lastActivityMs;
    int chooseWifiBtnY;
    int advWebBtnY;
    UiRect btnSave;
    UiRect btnBack;
    UiRect btnPrev;
    UiRect btnNext;
    int fieldX;
    int fieldY;
    int fieldW;
    int fieldH;
    KeyboardLayout kbLayout;
};

static bool inRect(int x, int y, const UiRect &r, int pad = 6) {
    return x >= r.x - pad && x < r.x + r.w + pad && y >= r.y - pad && y < r.y + r.h + pad;
}

static void setSettingsFont(int size = SETTINGS_FONT) {
    M5.Display.setTextSize(size);
    M5.Display.setTextColor(TFT_BLACK);
}

static void markActivity(SettingsUiState &st, SettingsActivityCallback cb) {
    st.lastActivityMs = millis();
    if (cb) {
        cb();
    }
}

static String normalizeCalendarMode(String mode) {
    mode.trim();
    mode.toLowerCase();
    if (mode == "google") {
        return "google";
    }
    return DEFAULT_CALENDAR_MODE;
}

static String coordDisplay(const String &stored) {
    if (stored.length() == 0) {
        return "";
    }
    float v = stored.toFloat();
    if (v == COORD_NOT_SET) {
        return "";
    }
    return stored;
}

static void fullRefresh() {
    M5.Display.endWrite();
    M5.Display.display();
}

static void beginFrame() {
    M5.Display.startWrite();
    M5.Display.fillScreen(TFT_WHITE);
}

static void drawOutlinedButton(const UiRect &r, const String &label, bool fill) {
    if (fill) {
        M5.Display.fillRect(r.x, r.y, r.w, r.h, TFT_BLACK);
    } else {
        M5.Display.fillRect(r.x, r.y, r.w, r.h, TFT_WHITE);
        M5.Display.drawRect(r.x, r.y, r.w, r.h, TFT_BLACK);
    }
    M5.Display.setTextDatum(MC_DATUM);
    M5.Display.setTextSize(SETTINGS_FONT);
    M5.Display.setTextColor(fill ? TFT_WHITE : TFT_BLACK);
    M5.Display.drawString(label, r.x + r.w / 2, r.y + r.h / 2);
    M5.Display.setTextDatum(TL_DATUM);
    M5.Display.setTextColor(TFT_BLACK);
}

static void drawChrome(SettingsUiState &st, bool showPager) {
    M5.Display.fillRect(0, 0, SCREEN_WIDTH, SETTINGS_HEADER_H, TFT_WHITE);
    M5.Display.fillRect(0, SETTINGS_CONTENT_BOTTOM, SCREEN_WIDTH, SETTINGS_FOOTER_H, TFT_WHITE);
    M5.Display.drawFastHLine(0, SETTINGS_HEADER_H - 1, SCREEN_WIDTH, TFT_BLACK);
    M5.Display.drawFastHLine(0, SETTINGS_CONTENT_BOTTOM, SCREEN_WIDTH, TFT_BLACK);

    st.btnPrev = {0, 0, SETTINGS_NAV_W, SETTINGS_HEADER_H};
    st.btnNext = {SCREEN_WIDTH - SETTINGS_NAV_W, 0, SETTINGS_NAV_W, SETTINGS_HEADER_H};
    st.btnSave = {SETTINGS_MARGIN, SETTINGS_CONTENT_BOTTOM + 10,
                  (SCREEN_WIDTH - SETTINGS_MARGIN * 3) / 2, SETTINGS_BTN_H};
    st.btnBack = {SETTINGS_MARGIN * 2 + st.btnSave.w, SETTINGS_CONTENT_BOTTOM + 10,
                  (SCREEN_WIDTH - SETTINGS_MARGIN * 3) / 2, SETTINGS_BTN_H};

    if (showPager) {
        drawOutlinedButton({SETTINGS_MARGIN, 10, SETTINGS_NAV_W - SETTINGS_MARGIN, 36}, "< Prev", false);
        drawOutlinedButton({SCREEN_WIDTH - SETTINGS_NAV_W, 10, SETTINGS_NAV_W - SETTINGS_MARGIN, 36},
                             "Next >", false);
    }

    setSettingsFont(SETTINGS_FONT);
    M5.Display.setTextDatum(TC_DATUM);
    String title = String(SECTION_TITLES[st.pageIndex]) + "  " +
                   String(st.pageIndex + 1) + "/" + String(SETTINGS_PAGE_COUNT);
    M5.Display.drawString(title, SCREEN_WIDTH / 2, SETTINGS_HEADER_H / 2 + 2);
    M5.Display.setTextDatum(TL_DATUM);

    drawOutlinedButton(st.btnSave, "Save", true);
    drawOutlinedButton(st.btnBack, "Exit", false);
}

static void drawWrapText(int x, int y, int maxW, const String &text, int lineH) {
    setSettingsFont(SETTINGS_FONT);
    String line = "";
    int cy = y;
    for (unsigned i = 0; i <= text.length(); i++) {
        char c = (i < text.length()) ? text.charAt(i) : '\n';
        if (c == ' ' || c == '\n' || i == text.length()) {
            if (line.length() > 0) {
                M5.Display.drawString(line, x, cy);
                cy += lineH;
                line = "";
            }
            if (c == '\n') {
                continue;
            }
        } else {
            line += c;
            if (M5.Display.textWidth(line) > maxW) {
                M5.Display.drawString(line, x, cy);
                cy += lineH;
                line = "";
            }
        }
    }
}

static bool tryConnectWifi(const String &ssid, const String &password, String &error) {
    WiFi.mode(WIFI_STA);
    WiFi.disconnect(true);
    delay(100);

    for (int attempt = 0; attempt < WIFI_RETRY_ATTEMPTS; attempt++) {
        if (attempt > 0) {
            delay(WIFI_RETRY_DELAY_MS);
        }
        WiFi.begin(ssid.c_str(), password.c_str());
        unsigned long start = millis();
        while (WiFi.status() != WL_CONNECTED && millis() - start < WIFI_TIMEOUT_MS) {
            delay(200);
            M5.update();
        }
        if (WiFi.status() == WL_CONNECTED) {
            return true;
        }
        WiFi.disconnect(true);
    }
    error = "Could not connect. Check password.";
    return false;
}

static String wifiConnectionLabel() {
    if (WiFi.status() == WL_CONNECTED) {
        return "Connected";
    }
    if (WiFi.getMode() == WIFI_STA) {
        return "Not connected";
    }
    return "Not connected";
}

static void drawWifiPage(SettingsUiState &st) {
    int y = SETTINGS_CONTENT_TOP + SETTINGS_MARGIN;
    setSettingsFont(SETTINGS_FONT);
    M5.Display.drawString("Network", SETTINGS_MARGIN, y);
    y += SETTINGS_LINE_H + 4;

    String ssidLabel = st.draft.ssid.length() > 0 ? st.draft.ssid : "(not configured)";
    M5.Display.drawString("SSID: " + ssidLabel, SETTINGS_MARGIN, y);
    y += SETTINGS_LINE_H;
    M5.Display.drawString("Status: " + wifiConnectionLabel(), SETTINGS_MARGIN, y);
    y += SETTINGS_LINE_H;
    if (WiFi.status() == WL_CONNECTED) {
        M5.Display.drawString("Signal: " + String(WiFi.RSSI()) + " dBm", SETTINGS_MARGIN, y);
        y += SETTINGS_LINE_H;
    }
    if (st.wifiStatus.length() > 0) {
        M5.Display.drawString(st.wifiStatus, SETTINGS_MARGIN, y);
        y += SETTINGS_LINE_H;
    }

    UiRect chooseBtn = {SETTINGS_MARGIN, y + 8, SCREEN_WIDTH - SETTINGS_MARGIN * 2, SETTINGS_BTN_H};
    drawOutlinedButton(chooseBtn, "Choose WiFi", false);
    st.chooseWifiBtnY = chooseBtn.y;

    y += SETTINGS_BTN_H + 16;
    UiRect advBtn = {SETTINGS_MARGIN, y, SCREEN_WIDTH - SETTINGS_MARGIN * 2, SETTINGS_BTN_H};
    drawOutlinedButton(advBtn, "Web setup", false);
    st.advWebBtnY = advBtn.y;

    y += SETTINGS_BTN_H + 12;
    M5.Display.drawString("2.4 GHz WiFi only", SETTINGS_MARGIN, y);
}

static void drawLocationPage(SettingsUiState &st) {
    int y = SETTINGS_CONTENT_TOP + SETTINGS_MARGIN;
    int colW = (SCREEN_WIDTH - SETTINGS_MARGIN * 3) / 2;
    int leftX = SETTINGS_MARGIN;
    int rightX = SETTINGS_MARGIN * 2 + colW;
    setSettingsFont(SETTINGS_FONT);

    M5.Display.drawString("City", leftX, y);
    M5.Display.drawString("Latitude", rightX, y);
    y += SETTINGS_LINE_H;

    String cityVal = st.draft.city.length() ? st.draft.city : "(required)";
    String latVal = coordDisplay(st.draft.latitude);
    if (latVal.length() == 0) {
        latVal = "(auto)";
    }
    M5.Display.drawString(cityVal, leftX, y);
    M5.Display.drawString(latVal, rightX, y);
    y += SETTINGS_LINE_H + 4;

    UiRect cityBtn = {leftX, y, colW, SETTINGS_BTN_H};
    UiRect latBtn = {rightX, y, colW, SETTINGS_BTN_H};
    drawOutlinedButton(cityBtn, "Edit City", false);
    drawOutlinedButton(latBtn, "Edit Lat", false);
    y += SETTINGS_BTN_H + 16;

    M5.Display.drawString("Longitude", rightX, y);
    y += SETTINGS_LINE_H;
    String lonVal = coordDisplay(st.draft.longitude);
    if (lonVal.length() == 0) {
        lonVal = "(auto)";
    }
    M5.Display.drawString(lonVal, rightX, y);
    y += SETTINGS_LINE_H + 4;
    UiRect lonBtn = {rightX, y, colW, SETTINGS_BTN_H};
    drawOutlinedButton(lonBtn, "Edit Lon", false);
    y += SETTINGS_BTN_H + 20;

    M5.Display.drawString("Temperature", leftX, y);
    UiRect fBtn = {leftX + 150, y - 4, 90, 40};
    UiRect cBtn = {leftX + 250, y - 4, 90, 40};
    drawOutlinedButton(fBtn, "F", st.draft.tempUnit != "C");
    drawOutlinedButton(cBtn, "C", st.draft.tempUnit == "C");

    y += 48;
    drawWrapText(leftX, y, SCREEN_WIDTH - SETTINGS_MARGIN * 2,
                 "Leave coordinates blank to auto-lookup city on save.", SETTINGS_LINE_H);
}

static void drawCalendarPage(SettingsUiState &st) {
    int y = SETTINGS_CONTENT_TOP + SETTINGS_MARGIN;
    bool isGoogle = (st.draft.calendarMode == "google");
    setSettingsFont(SETTINGS_FONT);
    M5.Display.drawString("Panel mode:", SETTINGS_MARGIN, y);
    y += SETTINGS_LINE_H + 4;
    UiRect monthBtn = {SETTINGS_MARGIN, y, (SCREEN_WIDTH - SETTINGS_MARGIN * 3) / 2, SETTINGS_BTN_H};
    UiRect googleBtn = {SETTINGS_MARGIN * 2 + monthBtn.w, y, monthBtn.w, SETTINGS_BTN_H};
    drawOutlinedButton(monthBtn, "Month", !isGoogle);
    drawOutlinedButton(googleBtn, "Google", isGoogle);
    y += SETTINGS_BTN_H + 16;

    String icsStatus = st.draft.calendarIcs.length() > 0 ? "Configured" : "Not configured";
    M5.Display.drawString("ICS URL: " + icsStatus, SETTINGS_MARGIN, y);
    y += SETTINGS_LINE_H + 4;

    if (isGoogle && st.draft.calendarIcs.length() == 0) {
        M5.Display.drawRect(SETTINGS_MARGIN, y, SCREEN_WIDTH - SETTINGS_MARGIN * 2, 40, TFT_BLACK);
        M5.Display.drawString("Warning: needs web ICS setup", SETTINGS_MARGIN + 8, y + 12);
        y += 48;
    }

    if (isGoogle) {
        drawWrapText(SETTINGS_MARGIN, y, SCREEN_WIDTH - SETTINGS_MARGIN * 2,
                     "ICS URL: use Web setup on WiFi page, or CFG AP at 192.168.4.1",
                     SETTINGS_LINE_H);
    }
}

static int refreshOptionIndex(int minutes) {
    const int opts[] = {1, 5, 10, 15, 30, 60, 120, 240, 480};
    minutes = normalizeRefreshMinutes(minutes, DEFAULT_FACE0_DAY_MIN);
    for (unsigned i = 0; i < sizeof(opts) / sizeof(opts[0]); i++) {
        if (opts[i] == minutes) {
            return (int)i;
        }
    }
    return 2;
}

static int refreshOptionAt(int index) {
    const int opts[] = {1, 5, 10, 15, 30, 60, 120, 240, 480};
    if (index < 0) {
        index = 0;
    }
    if (index >= (int)(sizeof(opts) / sizeof(opts[0]))) {
        index = (int)(sizeof(opts) / sizeof(opts[0])) - 1;
    }
    return opts[index];
}

static int face1RefreshOptionIndex(int minutes) {
    const int opts[] = {15, 30, 60, 120, 240, 480};
    minutes = normalizeFace1RefreshMinutes(minutes, DEFAULT_FACE1_DAY_MIN);
    for (unsigned i = 0; i < sizeof(opts) / sizeof(opts[0]); i++) {
        if (opts[i] == minutes) {
            return (int)i;
        }
    }
    return 1;  // default 30 min
}

static int face1RefreshOptionAt(int index) {
    const int opts[] = {15, 30, 60, 120, 240, 480};
    if (index < 0) {
        index = 0;
    }
    if (index >= (int)(sizeof(opts) / sizeof(opts[0]))) {
        index = (int)(sizeof(opts) / sizeof(opts[0])) - 1;
    }
    return opts[index];
}

static void stepInterval(int &minutes, int delta) {
    int idx = refreshOptionIndex(minutes) + delta;
    minutes = refreshOptionAt(idx);
}

static void stepFace1Interval(int &minutes, int delta) {
    int idx = face1RefreshOptionIndex(minutes) + delta;
    minutes = face1RefreshOptionAt(idx);
}

static int clockRefreshOptionIndex(int minutes) {
    const int opts[] = {1, 5, 10, 15, 30, 60, 120, 240};
    minutes = normalizeFace1ClockRefreshMinutes(minutes, DEFAULT_FACE1_CLOCK_DAY_MIN);
    for (unsigned i = 0; i < sizeof(opts) / sizeof(opts[0]); i++) {
        if (opts[i] == minutes) {
            return (int)i;
        }
    }
    return 0;
}

static int clockRefreshOptionAt(int index) {
    const int opts[] = {1, 5, 10, 15, 30, 60, 120, 240};
    if (index < 0) {
        index = 0;
    }
    if (index >= (int)(sizeof(opts) / sizeof(opts[0]))) {
        index = (int)(sizeof(opts) / sizeof(opts[0])) - 1;
    }
    return opts[index];
}

static void stepFace1ClockInterval(int &minutes, int delta) {
    int idx = clockRefreshOptionIndex(minutes) + delta;
    minutes = clockRefreshOptionAt(idx);
}

static const int SCHEDULE_ROW_COUNT = 6;
static const int SCHEDULE_ROW_GAP = 6;

static int scheduleRowY(int row) {
    int y = SETTINGS_CONTENT_TOP + 8;
    y += SETTINGS_LINE_H_SCHEDULE;  // Face 0 header
    if (row < 2) {
        return y + row * (SETTINGS_BTN_H + SCHEDULE_ROW_GAP);
    }
    y += 2 * (SETTINGS_BTN_H + SCHEDULE_ROW_GAP) + 8;
    y += SETTINGS_LINE_H_SCHEDULE;  // Face 1 header
    return y + (row - 2) * (SETTINGS_BTN_H + SCHEDULE_ROW_GAP);
}

static void getIntervalRowButtons(int y, UiRect &minus, UiRect &plus) {
    const int btnW = 52;
    const int gap = 8;
    const int valueW = 108;
    int right = SCREEN_WIDTH - SETTINGS_MARGIN;
    plus = {right - btnW, y, btnW, SETTINGS_BTN_H};
    int valueLeft = plus.x - gap - valueW;
    minus = {valueLeft - gap - btnW, y, btnW, SETTINGS_BTN_H};
}

static void drawIntervalRow(int y, const String &label, int minutes) {
    setSettingsFont(SETTINGS_FONT_SCHEDULE);
    M5.Display.drawString(label, SETTINGS_MARGIN, y + 12);
    UiRect minus;
    UiRect plus;
    getIntervalRowButtons(y, minus, plus);
    drawOutlinedButton(minus, "-", false);
    drawOutlinedButton(plus, "+", false);
    int valueCenterX = minus.x + minus.w + (plus.x - (minus.x + minus.w)) / 2;
    M5.Display.setTextDatum(MC_DATUM);
    M5.Display.setTextSize(SETTINGS_FONT_SCHEDULE);
    M5.Display.setTextColor(TFT_BLACK);
    M5.Display.drawString(String(minutes) + " min", valueCenterX, y + SETTINGS_BTN_H / 2);
    M5.Display.setTextDatum(TL_DATUM);
}

static void drawSchedulePage(SettingsUiState &st) {
    setSettingsFont(SETTINGS_FONT_SCHEDULE);
    M5.Display.drawString("Face 0 (weather dashboard)", SETTINGS_MARGIN, SETTINGS_CONTENT_TOP + 8);
    drawIntervalRow(scheduleRowY(0), "Wx day", st.draft.face0Day);
    drawIntervalRow(scheduleRowY(1), "Wx night", st.draft.face0Night);

    int face1HeaderY = scheduleRowY(2) - SETTINGS_LINE_H_SCHEDULE;
    M5.Display.drawString("Face 1 (clock face)", SETTINGS_MARGIN, face1HeaderY);
    drawIntervalRow(scheduleRowY(2), "Clock day", st.draft.face1ClockDay);
    drawIntervalRow(scheduleRowY(3), "Clock night", st.draft.face1ClockNight);
    drawIntervalRow(scheduleRowY(4), "Weather day", st.draft.face1Day);
    drawIntervalRow(scheduleRowY(5), "Weather night", st.draft.face1Night);

    int y = scheduleRowY(5) + SETTINGS_BTN_H + 10;
    if (displayFace == 1) {
        M5.Display.drawString("Now: clock " + String(getFace1ClockRefreshMinutes(isNightTime())) + "m, weather " +
                              String(getFace1WeatherRefreshMinutes(isNightTime())) + "m",
                              SETTINGS_MARGIN, y);
    } else {
        M5.Display.drawString("Now: Face 0 every " +
                              String(getFaceRefreshMinutes(0, isNightTime())) + " min",
                              SETTINGS_MARGIN, y);
    }
}

static void drawNightPage(SettingsUiState &st) {
    int y = SETTINGS_CONTENT_TOP + SETTINGS_MARGIN;
    setSettingsFont(SETTINGS_FONT_PAGE);
    UiRect toggle = {SETTINGS_MARGIN, y, 260, SETTINGS_BTN_H};
    drawOutlinedButton(toggle, st.draft.nightMode ? "Night: ON" : "Night: OFF", st.draft.nightMode);
    y += SETTINGS_BTN_H + 20;
    M5.Display.drawString("Start hour: " + String(st.draft.nightStart), SETTINGS_MARGIN, y);
    y += SETTINGS_LINE_H_PAGE + 6;
    UiRect sMinus = {SETTINGS_MARGIN, y, 56, SETTINGS_BTN_H};
    UiRect sPlus = {SETTINGS_MARGIN + 66, y, 56, SETTINGS_BTN_H};
    drawOutlinedButton(sMinus, "-", false);
    drawOutlinedButton(sPlus, "+", false);
    y += SETTINGS_BTN_H + 18;
    M5.Display.drawString("End hour: " + String(st.draft.nightEnd), SETTINGS_MARGIN, y);
    y += SETTINGS_LINE_H_PAGE + 6;
    UiRect eMinus = {SETTINGS_MARGIN, y, 56, SETTINGS_BTN_H};
    UiRect ePlus = {SETTINGS_MARGIN + 66, y, 56, SETTINGS_BTN_H};
    drawOutlinedButton(eMinus, "-", false);
    drawOutlinedButton(ePlus, "+", false);
    y += SETTINGS_BTN_H + 18;
    M5.Display.drawString("Example: 22 = 10 PM, 5 = 5 AM", SETTINGS_MARGIN, y);
}

static void drawPagerContent(SettingsUiState &st) {
    M5.Display.fillRect(0, SETTINGS_CONTENT_TOP, SCREEN_WIDTH,
                        SETTINGS_CONTENT_BOTTOM - SETTINGS_CONTENT_TOP, TFT_WHITE);
    switch (st.pageIndex) {
        case 0: drawWifiPage(st); break;
        case 1: drawLocationPage(st); break;
        case 2: drawCalendarPage(st); break;
        case 3: drawSchedulePage(st); break;
        case 4: drawNightPage(st); break;
    }
    if (st.statusMsg.length() > 0) {
        M5.Display.fillRect(SETTINGS_MARGIN, SETTINGS_CONTENT_BOTTOM - 40,
                            SCREEN_WIDTH - SETTINGS_MARGIN * 2, 32, TFT_WHITE);
        setSettingsFont(SETTINGS_FONT);
        M5.Display.drawString(st.statusMsg, SETTINGS_MARGIN, SETTINGS_CONTENT_BOTTOM - 36);
    }
}

static void drawWifiScan(SettingsUiState &st) {
    setSettingsFont(SETTINGS_FONT);
    M5.Display.drawString("Choose WiFi", SETTINGS_MARGIN, SETTINGS_MARGIN);
    M5.Display.drawString("Tap a network", SETTINGS_MARGIN, SETTINGS_MARGIN + SETTINGS_LINE_H);

    int yStart = 56;
    int listBottom = SETTINGS_CONTENT_BOTTOM - SETTINGS_SCAN_FOOTER_H;
    int visibleRows = (listBottom - yStart) / SETTINGS_LIST_ROW_H;
    if (visibleRows < 1) {
        visibleRows = 1;
    }
    for (int i = 0; i < visibleRows; i++) {
        int idx = st.listScroll + i;
        if (idx >= st.scanCount) {
            break;
        }
        int y = yStart + i * SETTINGS_LIST_ROW_H;
        String ssid = WiFi.SSID(idx);
        if (ssid.length() == 0) {
            ssid = "(hidden)";
        }
        bool secure = WiFi.encryptionType(idx) != WIFI_AUTH_OPEN;
        String line = ssid + "  " + String(WiFi.RSSI(idx)) + " dBm" + (secure ? "  lock" : "");
        M5.Display.drawRect(SETTINGS_MARGIN, y, SCREEN_WIDTH - SETTINGS_MARGIN * 2,
                              SETTINGS_LIST_ROW_H - 4, TFT_BLACK);
        M5.Display.drawString(line, SETTINGS_MARGIN + 8, y + 14);
    }

    int footerY = SETTINGS_CONTENT_BOTTOM - SETTINGS_BTN_H - 8;
    UiRect rescan = {SETTINGS_MARGIN, footerY, 180, SETTINGS_BTN_H};
    UiRect cancel = {SCREEN_WIDTH - SETTINGS_MARGIN - 180, footerY, 180, SETTINGS_BTN_H};
    drawOutlinedButton(rescan, "Rescan", false);
    drawOutlinedButton(cancel, "Cancel", false);
}

static void drawPasswordScreen(SettingsUiState &st) {
    setSettingsFont(SETTINGS_FONT);
    M5.Display.drawString("Password for:", SETTINGS_MARGIN, 10);
    M5.Display.drawString(st.pendingSsid, SETTINGS_MARGIN, 10 + SETTINGS_LINE_H);

    UiRect showBtn = {SCREEN_WIDTH - SETTINGS_MARGIN - 100, 10, 100, 36};
    drawOutlinedButton(showBtn, st.showPassword ? "Hide" : "Show", false);

    st.fieldX = SETTINGS_MARGIN;
    st.fieldY = 10 + SETTINGS_LINE_H * 2 + 8;
    st.fieldW = SCREEN_WIDTH - SETTINGS_MARGIN * 2;
    st.fieldH = SETTINGS_INPUT_H;
    drawTextField(st.fieldX, st.fieldY, st.fieldW, st.fieldH, st.editBuffer,
                  "", true, st.showPassword, st.lastCharRevealMs);

    int kbTop = st.fieldY + st.fieldH + 14;
    int kbBottom = SETTINGS_CONTENT_BOTTOM - SETTINGS_KB_FOOTER_H;
    keyboardComputeLayout(kbTop, kbBottom, KEYBOARD_PASSWORD, false, st.kbLayout);
    st.kbLayout.shift = st.shift;
    keyboardDraw(st.kbLayout);

    int footerY = SETTINGS_CONTENT_BOTTOM - SETTINGS_BTN_H - 8;
    UiRect connectBtn = {SETTINGS_MARGIN, footerY,
                         (SCREEN_WIDTH - SETTINGS_MARGIN * 3) / 2, SETTINGS_BTN_H};
    UiRect cancelBtn = {SETTINGS_MARGIN * 2 + connectBtn.w, footerY,
                        connectBtn.w, SETTINGS_BTN_H};
    drawOutlinedButton(connectBtn, "Connect", true);
    drawOutlinedButton(cancelBtn, "Cancel", false);
}

static void drawKeyboardEditor(SettingsUiState &st) {
    String title = "City name";
    KeyboardMode mode = KEYBOARD_TEXT;
    if (st.editTarget == 1) {
        title = "Latitude";
        mode = KEYBOARD_NUMERIC;
    } else if (st.editTarget == 2) {
        title = "Longitude";
        mode = KEYBOARD_NUMERIC;
    }
    setSettingsFont(SETTINGS_FONT);
    M5.Display.drawString(title, SETTINGS_MARGIN, 10);

    UiRect doneBtn = {SCREEN_WIDTH - SETTINGS_MARGIN - 120, 8, 120, 40};
    drawOutlinedButton(doneBtn, "Done", true);

    st.fieldX = SETTINGS_MARGIN;
    st.fieldY = 56;
    st.fieldW = SCREEN_WIDTH - SETTINGS_MARGIN * 2;
    st.fieldH = SETTINGS_INPUT_H;
    drawTextField(st.fieldX, st.fieldY, st.fieldW, st.fieldH, st.editBuffer,
                  "", false, false, 0);

    int kbTop = st.fieldY + st.fieldH + 12;
    int kbBottom = SETTINGS_CONTENT_BOTTOM;
    keyboardComputeLayout(kbTop, kbBottom, mode, false, st.kbLayout);
    st.kbLayout.shift = st.shift;
    keyboardDraw(st.kbLayout);
}

static void redraw(SettingsUiState &st) {
    beginFrame();
    if (st.subScreen == SUB_WIFI_SCAN) {
        drawWifiScan(st);
    } else if (st.subScreen == SUB_WIFI_PASSWORD) {
        drawPasswordScreen(st);
    } else if (st.subScreen == SUB_KEYBOARD) {
        drawKeyboardEditor(st);
    } else {
        drawChrome(st, true);
        drawPagerContent(st);
    }
    fullRefresh();
}

static void commitKeyboardEdit(SettingsUiState &st) {
    if (st.editTarget == 0) {
        st.draft.city = st.editBuffer;
    } else if (st.editTarget == 1) {
        st.draft.latitude = st.editBuffer;
    } else if (st.editTarget == 2) {
        st.draft.longitude = st.editBuffer;
    }
    st.subScreen = SUB_NONE;
}

static bool processKeyboardKey(SettingsUiState &st, const String &key, const KeyboardKeyRect &hitKey,
                               bool passwordMode, SettingsActivityCallback cb, bool &needFullRedraw) {
    needFullRedraw = false;
    markActivity(st, cb);

    if (key == "\t") {
        st.shift = !st.shift;
        st.kbLayout.shift = st.shift;
        needFullRedraw = true;
        return true;
    }
    if (key == "\x1b") {
        if (passwordMode) {
            st.subScreen = SUB_WIFI_SCAN;
            needFullRedraw = true;
        } else {
            commitKeyboardEdit(st);
            needFullRedraw = true;
        }
        return true;
    }
    if (key == "\n") {
        if (passwordMode) {
            String err;
            if (tryConnectWifi(st.pendingSsid, st.editBuffer, err)) {
                st.draft.ssid = st.pendingSsid;
                st.draft.password = st.editBuffer;
                st.wifiStatus = "Connected to " + st.pendingSsid;
                st.subScreen = SUB_NONE;
            } else {
                st.wifiStatus = err;
            }
            needFullRedraw = true;
        } else {
            commitKeyboardEdit(st);
            needFullRedraw = true;
        }
        return true;
    }
    if (key == "\b") {
        if (st.editBuffer.length() > 0) {
            st.editBuffer.remove(st.editBuffer.length() - 1);
        }
    } else {
        st.editBuffer += key;
        st.lastCharRevealMs = millis();
    }

    keyboardFlashKey(hitKey);
    keyboardRefreshTextField(st.fieldX, st.fieldY, st.fieldW, st.fieldH, st.editBuffer,
                           passwordMode, st.showPassword, st.lastCharRevealMs);
    return true;
}

static bool handlePageContentTouch(SettingsUiState &st, int tx, int ty, SettingsActivityCallback cb) {
    if (st.pageIndex == 0) {
        UiRect chooseBtn = {SETTINGS_MARGIN, st.chooseWifiBtnY,
                            SCREEN_WIDTH - SETTINGS_MARGIN * 2, SETTINGS_BTN_H};
        if (inRect(tx, ty, chooseBtn)) {
            markActivity(st, cb);
            st.subScreen = SUB_WIFI_SCAN;
            st.listScroll = 0;
            WiFi.mode(WIFI_STA);
            st.scanCount = WiFi.scanNetworks();
            redraw(st);
            return true;
        }
    } else if (st.pageIndex == 1) {
        int colW = (SCREEN_WIDTH - SETTINGS_MARGIN * 3) / 2;
        int leftX = SETTINGS_MARGIN;
        int rightX = SETTINGS_MARGIN * 2 + colW;
        int y = SETTINGS_CONTENT_TOP + SETTINGS_MARGIN + SETTINGS_LINE_H * 2 + 4;
        UiRect cityBtn = {leftX, y, colW, SETTINGS_BTN_H};
        if (inRect(tx, ty, cityBtn)) {
            st.editTarget = 0;
            st.editBuffer = st.draft.city;
            st.subScreen = SUB_KEYBOARD;
            st.shift = false;
            markActivity(st, cb);
            redraw(st);
            return true;
        }
        UiRect latBtn = {rightX, y, colW, SETTINGS_BTN_H};
        if (inRect(tx, ty, latBtn)) {
            st.editTarget = 1;
            st.editBuffer = coordDisplay(st.draft.latitude);
            st.subScreen = SUB_KEYBOARD;
            markActivity(st, cb);
            redraw(st);
            return true;
        }
        y += SETTINGS_BTN_H + 16 + SETTINGS_LINE_H * 2 + 4;
        UiRect lonBtn = {rightX, y, colW, SETTINGS_BTN_H};
        if (inRect(tx, ty, lonBtn)) {
            st.editTarget = 2;
            st.editBuffer = coordDisplay(st.draft.longitude);
            st.subScreen = SUB_KEYBOARD;
            markActivity(st, cb);
            redraw(st);
            return true;
        }
        y += SETTINGS_BTN_H + 20;
        UiRect fBtn = {leftX + 150, y - 4, 90, 40};
        UiRect cBtn = {leftX + 250, y - 4, 90, 40};
        if (inRect(tx, ty, fBtn)) {
            st.draft.tempUnit = "F";
            markActivity(st, cb);
            redraw(st);
            return true;
        }
        if (inRect(tx, ty, cBtn)) {
            st.draft.tempUnit = "C";
            markActivity(st, cb);
            redraw(st);
            return true;
        }
    } else if (st.pageIndex == 2) {
        int y = SETTINGS_CONTENT_TOP + SETTINGS_MARGIN + SETTINGS_LINE_H + 4;
        UiRect monthBtn = {SETTINGS_MARGIN, y, (SCREEN_WIDTH - SETTINGS_MARGIN * 3) / 2, SETTINGS_BTN_H};
        UiRect googleBtn = {SETTINGS_MARGIN * 2 + monthBtn.w, y, monthBtn.w, SETTINGS_BTN_H};
        if (inRect(tx, ty, monthBtn)) {
            st.draft.calendarMode = DEFAULT_CALENDAR_MODE;
            markActivity(st, cb);
            redraw(st);
            return true;
        }
        if (inRect(tx, ty, googleBtn)) {
            st.draft.calendarMode = "google";
            markActivity(st, cb);
            redraw(st);
            return true;
        }
    } else if (st.pageIndex == 3) {
        for (int row = 0; row < SCHEDULE_ROW_COUNT; row++) {
            int y = scheduleRowY(row);
            UiRect minus;
            UiRect plus;
            getIntervalRowButtons(y, minus, plus);
            if (inRect(tx, ty, minus)) {
                if (row == 0) stepInterval(st.draft.face0Day, -1);
                if (row == 1) stepInterval(st.draft.face0Night, -1);
                if (row == 2) stepFace1ClockInterval(st.draft.face1ClockDay, -1);
                if (row == 3) stepFace1ClockInterval(st.draft.face1ClockNight, -1);
                if (row == 4) stepFace1Interval(st.draft.face1Day, -1);
                if (row == 5) stepFace1Interval(st.draft.face1Night, -1);
                markActivity(st, cb);
                redraw(st);
                return true;
            }
            if (inRect(tx, ty, plus)) {
                if (row == 0) stepInterval(st.draft.face0Day, 1);
                if (row == 1) stepInterval(st.draft.face0Night, 1);
                if (row == 2) stepFace1ClockInterval(st.draft.face1ClockDay, 1);
                if (row == 3) stepFace1ClockInterval(st.draft.face1ClockNight, 1);
                if (row == 4) stepFace1Interval(st.draft.face1Day, 1);
                if (row == 5) stepFace1Interval(st.draft.face1Night, 1);
                markActivity(st, cb);
                redraw(st);
                return true;
            }
        }
    } else if (st.pageIndex == 4) {
        int y = SETTINGS_CONTENT_TOP + SETTINGS_MARGIN;
        UiRect toggle = {SETTINGS_MARGIN, y, 260, SETTINGS_BTN_H};
        if (inRect(tx, ty, toggle)) {
            st.draft.nightMode = !st.draft.nightMode;
            markActivity(st, cb);
            redraw(st);
            return true;
        }
        y += SETTINGS_BTN_H + 20 + SETTINGS_LINE_H_PAGE + 6;
        UiRect sMinus = {SETTINGS_MARGIN, y, 56, SETTINGS_BTN_H};
        UiRect sPlus = {SETTINGS_MARGIN + 66, y, 56, SETTINGS_BTN_H};
        if (inRect(tx, ty, sMinus)) {
            st.draft.nightStart = (st.draft.nightStart + 23) % 24;
            markActivity(st, cb);
            redraw(st);
            return true;
        }
        if (inRect(tx, ty, sPlus)) {
            st.draft.nightStart = (st.draft.nightStart + 1) % 24;
            markActivity(st, cb);
            redraw(st);
            return true;
        }
        y += SETTINGS_BTN_H + 18 + SETTINGS_LINE_H_PAGE + 6;
        UiRect eMinus = {SETTINGS_MARGIN, y, 56, SETTINGS_BTN_H};
        UiRect ePlus = {SETTINGS_MARGIN + 66, y, 56, SETTINGS_BTN_H};
        if (inRect(tx, ty, eMinus)) {
            st.draft.nightEnd = (st.draft.nightEnd + 23) % 24;
            markActivity(st, cb);
            redraw(st);
            return true;
        }
        if (inRect(tx, ty, ePlus)) {
            st.draft.nightEnd = (st.draft.nightEnd + 1) % 24;
            markActivity(st, cb);
            redraw(st);
            return true;
        }
    }
    return true;
}

static bool handleScanTouch(SettingsUiState &st, int tx, int ty, SettingsActivityCallback cb) {
    int footerY = SETTINGS_CONTENT_BOTTOM - SETTINGS_BTN_H - 8;
    UiRect rescan = {SETTINGS_MARGIN, footerY, 180, SETTINGS_BTN_H};
    UiRect cancel = {SCREEN_WIDTH - SETTINGS_MARGIN - 180, footerY, 180, SETTINGS_BTN_H};
    if (inRect(tx, ty, cancel)) {
        st.subScreen = SUB_NONE;
        markActivity(st, cb);
        redraw(st);
        return true;
    }
    if (inRect(tx, ty, rescan)) {
        st.scanCount = WiFi.scanNetworks();
        markActivity(st, cb);
        redraw(st);
        return true;
    }

    int yStart = 56;
    int listBottom = SETTINGS_CONTENT_BOTTOM - SETTINGS_SCAN_FOOTER_H;
    int visibleRows = (listBottom - yStart) / SETTINGS_LIST_ROW_H;
    if (visibleRows < 1) {
        visibleRows = 1;
    }
    for (int i = 0; i < visibleRows; i++) {
        int idx = st.listScroll + i;
        if (idx >= st.scanCount) {
            break;
        }
        int y = yStart + i * SETTINGS_LIST_ROW_H;
        UiRect row = {SETTINGS_MARGIN, y, SCREEN_WIDTH - SETTINGS_MARGIN * 2, SETTINGS_LIST_ROW_H - 2};
        if (inRect(tx, ty, row)) {
            st.pendingSsid = WiFi.SSID(idx);
            if (st.pendingSsid.length() == 0) {
                st.statusMsg = "Hidden networks not supported";
                st.subScreen = SUB_NONE;
                redraw(st);
                return true;
            }
            st.pendingEncrypt = WiFi.encryptionType(idx);
            if (st.pendingEncrypt == WIFI_AUTH_OPEN) {
                String err;
                if (tryConnectWifi(st.pendingSsid, "", err)) {
                    st.draft.ssid = st.pendingSsid;
                    st.draft.password = "";
                    st.wifiStatus = "Connected to " + st.pendingSsid;
                } else {
                    st.wifiStatus = err;
                }
                st.subScreen = SUB_NONE;
            } else {
                st.editBuffer = "";
                st.showPassword = false;
                st.subScreen = SUB_WIFI_PASSWORD;
            }
            markActivity(st, cb);
            redraw(st);
            return true;
        }
    }

    if (ty > yStart && ty < listBottom) {
        if (ty < yStart + 40 && st.listScroll > 0) {
            st.listScroll--;
            redraw(st);
        } else if (st.listScroll + visibleRows < st.scanCount) {
            st.listScroll++;
            redraw(st);
        }
    }
    return true;
}

static bool handlePasswordTouch(SettingsUiState &st, int tx, int ty, SettingsActivityCallback cb) {
    UiRect showBtn = {SCREEN_WIDTH - SETTINGS_MARGIN - 100, 10, 100, 36};
    if (inRect(tx, ty, showBtn)) {
        st.showPassword = !st.showPassword;
        markActivity(st, cb);
        keyboardRefreshTextField(st.fieldX, st.fieldY, st.fieldW, st.fieldH, st.editBuffer,
                                 true, st.showPassword, st.lastCharRevealMs);
        return true;
    }

    int footerY = SETTINGS_CONTENT_BOTTOM - SETTINGS_BTN_H - 8;
    UiRect connectBtn = {SETTINGS_MARGIN, footerY,
                         (SCREEN_WIDTH - SETTINGS_MARGIN * 3) / 2, SETTINGS_BTN_H};
    UiRect cancelBtn = {SETTINGS_MARGIN * 2 + connectBtn.w, footerY,
                        connectBtn.w, SETTINGS_BTN_H};
    if (inRect(tx, ty, cancelBtn)) {
        st.subScreen = SUB_WIFI_SCAN;
        markActivity(st, cb);
        redraw(st);
        return true;
    }
    if (inRect(tx, ty, connectBtn)) {
        String err;
        if (tryConnectWifi(st.pendingSsid, st.editBuffer, err)) {
            st.draft.ssid = st.pendingSsid;
            st.draft.password = st.editBuffer;
            st.wifiStatus = "Connected to " + st.pendingSsid;
            st.subScreen = SUB_NONE;
        } else {
            st.wifiStatus = err;
        }
        markActivity(st, cb);
        redraw(st);
        return true;
    }

    st.kbLayout.shift = st.shift;
    String key;
    KeyboardKeyRect hitKey;
    if (keyboardHitTest(st.kbLayout, tx, ty, key, &hitKey) >= 0) {
        bool needFullRedraw = false;
        processKeyboardKey(st, key, hitKey, true, cb, needFullRedraw);
        if (needFullRedraw) {
            redraw(st);
        }
    }
    return true;
}

static bool handleKeyboardTouch(SettingsUiState &st, int tx, int ty, SettingsActivityCallback cb) {
    UiRect doneBtn = {SCREEN_WIDTH - SETTINGS_MARGIN - 120, 8, 120, 40};
    if (inRect(tx, ty, doneBtn)) {
        commitKeyboardEdit(st);
        markActivity(st, cb);
        redraw(st);
        return true;
    }

    st.kbLayout.shift = st.shift;
    String key;
    KeyboardKeyRect hitKey;
    if (keyboardHitTest(st.kbLayout, tx, ty, key, &hitKey) < 0) {
        return true;
    }
    bool needFullRedraw = false;
    processKeyboardKey(st, key, hitKey, false, cb, needFullRedraw);
    if (needFullRedraw) {
        redraw(st);
    }
    return true;
}

static bool handleBack(SettingsUiState &st, SettingsActivityCallback cb) {
    markActivity(st, cb);
    if (st.subScreen == SUB_WIFI_PASSWORD) {
        st.subScreen = SUB_WIFI_SCAN;
        redraw(st);
        return true;
    }
    if (st.subScreen == SUB_WIFI_SCAN || st.subScreen == SUB_KEYBOARD) {
        st.subScreen = SUB_NONE;
        redraw(st);
        return true;
    }
    return false;
}

SettingsExitResult runOnDeviceSettings(SettingsActivityCallback onActivity) {
    SettingsUiState st;
    loadSettingsFromPreferences(st.draft);
    st.draft.latitude = coordDisplay(st.draft.latitude);
    st.draft.longitude = coordDisplay(st.draft.longitude);
    st.pageIndex = 0;
    st.subScreen = SUB_NONE;
    st.listScroll = 0;
    st.scanCount = 0;
    st.showPassword = false;
    st.lastCharRevealMs = 0;
    st.shift = false;
    st.symbols = false;
    st.statusMsg = "";
    st.wifiStatus = "";
    st.chooseWifiBtnY = 0;
    st.advWebBtnY = 0;
    st.lastActivityMs = millis();
    if (onActivity) {
        onActivity();
    }

    M5.Display.setTextColor(TFT_BLACK);
    redraw(st);

    while (true) {
        if (st.subScreen == SUB_WIFI_PASSWORD && !st.showPassword &&
            st.editBuffer.length() > 0) {
            unsigned long since = millis() - st.lastCharRevealMs;
            if (since >= 1000 && since < 1050) {
                keyboardRefreshTextField(st.fieldX, st.fieldY, st.fieldW, st.fieldH, st.editBuffer,
                                         true, st.showPassword, st.lastCharRevealMs);
            }
        }

        M5.update();

        if (M5.BtnA.wasPressed()) {
            if (!handleBack(st, onActivity)) {
                return SETTINGS_EXIT_CANCEL;
            }
            continue;
        }

        auto touch = M5.Touch.getDetail();
        if (!touch.wasPressed()) {
            delay(20);
            continue;
        }

        int tx = touch.x;
        int ty = touch.y;

        if (st.subScreen == SUB_NONE) {
            if (inRect(tx, ty, st.btnPrev)) {
                markActivity(st, onActivity);
                st.pageIndex = (st.pageIndex + SETTINGS_PAGE_COUNT - 1) % SETTINGS_PAGE_COUNT;
                st.statusMsg = "";
                redraw(st);
                continue;
            }
            if (inRect(tx, ty, st.btnNext)) {
                markActivity(st, onActivity);
                st.pageIndex = (st.pageIndex + 1) % SETTINGS_PAGE_COUNT;
                st.statusMsg = "";
                redraw(st);
                continue;
            }
            if (inRect(tx, ty, st.btnBack)) {
                markActivity(st, onActivity);
                return SETTINGS_EXIT_CANCEL;
            }
            if (inRect(tx, ty, st.btnSave)) {
                String err = validateSettings(st.draft, false);
                if (err.length() > 0) {
                    st.statusMsg = err;
                    markActivity(st, onActivity);
                    redraw(st);
                    continue;
                }
                saveSettingsToPreferences(st.draft, true);
                return SETTINGS_EXIT_SAVED;
            }
            if (st.pageIndex == 0) {
                UiRect advBtn = {SETTINGS_MARGIN, st.advWebBtnY,
                                 SCREEN_WIDTH - SETTINGS_MARGIN * 2, SETTINGS_BTN_H};
                if (inRect(tx, ty, advBtn)) {
                    markActivity(st, onActivity);
                    return SETTINGS_EXIT_WEB_PORTAL;
                }
            }
            handlePageContentTouch(st, tx, ty, onActivity);
        } else if (st.subScreen == SUB_WIFI_SCAN) {
            handleScanTouch(st, tx, ty, onActivity);
        } else if (st.subScreen == SUB_WIFI_PASSWORD) {
            handlePasswordTouch(st, tx, ty, onActivity);
        } else if (st.subScreen == SUB_KEYBOARD) {
            handleKeyboardTouch(st, tx, ty, onActivity);
        }
    }
}
