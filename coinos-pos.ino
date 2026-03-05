#include <Arduino.h>
#include <Wire.h>
#include <Keypad.h>
#include <WiFi.h>
#include <LittleFS.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>
#include "config.h"
#include "ui.h"
#include "checkmark.h"
#include "storage.h"
#include "wifi_net.h"
#include "payments.h"
#include "nfc.h"
#include "ws.h"
#include "esp_sleep.h"
#include "driver/gpio.h"

// ---- Globals from UI ----
Adafruit_SH1106G display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET_PIN);

// ---- Keypad ----
Keypad keypad = Keypad(makeKeymap(KEYS), (byte*)ROW_PINS, (byte*)COL_PINS, ROWS, COLS);

// ---- State ----
Credentials creds;
String token;
String merchantLnurl;
uint64_t cents = 0;
uint64_t submittedCents = 0;
uint8_t digitsTyped = 0;
String lineStr = "";
String currentInvoiceId = "";
String currentInvoiceFiat = "";
bool invoicePaid = false;


// Auto-submit after 5 seconds of no digit changes
const unsigned long AUTO_SUBMIT_DELAY_MS = 2000;

unsigned long lastAmountChange = 0;
bool autoSubmitDoneForCurrentAmount = false;

Payment history[10];
int historyCount = 0;
int historyIndex = -1;

unsigned long lastHeartbeat = 0;
unsigned long lastActivity = 0;
bool isAsleep = false;

unsigned long lastLog = 0;
const unsigned long LOG_INTERVAL = 5000;

inline void touchActivity() { lastActivity = millis(); }

inline void showAmount(const char* header=nullptr) {
  lineStr = String("$") + formatCents(cents);
  renderLine(lineStr, header);
}

void goToSleep() {
  // Power down peripherals
  oledCommand(0xAE);
  digitalWrite(LED_GREEN_PIN, LOW);
  WiFi.disconnect(true, true);
  WiFi.mode(WIFI_OFF);

  // 1) Bias the keypad for deep sleep:
  //    rows -> OUTPUT LOW (held low during deep sleep)
  for (int i = 0; i < ROWS; ++i) {
    int rp = ROW_PINS[i];
    pinMode(rp, OUTPUT);
    digitalWrite(rp, LOW);
    gpio_hold_en((gpio_num_t)rp);
  }
  gpio_deep_sleep_hold_en();   // enable global hold for held pins

  //    columns -> INPUT_PULLUP (idle HIGH; press pulls LOW via row=LOW)
  for (int i = 0; i < COLS; ++i) {
    pinMode(COL_PINS[i], INPUT_PULLUP);
  }

  // 2) Deep-sleep GPIO wake on wake-capable columns only (GPIO5, GPIO3)
  uint64_t wakeMask = 0;
  for (int i = 0; i < COLS; ++i) {
    int g = COL_PINS[i];
    if (g == 5 || g == 3) wakeMask |= (1ULL << g);
  }
  esp_deep_sleep_enable_gpio_wakeup(wakeMask, ESP_GPIO_WAKEUP_GPIO_LOW);

  // 3) Also wake every 24 hours
  esp_sleep_enable_timer_wakeup(24ULL * 60ULL * 60ULL * 1000000ULL);

  Serial.println("SLEEP (deep: GPIO 3/5 + 24h)");
  esp_deep_sleep_start();   // never returns
}

void wakeFromSleep() {
  oledCommand(0xAF);
  delay(1);
  display.clearDisplay();
  renderLine("Keypad Ready");
  isAsleep = false;
  lastActivity = millis();
  Serial.println("WAKE");
}

void resetAll() {
  digitalWrite(LED_GREEN_PIN, LOW);
  cents = 0;
  digitsTyped = 0;
  if (currentInvoiceId != "") ws_disconnect();
  currentInvoiceId = "";
  invoicePaid = false;
  showAmount("*:Back   #:Enter");

  autoSubmitDoneForCurrentAmount = false;
  lastAmountChange = millis();
}

