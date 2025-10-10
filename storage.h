#pragma once
#include <LittleFS.h>
#include <Arduino.h>

struct Credentials {
  String ssid, wifikey, token;
};

inline bool readCredentials(Credentials& out, const char* path="/config.txt") {
  File f = LittleFS.open(path, "r");
  if (!f) { Serial.println("Failed to open creds"); return false; }
  out.ssid    = f.readStringUntil('\n');  out.ssid.trim();
  out.wifikey = f.readStringUntil('\n');  out.wifikey.trim();
  out.token   = f.readStringUntil('\n');  out.token.trim();
  f.close();
  return true;
}
