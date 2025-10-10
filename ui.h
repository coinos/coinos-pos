#pragma once
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>
#include "config.h"

extern Adafruit_SH1106G display;

// Cursor offset used in your code
static const int X_OFFSET = 8;

inline void setCursorOffset(int x, int y) {
  display.setCursor(x + X_OFFSET, y);
}

inline void oledCommand(uint8_t cmd) {
  Wire.beginTransmission(I2C_ADDRESS);
  Wire.write((uint8_t)0x00);
  Wire.write(cmd);
  Wire.endTransmission();
}

inline void ui_begin() {
  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
  Wire.setClock(100000);
  if (!display.begin(I2C_ADDRESS, true)) {
    Serial.println("SH1106 init fail");
    for(;;);
  }
  oledCommand(0x10);
  display.clearDisplay();
  display.display();
}

inline void renderLine( const String& line, const char* header = nullptr ) {
  display.clearDisplay();
  display.setTextWrap(false);
  display.setTextColor(SH110X_WHITE);

  if (header) {
    display.setTextSize(1);
    setCursorOffset(0, 0);
    display.println(header);
  }

  display.setTextSize(3);
  int y = header ? 24 : 8;
  setCursorOffset(0, y);

  String tail = line;
  if ((int)tail.length() > MAX_LINE) tail = tail.substring(tail.length() - MAX_LINE);
  display.print(tail);
  display.display();
}

// Thick line helper
inline int iLerp(int a, int b, float t) {
  if (t < 0) t = 0; if (t > 1) t = 1;
  return a + (int)((b - a) * t);
}
inline void drawThickLine(int x0, int y0, int x1, int y1, uint8_t th, uint16_t color) {
  int dx = x1-x0, dy = y1-y0;
  float len = sqrtf((float)(dx*dx + dy*dy));
  if (len < 1) { display.drawPixel(x0, y0, color); return; }
  float nx = -dy/len, ny = dx/len;
  int half = th/2;
  for (int i=-half; i<=half; ++i) {
    int ox = (int)roundf(nx*i), oy = (int)roundf(ny*i);
    display.drawLine(x0+ox, y0+oy, x1+ox, y1+oy, color);
  }
}