void renderPayment() {
  if (historyCount == 0) { renderLine("", "No history"); return; }
  if (historyIndex < 0) historyIndex = historyCount - 1;
  if (historyIndex >= historyCount) historyIndex = 0;

  Payment &p = history[historyIndex];

  // Convert sats → fiat cents
  uint64_t satsPaid   = (uint64_t)(p.amount < 0 ? 0 : p.amount);
  uint64_t centsPaid  = roundCentsU64(satsPaid, p.rate);

  uint64_t satsTip    = (uint64_t)(p.tip < 0 ? 0 : p.tip);
  uint64_t centsTip   = roundCentsU64(satsTip, p.rate);

  // Header: local-ish time (keeps your original offset)
  time_t t = (p.created / 1000) - 25200;
  struct tm *createdTime = gmtime(&t);
  char dateString[32], timeString[16], header[48];
  const char* monthAbbrev[] = {"Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"};
  snprintf(dateString, sizeof(dateString), "%s %d, %d",
           monthAbbrev[createdTime->tm_mon],
           createdTime->tm_mday,
           createdTime->tm_year + 1900);
  int h12 = createdTime->tm_hour % 12; if (h12 == 0) h12 = 12;
  snprintf(timeString, sizeof(timeString), "%d:%02d%s",
           h12, createdTime->tm_min,
           (createdTime->tm_hour < 12) ? "am" : "pm");
  snprintf(header, sizeof(header), "%s %s", dateString, timeString);

  String amountStr = "$" + formatCents(centsPaid);
  String tipStr    = centsTip > 0 ? " +$" + formatCents(centsTip) : "";

  int16_t x1, y1; uint16_t w, hgt;

  display.clearDisplay();
  display.setCursor(0, 0);
  display.setTextSize(1);
  display.print(header);

  display.setTextSize(2);
  display.setCursor(4, 24);
  display.print(amountStr); 

  display.getTextBounds(amountStr.c_str(), 0, 0, &x1, &y1, &w, &hgt);
  display.setTextSize(1);
  display.setCursor(w, 28);
  display.print(tipStr); 

  display.display();
}

bool fetchPayments() {
  if (!ensureWifiConnected(creds, 20000)) return false;

  HTTPClient http;
  http.begin(String(API_HOST) + "/payments?limit=10&received=true");
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Authorization", "Bearer " + token);

  int code = http.GET();
  if (code == HTTP_CODE_OK) {
    String payload = http.getString();
    http.end();

    StaticJsonDocument<256> filter;
    JsonArray flist = filter["payments"].to<JsonArray>();
    JsonObject f = flist.createNestedObject();
    f["id"]=true; f["amount"]=true; f["tip"]=true; f["created"]=true; f["currency"]=true; f["rate"]=true;
    filter["count"]=true;

    StaticJsonDocument<4096> doc;
    auto err = deserializeJson(doc, payload, DeserializationOption::Filter(filter));
    if (err) { Serial.print("payments JSON error: "); Serial.println(err.f_str()); return false; }

    JsonArray arr = doc["payments"].as<JsonArray>();
    historyCount = 0;
    for (JsonObjectConst p : arr) {
      if (historyCount >= (int)(sizeof(history)/sizeof(history[0]))) break;
      Payment &out = history[historyCount++];
      out.id       = p["id"]       | "";
      out.amount   = p["amount"]   | 0LL;
      out.tip      = p["tip"]      | 0LL;
      out.created  = p["created"]  | 0ULL;
      out.currency = p["currency"] | "";
      out.rate     = p["rate"]     | 0.0;
    }
    return true;
  } else {
    http.end();
    return false;
  }
}

