// wifi_ui.h
#pragma once
#include <WiFi.h>
#include "storage.h"
#include "ui.h"  // <- your OLED helpers (Adafruit_SH1106G display, setCursorOffset, renderLine, drawThickLine, etc.)

// --- tiny UI helpers ---
// Print "SSID: <name>", or an explicit hint when no config was loaded, so a
// blank line on the OLED never gets mistaken for a wifi problem.
inline void drawSsidLine(const String& ssid) {
  if (ssid.length() == 0) {
    display.println("No config.txt!");
  } else {
    display.print("SSID: ");
    display.println(ssid);
  }
}

inline void drawWifiConnectingUI(const String& ssid, uint8_t frame, uint32_t elapsed, uint32_t timeoutMs) {
  static const char* SPIN = "|/-\\";
  float t = timeoutMs ? (float)elapsed / (float)timeoutMs : 0.f;
  if (t < 0) t = 0; if (t > 1) t = 1;

  display.clearDisplay();
  display.setTextWrap(false);
  display.setTextColor(SH110X_WHITE);

  // Header
  display.setTextSize(1);
  setCursorOffset(0, 0);
  display.println("Wi-Fi");

  // Status line (big)
  display.setTextSize(1);
  setCursorOffset(0, 16);
  display.print("Connecting ");
  display.print(SPIN[frame & 3]);

  // SSID (small)
  display.setTextSize(1);
  setCursorOffset(0, 40);
  drawSsidLine(ssid);

  // Progress bar (uses your thick line helper)
  int x0 = X_OFFSET, x1 = X_OFFSET + 112;     // 128px wide screen, leave 8px side margin
  int y  = 56;                                 // near bottom
  drawThickLine(x0, y, x1, y, 5, SH110X_WHITE);            // background track
  int xf  = iLerp(x0, x1, t);
  display.fillRect(x0, y-2, xf - x0, 5, SH110X_WHITE);     // filled progress

  display.display();
}

inline void drawWifiConnectedUI(const IPAddress& ip) {
  display.clearDisplay();
  display.setTextColor(SH110X_WHITE);
  display.setTextWrap(false);

  display.setTextSize(1);
  setCursorOffset(0, 0);
  display.println("Wi-Fi");

  display.setTextSize(2);
  setCursorOffset(0, 18);
  display.println("Connected!");

  display.setTextSize(1);
  setCursorOffset(0, 48);
  display.print("IP: ");
  display.println(ip);
  display.display();
}

inline void drawWifiFailedUI(const String& ssid) {
  display.clearDisplay();
  display.setTextColor(SH110X_WHITE);
  display.setTextWrap(false);

  display.setTextSize(1);
  setCursorOffset(0, 0);
  display.println("Wi-Fi");

  display.setTextSize(2);
  setCursorOffset(0, 18);
  display.println("Timeout");

  display.setTextSize(1);
  setCursorOffset(0, 40);
  drawSsidLine(ssid);
  setCursorOffset(0, 52);
  display.println("Check SSID/password");
  display.display();
}

// --- drop-in replacement for your ensureWifiConnected ---
inline bool ensureWifiConnected(const Credentials& creds,
                                    uint32_t timeoutMs,
                                    const std::function<bool(void)>& allowCancel = nullptr,
                                    uint32_t failHoldMs = 3000) {
  if (WiFi.status() == WL_CONNECTED) {
    return true;
  }

  WiFi.mode(WIFI_STA);
  if (creds.wifikey != "" && creds.wifikey != "none")
    WiFi.begin(creds.ssid.c_str(), creds.wifikey.c_str());
  else
    WiFi.begin(creds.ssid.c_str());

  unsigned long t0 = millis();
  unsigned long lastUI = 0;
  uint8_t frame = 0;

  while (WiFi.status() != WL_CONNECTED && (millis() - t0) < timeoutMs) {
    if (allowCancel && allowCancel()) return false;

    // Update OLED ~every 150ms
    unsigned long now = millis();
    if (now - lastUI >= 150) {
      drawWifiConnectingUI(creds.ssid, frame++, now - t0, timeoutMs);
      lastUI = now;
    }
    delay(30); // short delay to yield
  }

  if (WiFi.status() == WL_CONNECTED) {
    drawWifiConnectedUI(WiFi.localIP());
    return true;
  } else {
    drawWifiFailedUI(creds.ssid);
    delay(failHoldMs);
    return false;
  }
}
