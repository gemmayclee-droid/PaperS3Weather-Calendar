#ifndef ONSCREEN_KEYBOARD_H
#define ONSCREEN_KEYBOARD_H

#include <M5Unified.h>
#include <Arduino.h>

enum KeyboardMode {
    KEYBOARD_TEXT,
    KEYBOARD_PASSWORD,
    KEYBOARD_NUMERIC
};

struct KeyboardLayout {
    int x;
    int y;
    int w;
    int h;
    KeyboardMode mode;
    bool shift;
    bool symbols;
    bool includeActionRow;  // Cancel/Done row (false for settings UI)
};

struct KeyboardKeyRect {
    int x, y, w, h;
    String label;
    String output;
};

// Compute keyboard top Y and height from content area; sets key row height internally
void keyboardComputeLayout(int contentTop, int contentBottom, KeyboardMode mode,
                           bool includeActionRow, KeyboardLayout &layout);

int keyboardBuildKeyRects(const KeyboardLayout &layout, KeyboardKeyRect *keys, int maxKeys);

void keyboardDraw(const KeyboardLayout &layout);

void keyboardDrawKey(const KeyboardKeyRect &key, bool inverted);

// Returns key index (>=0) and sets output; -1 if no hit
int keyboardHitTest(const KeyboardLayout &layout, int touchX, int touchY, String &output,
                    KeyboardKeyRect *hitKey);

void drawTextField(int x, int y, int w, int h, const String &value, const String &label,
                   bool passwordMode, bool showPlaintext, unsigned long lastCharRevealMs);

void drawTextFieldClear(int x, int y, int w, int h);

String formatPasswordDisplay(const String &value, bool showPlaintext, unsigned long lastCharRevealMs);

void keyboardFlashKey(const KeyboardKeyRect &key);

void keyboardRefreshTextField(int x, int y, int w, int h, const String &value,
                              bool passwordMode, bool showPlaintext,
                              unsigned long lastCharRevealMs);

#endif // ONSCREEN_KEYBOARD_H
