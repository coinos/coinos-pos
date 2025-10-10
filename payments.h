#pragma once
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include "config.h"

struct Payment {
  String id;
  long long amount;   // sats
  long long tip;      // sats
  uint64_t created;   // ms
  String currency;
  double  rate;
};

inline unsigned long long roundCentsU64(unsigned long long sats, double rate_per_btc) {
  long long rate_cents = llround(rate_per_btc * 100.0);
  unsigned long long num = (unsigned long long)sats * (unsigned long long)rate_cents;
  return (num + 50000000ULL) / 100000000ULL;
}

inline String formatCents(unsigned long long c) {
  char buf[40];
  unsigned long long dollars = c / 100ULL;
  unsigned frac = (unsigned)(c % 100ULL);
  snprintf(buf, sizeof(buf), "%llu.%02u",
           (unsigned long long)dollars, frac);
  return String(buf);
}
