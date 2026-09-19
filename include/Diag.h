#ifndef DIAG_H
#define DIAG_H

#include <Arduino.h>

// Initialize diag helpers (mount SPIFFS, print available commands)
void diagBegin();

// Handle an input line from Serial. Return true if the line was handled
// by diag; false if not (caller may process it as other CLI commands).
bool diagHandleLine(const String &line);

#endif // DIAG_H