void submitInvoice() {
  if (digitsTyped == 0) {
    if (historyIndex < 0) { 
      fetchPayments(); 
      if (historyCount > 0) historyIndex = historyCount - 1; 
    }
    else if (historyCount > 0) historyIndex = (historyIndex - 1 + historyCount) % historyCount;
    renderPayment();
    return;
  }

  String fiat = formatCents(cents);
  if (fiat.toFloat() <= 0 || currentInvoiceId != "") { resetAll(); return; }

  if (!ensureWifiConnected(creds, 20000)) return;

  renderLine(lineStr, "Submitting...");
  submittedCents = cents;

  HTTPClient http;
  http.begin(String(API_HOST) + "/invoice");
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Authorization", "Bearer " + token);

  String json = "{\"invoice\":{\"fiat\":\"" + fiat + "\"}}";
  int code = http.POST(json);

  if (code > 0) {
    String payload = http.getString();
    StaticJsonDocument<64> filter; filter["id"] = true;
    DynamicJsonDocument doc(4096);
    auto err = deserializeJson(doc, payload, DeserializationOption::Filter(filter));
    if (!err && doc["id"]) {
      currentInvoiceFiat = fiat;
      currentInvoiceId = (const char*)doc["id"];
      invoicePaid = false;
      ws_connect();
      ws_subscribe(currentInvoiceId);
      renderLine(lineStr, "Waiting for payment..");
    } else {
      renderLine(lineStr, "Invoice error"); delay(800);
    }
  } else {
    renderLine(lineStr, "Submit failed"); delay(800);
  }
  http.end();
}

void handlePayment(long long amount, long long tipSats) {
  if (currentInvoiceId == "" || invoicePaid) return;

  lastActivity = millis();
  invoicePaid = true;
  ws_disconnect();
  digitalWrite(LED_GREEN_PIN, HIGH);

  String header = "$" + currentInvoiceFiat;
  if (tipSats > 0 && submittedCents > 0) {
    double centsPerSat = (double)submittedCents / (double)amount;
    unsigned long long tipCents =
      (unsigned long long) llround((double)tipSats * centsPerSat);
    header += "+$" + formatCents(tipCents);
  }
  startCheckmarkAnimation(header.c_str());
}

