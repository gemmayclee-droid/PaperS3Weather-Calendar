#ifndef CALENDAR_API_H
#define CALENDAR_API_H

#include <Arduino.h>

#define MAX_CALENDAR_EVENTS 3

bool fetchCalendarData(const String &icsUrl);

extern String calendarEvents[MAX_CALENDAR_EVENTS];
extern int calendarEventCount;
extern bool calendarFetchOk;
extern String calendarStatusMessage;

#endif // CALENDAR_API_H
