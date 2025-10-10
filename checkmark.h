#pragma once
#include <Arduino.h>
#include "ui.h"

// Animation state
struct CheckmarkState {
  bool active = false;
  uint8_t stage = 0;
  unsigned long stageStart = 0;
  char header[64] = "Payment received!";
};

static CheckmarkState CM;

// Timing
static const unsigned long CM_DUR1 = 450;
static const unsigned long CM_DUR2 = 550;
static const unsigned long CM_HOLD = 1500;

// Geometry
static const int CM_X0 = 30, CM_Y0 = 42;
static const int CM_X1 = 50, CM_Y1 = 58;
static const int CM_X2 = 90, CM_Y2 = 28;
static const uint8_t CM_THICK = 3;

inline void startCheckmarkAnimation(const char* header = nullptr) {
  CM.active = true;
  CM.stage = 1;
  CM.stageStart = millis();
  if (header) {
    strncpy(CM.header, header, sizeof(CM.header) - 1);
    CM.header[sizeof(CM.header) - 1] = '\0';
  } else {
    CM.header[0] = '\0';
  }
}

inline void renderCheckmarkFrame() {
  if (!CM.active) return;
  unsigned long now = millis();

  display.clearDisplay();
  display.setTextColor(SH110X_WHITE);

  if (CM.header[0]) {
    String h = CM.header;
    int plusIdx = h.indexOf('+');
    if (plusIdx >= 0) {
      String mainPart = h.substring(0, plusIdx);
      String tipPart  = h.substring(plusIdx);

      display.setTextSize(2);
      setCursorOffset(0, 0);
      display.print(mainPart);

      int16_t x1, y1; uint16_t w, hgt;
      display.getTextBounds(mainPart.c_str(), 0, 0, &x1, &y1, &w, &hgt);

      display.setTextSize(1);
      setCursorOffset(w + 4, 4);
      display.print(tipPart);
    } else {
      display.setTextSize(2);
      setCursorOffset(0, 0);
      display.println(h);
    }
  }

  float t1 = 0.0f, t2 = 0.0f;
  if (CM.stage == 1) {
    t1 = (now - CM.stageStart) / (float)CM_DUR1;
    if (t1 >= 1.0f) { t1 = 1.0f; CM.stage = 2; CM.stageStart = now; }
  } else {
    t1 = 1.0f;
  }

  int fx = iLerp(CM_X0, CM_X1, t1);
  int fy = iLerp(CM_Y0, CM_Y1, t1);
  drawThickLine(CM_X0, CM_Y0, fx, fy, CM_THICK, SH110X_WHITE);

  if (CM.stage >= 2) {
    if (CM.stage == 2) {
      t2 = (now - CM.stageStart) / (float)CM_DUR2;
      if (t2 >= 1.0f) { t2 = 1.0f; CM.stage = 3; CM.stageStart = now; }
    } else {
      t2 = 1.0f;
    }
    int sx = iLerp(CM_X1, CM_X2, t2);
    int sy = iLerp(CM_Y1, CM_Y2, t2);
    drawThickLine(CM_X1, CM_Y1, sx, sy, CM_THICK, SH110X_WHITE);
  }

  display.display();

  if (CM.stage == 3 && (now - CM.stageStart) >= CM_HOLD) {
    CM.active = false;
  }
}