// ---- Key handling ----
void onKey(char k) {
  if (isAsleep) wakeFromSleep();

  if (k == '*') {
    // Backspace / history browse
    if (digitsTyped == 0) {
      if (historyIndex < 0) { 
        fetchPayments(); 
        if (historyCount > 0) historyIndex = 0; 
      }
      else if (historyCount > 0) historyIndex = (historyIndex + 1) % historyCount;
      renderPayment();
      return;
    }

    // Backspace a digit
    cents /= 10; 
    digitsTyped--; 
    showAmount("*:Back   #:Enter");
    digitalWrite(LED_GREEN_PIN, LOW);
    currentInvoiceId = ""; 
    invoicePaid = false;

    // amount changed -> reset auto-submit timer
    lastAmountChange = millis();
    autoSubmitDoneForCurrentAmount = false;

    return;
  }

  if (k == '#') {
    digitalWrite(LED_GREEN_PIN, LOW);
    if (currentInvoiceId != "" || invoicePaid) { 
      resetAll(); 
      return; 
    }
    // manual submit: mark as done for this amount
    autoSubmitDoneForCurrentAmount = true;
    submitInvoice();
    return;
  }

  if (k >= '0' && k <= '9') {
    // New digit
    if (currentInvoiceId != "" || invoicePaid) resetAll();
    if (digitsTyped < MAX_DIGITS) {
      uint8_t v = (uint8_t)(k - '0');
      cents = cents * 10 + v;
      digitsTyped++;
      showAmount("*:Back   #:Enter");

      // amount changed -> reset auto-submit timer
      lastAmountChange = millis();
      autoSubmitDoneForCurrentAmount = false;
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(50);

  if (!LittleFS.begin()) Serial.println("LittleFS mount error");

  gpio_deep_sleep_hold_dis();
  for (int i = 0; i < ROWS; ++i) {
    gpio_hold_dis((gpio_num_t)ROW_PINS[i]);
  }

  // (Optional) check why we woke
  auto cause = esp_sleep_get_wakeup_cause();
  if (cause == ESP_SLEEP_WAKEUP_TIMER) {
    // quick maintenance: WiFi on -> check updates -> WiFi off -> sleep again if idle
  }

  // Load creds (token included in file’s 3rd line)
  if (readCredentials(creds)) {
    token = creds.token;
  } else {
    token = "";
  }

  ui_begin();
  nfc_begin();  // PN532 on shared I2C bus (addr 0x24)
  pinMode(LED_GREEN_PIN, OUTPUT);

  // Fetch username from API to derive LNURL for NFC
  if (token.length() > 0 && ensureWifiConnected(creds, 20000)) {
    HTTPClient http;
    http.begin(String(API_HOST) + "/me");
    http.addHeader("Authorization", "Bearer " + token);
    int code = http.GET();
    if (code == HTTP_CODE_OK) {
      StaticJsonDocument<64> filter; filter["username"] = true;
      StaticJsonDocument<256> doc;
      auto err = deserializeJson(doc, http.getString(), DeserializationOption::Filter(filter));
      if (!err && doc["username"]) {
        String username = (const char*)doc["username"];
        merchantLnurl = nfc_deriveLnurl(username);
        Serial.printf("Merchant: %s\n", username.c_str());
      }
    }
    http.end();

    // Prepare websocket for payment notifications (connects on invoice submit)
    onPaymentReceived = handlePayment;
    ws_init(token);
  }

  resetAll();
  lastActivity = millis();
}

void loop() {
  // Fast keypad read
  char key = keypad.getKey();
  if (!key) key = keypad.getKey();
  if (key) { onKey(key); touchActivity(); }

  // Throttled debug
  if (millis() - lastLog >= LOG_INTERVAL) {
    lastLog = millis();
    Serial.print("idle(ms): "); Serial.println(millis() - lastActivity);
  }

  unsigned long now = millis();
  unsigned long idle = now - lastActivity;
  unsigned long timeout = (currentInvoiceId == "")
      ? INACTIVITY_NO_INVOICE_MS
      : INACTIVITY_WITH_INVOICE_MS;

  if (!isAsleep) {
    // Is there an amount on screen that we intend to auto-submit?
    bool pendingAutoSubmit =
      (currentInvoiceId == "" && !invoicePaid &&
       digitsTyped > 0 && !autoSubmitDoneForCurrentAmount);

    // --- AUTO-SUBMIT IF AMOUNT UNCHANGED FOR 5s ---
    if (pendingAutoSubmit &&
        (now - lastAmountChange) >= AUTO_SUBMIT_DELAY_MS) {

      autoSubmitDoneForCurrentAmount = true;
      submitInvoice();
      touchActivity(); // so we don't immediately go to sleep
      // NOTE: after this, currentInvoiceId should be non-empty on success,
      // so pendingAutoSubmit will be false on the next loop iteration.
    }
    // ----------------------------------------------

    // NFC: emulate tag with merchant's LNURL while invoice is active
    if (currentInvoiceId != "" && !invoicePaid && merchantLnurl.length() > 0) {
      nfc_emulate(merchantLnurl.c_str());
      if (nfc_poll()) touchActivity();
    } else {
      nfc_stop();
    }

    if (CM.active) {
      renderCheckmarkFrame();
    } else if (idle >= timeout && !pendingAutoSubmit) {
      // Only sleep if there's no auto-submit waiting to happen
      goToSleep();
    }
  }

  // Process websocket events and send heartbeat
  ws_loop();
  if (wsEnabled && now - lastHeartbeat >= 2000) {
    lastHeartbeat = now;
    ws_heartbeat();
  }

  delay(1);
}
