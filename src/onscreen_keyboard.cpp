#include "onscreen_keyboard.h"
#include "constants.h"

static const char *ROW1 = "qwertyuiop";
static const char *ROW2 = "asdfghjkl";
static const char *ROW3 = "zxcvbnm";
static const char *ROW1_SHIFT = "QWERTYUIOP";
static const char *ROW2_SHIFT = "ASDFGHJKL";
static const char *ROW3_SHIFT = "ZXCVBNM";
static const char *NUM_ROW = "1234567890";
static const char *SPEC_ROW = "!#$_+=-?&*^%";
static const char *NUM_ROW2 = "-.";
static const char *NUM_ROW3 = "0123456789";

static int rowCount(const KeyboardLayout &layout) {
    if (layout.mode == KEYBOARD_NUMERIC) {
        return 4;  // 3 number rows + controls
    }
    return layout.includeActionRow ? 7 : 6;  // num, spec, 3 alpha, controls [, actions]
}

static int keyHeightForLayout(const KeyboardLayout &layout) {
    int rows = rowCount(layout);
    int gap = 3;
    int totalGaps = (rows + 1) * gap;
    int avail = layout.h - totalGaps;
    if (avail < rows * 24) {
        return 24;
    }
    return avail / rows;
}

static void drawButton(const KeyboardKeyRect &key, bool inverted) {
    if (inverted) {
        M5.Display.fillRect(key.x, key.y, key.w, key.h, TFT_BLACK);
        M5.Display.setTextColor(TFT_WHITE);
    } else {
        M5.Display.fillRect(key.x, key.y, key.w, key.h, TFT_WHITE);
        M5.Display.drawRect(key.x, key.y, key.w, key.h, TFT_BLACK);
        M5.Display.setTextColor(TFT_BLACK);
    }
    M5.Display.setTextDatum(MC_DATUM);
    M5.Display.setTextSize(SETTINGS_FONT);
    M5.Display.drawString(key.label, key.x + key.w / 2, key.y + key.h / 2);
    M5.Display.setTextDatum(TL_DATUM);
}

static int addCharRow(KeyboardKeyRect *keys, int count, int maxKeys,
                      int x, int y, int totalW, int kh, int gap, const char *chars) {
    int n = 0;
    while (chars[n]) {
        n++;
    }
    if (n == 0) {
        return count;
    }
    int kw = (totalW - gap * (n + 1)) / n;
    for (int i = 0; i < n && count < maxKeys; i++) {
        keys[count++] = {x + gap + i * (kw + gap), y, kw, kh,
                           String(chars[i]), String(chars[i])};
    }
    return count;
}

void keyboardComputeLayout(int contentTop, int contentBottom, KeyboardMode mode,
                           bool includeActionRow, KeyboardLayout &layout) {
    layout.x = SETTINGS_MARGIN;
    layout.y = contentTop;
    layout.w = SCREEN_WIDTH - SETTINGS_MARGIN * 2;
    layout.h = contentBottom - contentTop;
    layout.mode = mode;
    layout.shift = false;
    layout.symbols = false;
    layout.includeActionRow = includeActionRow;
}

int keyboardBuildKeyRects(const KeyboardLayout &layout, KeyboardKeyRect *keys, int maxKeys) {
    int count = 0;
    int gap = 3;
    int kh = keyHeightForLayout(layout);
    int y = layout.y;

    if (layout.mode == KEYBOARD_NUMERIC) {
        count = addCharRow(keys, count, maxKeys, layout.x, y, layout.w, kh, gap, NUM_ROW3);
        y += kh + gap;
        count = addCharRow(keys, count, maxKeys, layout.x, y, layout.w, kh, gap, NUM_ROW2);
        y += kh + gap;
        int cols = 5;
        int kw = (layout.w - gap * (cols + 1)) / cols;
        const char *row4 = "+-";
        for (int i = 0; row4[i] && count < maxKeys; i++) {
            keys[count++] = {layout.x + gap + i * (kw + gap), y, kw * 2, kh,
                             String(row4[i]), String(row4[i])};
        }
        y += kh + gap;
        int bkspW = (layout.w - gap * 3) / 2;
        keys[count++] = {layout.x + gap, y, bkspW, kh, "Bksp", "\b"};
        keys[count++] = {layout.x + gap * 2 + bkspW, y, bkspW, kh, "Done", "\n"};
        return count;
    }

    count = addCharRow(keys, count, maxKeys, layout.x, y, layout.w, kh, gap, NUM_ROW);
    y += kh + gap;
    count = addCharRow(keys, count, maxKeys, layout.x, y, layout.w, kh, gap, SPEC_ROW);
    y += kh + gap;

    const char *r1 = layout.shift ? ROW1_SHIFT : ROW1;
    const char *r2 = layout.shift ? ROW2_SHIFT : ROW2;
    const char *r3 = layout.shift ? ROW3_SHIFT : ROW3;

    int kw = (layout.w - gap * 11) / 10;
    for (int i = 0; r1[i] && count < maxKeys; i++) {
        keys[count++] = {layout.x + gap + i * (kw + gap), y, kw, kh,
                         String(r1[i]), String(r1[i])};
    }
    y += kh + gap;
    int off = kw / 2;
    for (int i = 0; r2[i] && count < maxKeys; i++) {
        keys[count++] = {layout.x + gap + off + i * (kw + gap), y, kw, kh,
                         String(r2[i]), String(r2[i])};
    }
    y += kh + gap;
    int off2 = kw;
    for (int i = 0; r3[i] && count < maxKeys; i++) {
        keys[count++] = {layout.x + gap + off2 + i * (kw + gap), y, kw, kh,
                         String(r3[i]), String(r3[i])};
    }

    y += kh + gap;
    int shiftW = kw * 2;
    int bkspW = kw * 2;
    int spaceW = layout.w - shiftW - bkspW - gap * 4;
    keys[count++] = {layout.x + gap, y, shiftW, kh, "Shift", "\t"};
    keys[count++] = {layout.x + gap + shiftW + gap, y, spaceW, kh, "Space", " "};
    keys[count++] = {layout.x + layout.w - gap - bkspW, y, bkspW, kh, "Bksp", "\b"};

    if (layout.includeActionRow) {
        y += kh + gap;
        int half = (layout.w - gap * 3) / 2;
        keys[count++] = {layout.x + gap, y, half, kh, "Cancel", "\x1b"};
        keys[count++] = {layout.x + gap * 2 + half, y, half, kh, "Done", "\n"};
    }
    return count;
}

String formatPasswordDisplay(const String &value, bool showPlaintext, unsigned long lastCharRevealMs) {
    if (showPlaintext) {
        return value;
    }
    if (value.length() == 0) {
        return "";
    }
    String out = "";
    for (unsigned i = 0; i < value.length(); i++) {
        if (i == value.length() - 1 && (millis() - lastCharRevealMs) < 1000) {
            out += value.charAt(i);
        } else {
            out += '\x7f';
        }
    }
    return out;
}

void drawTextFieldClear(int x, int y, int w, int h) {
    M5.Display.fillRect(x - 2, y - 2, w + 4, h + 4, TFT_WHITE);
}

void drawTextField(int x, int y, int w, int h, const String &value, const String &label,
                   bool passwordMode, bool showPlaintext, unsigned long lastCharRevealMs) {
    drawTextFieldClear(x, y, w, h);
    M5.Display.setTextSize(SETTINGS_FONT);
    M5.Display.setTextColor(TFT_BLACK);
    if (label.length() > 0) {
        M5.Display.drawString(label, x, y - 18);
    }
    M5.Display.drawRect(x, y, w, h, TFT_BLACK);
    String shown = passwordMode ? formatPasswordDisplay(value, showPlaintext, lastCharRevealMs) : value;
    if (passwordMode && !showPlaintext) {
        String stars = "";
        for (unsigned i = 0; i < shown.length(); i++) {
            char c = shown.charAt(i);
            stars += (c == '\x7f') ? '*' : c;
        }
        shown = stars;
    }
    M5.Display.setTextDatum(ML_DATUM);
    M5.Display.setTextSize(SETTINGS_FONT);
    M5.Display.drawString(shown, x + 8, y + h / 2);
    M5.Display.setTextDatum(TL_DATUM);
}

void keyboardDraw(const KeyboardLayout &layout) {
    KeyboardKeyRect keys[80];
    int n = keyboardBuildKeyRects(layout, keys, 80);
    for (int i = 0; i < n; i++) {
        drawButton(keys[i], false);
    }
}

void keyboardDrawKey(const KeyboardKeyRect &key, bool inverted) {
    drawButton(key, inverted);
}

void keyboardFlashKey(const KeyboardKeyRect &key) {
    keyboardDrawKey(key, true);
    M5.Display.endWrite();
    M5.Display.display(key.x, key.y, key.w, key.h);
    M5.Display.startWrite();
    delay(70);
    keyboardDrawKey(key, false);
    M5.Display.endWrite();
    M5.Display.display(key.x, key.y, key.w, key.h);
    M5.Display.startWrite();
}

void keyboardRefreshTextField(int x, int y, int w, int h, const String &value,
                              bool passwordMode, bool showPlaintext,
                              unsigned long lastCharRevealMs) {
    drawTextField(x, y, w, h, value, "", passwordMode, showPlaintext, lastCharRevealMs);
    M5.Display.endWrite();
    M5.Display.display(x - 2, y - 2, w + 4, h + 4);
    M5.Display.startWrite();
}

int keyboardHitTest(const KeyboardLayout &layout, int touchX, int touchY, String &output,
                    KeyboardKeyRect *hitKey) {
    KeyboardKeyRect keys[80];
    int n = keyboardBuildKeyRects(layout, keys, 80);
    for (int i = 0; i < n; i++) {
        if (touchX >= keys[i].x && touchX < keys[i].x + keys[i].w &&
            touchY >= keys[i].y && touchY < keys[i].y + keys[i].h) {
            output = keys[i].output;
            if (hitKey) {
                *hitKey = keys[i];
            }
            return i;
        }
    }
    return -1;
}
