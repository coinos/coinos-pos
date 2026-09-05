// NFC Reader mode test sketch
// PN532 acts as reader, phone's HCE acts as tag
// Reads NDEF URL from phone via ISO 14443-4 APDU exchange

#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <LittleFS.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Adafruit_PN532.h>

// ---- Config ----
#define I2C_SDA_PIN  6
#define I2C_SCL_PIN  7
#define NFC_IRQ_PIN  -1
#define CHARGE_SATS  1  // amount to charge per tap (sats)

// ---- Credentials (from LittleFS /config.txt) ----
struct Credentials { String ssid, wifikey, username, token; };
Credentials creds;
String apiHost;

// ---- NFC ----
Adafruit_PN532 pn532(NFC_IRQ_PIN, -1, &Wire);
bool nfcAvailable = false;
String lastUrl;

// ---- NFC RF Optimization ----
void nfc_boost_range() {
    // RFConfiguration cmd 0x32, CfgItem 0x0A: 106 kbps type A analog settings
    // Byte order: RFCfg, GsNOn, CWGsP, ModGsP, Demod, RxThreshold
    uint8_t rfcfg[] = {
        0x32,  // RFConfiguration
        0x0A,  // CfgItem: 106 kbps type A
        0x79,  // CIU_RFCfg: RxGain = 48 dB (max, default ~38 dB)
        0xFF,  // CIU_GsNOn: max N-driver conductance (strongest field)
        0x3F,  // CIU_CWGsP: max P-driver CW conductance
        0x11,  // CIU_ModGsP: default modulation conductance
        0x41,  // CIU_Demod: default
        0x85,  // CIU_RxThreshold: default
    };
    if (pn532.sendCommandCheckAck(rfcfg, sizeof(rfcfg), 100)) {
        pn532.waitready(100);
        uint8_t buf[8];
        pn532.readdata(buf, sizeof(buf));
        Serial.println("NFC: RF boosted (RxGain=48dB, max field strength)");
    } else {
        Serial.println("NFC: RF boost failed");
    }
}

// ---- NFC APDU exchange (manual, avoids library's 64-byte buffer limit) ----
bool nfc_apdu(const uint8_t* send, uint8_t sendLen, uint8_t* resp, uint8_t* respLen) {
    uint8_t cmd[sendLen + 2];
    cmd[0] = 0x40;  // PN532_COMMAND_INDATAEXCHANGE
    cmd[1] = 0x01;  // Target number
    memcpy(cmd + 2, send, sendLen);

    if (!pn532.sendCommandCheckAck(cmd, sendLen + 2, 1000)) return false;
    if (!pn532.waitready(2000)) return false;

    uint8_t buf[128];
    pn532.readdata(buf, sizeof(buf));

    // Frame: buf[5]=TFI(D5), buf[6]=CMD(41), buf[7]=status
    if (buf[5] != 0xD5 || buf[6] != 0x41 || buf[7] != 0x00) return false;

    uint8_t dataLen = buf[3] - 3;
    uint8_t maxLen = *respLen;
    if (dataLen > maxLen) dataLen = maxLen;
    memcpy(resp, buf + 8, dataLen);
    *respLen = dataLen;
    return true;
}

// ---- NFC Reader Poll ----
bool nfc_poll() {
    if (!nfcAvailable) return false;

    uint8_t uid[7];
    uint8_t uidLen;

    // Poll for ISO 14443-4 target (phone with HCE)
    if (!pn532.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLen, 2000))
        return false;

    Serial.print("Phone detected, UID: ");
    for (uint8_t i = 0; i < uidLen; i++) Serial.printf("%02X ", uid[i]);
    Serial.println();

    uint8_t resp[120];
    uint8_t respLen;

    // 1. SELECT NDEF Application (AID: D2760000850101)
    const uint8_t selAid[] = {0x00,0xA4,0x04,0x00,0x07,
                               0xD2,0x76,0x00,0x00,0x85,0x01,0x01, 0x00};
    respLen = sizeof(resp);
    if (!nfc_apdu(selAid, sizeof(selAid), resp, &respLen)) {
        Serial.println("SELECT AID failed"); return false;
    }
    if (respLen < 2 || resp[respLen-2] != 0x90) {
        Serial.println("SELECT AID rejected"); return false;
    }

    // 2. SELECT NDEF file (E1 04)
    const uint8_t selFile[] = {0x00,0xA4,0x00,0x0C,0x02, 0xE1,0x04};
    respLen = sizeof(resp);
    if (!nfc_apdu(selFile, sizeof(selFile), resp, &respLen)) {
        Serial.println("SELECT NDEF file failed"); return false;
    }

    // 3. READ BINARY: 2 bytes at offset 0 → NDEF message length
    const uint8_t rdLen[] = {0x00,0xB0,0x00,0x00,0x02};
    respLen = sizeof(resp);
    if (!nfc_apdu(rdLen, sizeof(rdLen), resp, &respLen)) {
        Serial.println("READ length failed"); return false;
    }
    if (respLen < 4) { Serial.println("READ length too short"); return false; }
    uint16_t ndefMsgLen = (resp[0] << 8) | resp[1];
    Serial.printf("NDEF message: %d bytes\n", ndefMsgLen);
    if (ndefMsgLen == 0 || ndefMsgLen > 250) return false;

    // 4. READ BINARY: NDEF content in chunks (50 bytes each to stay within I2C limits)
    uint8_t ndefBuf[256];
    uint16_t ndefRead = 0;
    uint16_t offset = 2;  // skip 2-byte length prefix

    while (ndefRead < ndefMsgLen) {
        uint8_t chunk = min((int)(ndefMsgLen - ndefRead), 50);
        uint8_t rdData[] = {0x00, 0xB0, (uint8_t)(offset >> 8), (uint8_t)(offset & 0xFF), chunk};
        respLen = sizeof(resp);
        if (!nfc_apdu(rdData, sizeof(rdData), resp, &respLen)) break;
        if (respLen < chunk + 2) break;  // need data + SW1 SW2
        memcpy(ndefBuf + ndefRead, resp, chunk);
        ndefRead += chunk;
        offset += chunk;
    }

    if (ndefRead < 5) { Serial.println("NDEF read incomplete"); return false; }

    // Parse NDEF URL record: D1 01 LL 55 PP [body...]
    if (ndefBuf[0] == 0xD1 && ndefBuf[3] == 0x55) {
        uint8_t payloadLen = ndefBuf[2];
        uint8_t prefixCode = ndefBuf[4];
        static const char* PREFIXES[] = {
            "", "http://www.", "https://www.", "http://", "https://"
        };
        const char* prefix = (prefixCode < 5) ? PREFIXES[prefixCode] : "";
        int bodyLen = payloadLen - 1;
        if (bodyLen > 0 && bodyLen < 240) {
            char url[300];
            int p = snprintf(url, sizeof(url), "%s", prefix);
            memcpy(url + p, ndefBuf + 5, bodyLen);
            url[p + bodyLen] = '\0';
            lastUrl = String(url);
            Serial.printf("URL from phone: %s\n", url);
            return true;
        }
    }

    // Dump raw NDEF for debugging if parse fails
    Serial.print("Raw NDEF: ");
    for (int i = 0; i < min((int)ndefRead, 30); i++) Serial.printf("%02X ", ndefBuf[i]);
    Serial.println();
    return false;
}

// ---- WiFi ----
bool connectWifi() {
    WiFi.mode(WIFI_STA);
    if (creds.wifikey.length() > 0 && creds.wifikey != "none")
        WiFi.begin(creds.ssid.c_str(), creds.wifikey.c_str());
    else
        WiFi.begin(creds.ssid.c_str());

    Serial.printf("Connecting to %s", creds.ssid.c_str());
    unsigned long t0 = millis();
    while (WiFi.status() != WL_CONNECTED && (millis() - t0) < 20000) {
        Serial.print(".");
        delay(250);
    }
    Serial.println();

    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("Connected! IP: %s\n", WiFi.localIP().toString().c_str());
        return true;
    }
    Serial.println("WiFi connection failed");
    return false;
}

// ---- Read credentials from LittleFS ----
bool readCreds() {
    File f = LittleFS.open("/config.txt", "r");
    if (!f) { Serial.println("No /config.txt"); return false; }
    creds.ssid     = f.readStringUntil('\n'); creds.ssid.trim();
    creds.wifikey  = f.readStringUntil('\n'); creds.wifikey.trim();
    creds.username = f.readStringUntil('\n'); creds.username.trim();
    creds.token    = f.readStringUntil('\n'); creds.token.trim();
    f.close();
    return true;
}

// ---- Create invoice on coinos ----
String createInvoice(int amountSats) {
    HTTPClient http;
    http.begin(apiHost + "/invoice");
    http.addHeader("Content-Type", "application/json");
    http.addHeader("Authorization", "Bearer " + creds.token);

    StaticJsonDocument<512> doc;
    doc["invoice"]["amount"] = amountSats;
    doc["invoice"]["type"] = "lightning";
    doc["user"]["username"] = creds.username;
    String body;
    serializeJson(doc, body);

    Serial.printf("Creating %d sat invoice...\n", amountSats);
    int code = http.POST(body);
    if (code != 200) {
        Serial.printf("Invoice creation failed: %d\n", code);
        http.end();
        return "";
    }

    String resp = http.getString();
    http.end();

    StaticJsonDocument<1024> rdoc;
    deserializeJson(rdoc, resp);
    String bolt11 = rdoc["text"].as<String>();
    Serial.printf("Invoice created: %s...\n", bolt11.substring(0, 40).c_str());
    return bolt11;
}

// ---- LNURL-withdraw flow ----
bool lnurlWithdraw(const String& lnurl, int amountSats) {
    // 1. GET the LNURL URL to get withdrawRequest
    HTTPClient http;
    http.begin(lnurl);
    int code = http.GET();
    if (code != 200) {
        Serial.printf("LNURL request failed: %d\n", code);
        http.end();
        return false;
    }

    String resp = http.getString();
    http.end();

    StaticJsonDocument<512> doc;
    deserializeJson(doc, resp);

    String tag = doc["tag"].as<String>();
    if (tag != "withdrawRequest") {
        Serial.printf("Not a withdrawRequest: %s\n", tag.c_str());
        return false;
    }

    String callback = doc["callback"].as<String>();
    String k1 = doc["k1"].as<String>();
    int maxSats = doc["maxWithdrawable"].as<int>() / 1000;
    Serial.printf("Withdraw available: %d sats (requesting %d)\n", maxSats, amountSats);

    if (amountSats > maxSats) {
        Serial.println("Insufficient funds in tap wallet");
        return false;
    }

    // 2. Create invoice for the charge amount
    String bolt11 = createInvoice(amountSats);
    if (bolt11.length() == 0) return false;

    // 3. Call the callback with k1 and pr
    String cbUrl = callback + "?k1=" + k1 + "&pr=" + bolt11;
    http.begin(cbUrl);
    code = http.GET();
    resp = http.getString();
    http.end();

    StaticJsonDocument<256> cbDoc;
    deserializeJson(cbDoc, resp);
    String status = cbDoc["status"].as<String>();

    if (status == "OK") {
        Serial.printf("Payment received: %d sats\n", amountSats);
        return true;
    } else {
        String reason = cbDoc["reason"].as<String>();
        Serial.printf("Payment failed: %s\n", reason.c_str());
        return false;
    }
}

// ---- Setup ----
void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("\n=== NFC POS Reader ===");

    if (!LittleFS.begin(true)) { Serial.println("LittleFS error"); return; }
    if (!readCreds()) return;

    // Init I2C and PN532
    Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
    Wire.setClock(400000);

    pn532.begin();
    uint32_t ver = pn532.getFirmwareVersion();
    if (!ver) {
        Serial.println("PN532 not found!");
        nfcAvailable = false;
    } else {
        Serial.printf("PN532 fw %d.%d\n", (ver >> 24) & 0xFF, (ver >> 16) & 0xFF);
        pn532.SAMConfig();
        Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
        Wire.setClock(400000);
        nfc_boost_range();
        nfcAvailable = true;
    }

    // Connect WiFi
    if (!connectWifi()) return;

    Serial.printf("User: %s\n", creds.username.c_str());
    Serial.printf("Charge amount: %d sats\n", CHARGE_SATS);
    Serial.println("\nReady — tap phone to pay");
}

// ---- Loop ----
void loop() {
    if (nfcAvailable) {
        if (nfc_poll()) {
            Serial.printf("URL from tap: %s\n", lastUrl.c_str());

            // Derive API host from the LNURL URL
            // e.g. https://staging.coinos.io/api/lnurlw/... → https://staging.coinos.io/api
            int apiIdx = lastUrl.indexOf("/api/");
            if (apiIdx > 0) {
                apiHost = lastUrl.substring(0, apiIdx + 4);
            } else {
                Serial.println("Not a coinos LNURL");
                delay(2000);
                return;
            }

            if (lnurlWithdraw(lastUrl, CHARGE_SATS)) {
                Serial.println("===== PAYMENT SUCCESS =====");
            } else {
                Serial.println("===== PAYMENT FAILED =====");
            }
            delay(3000);  // cooldown before next tap
        }
    }
    delay(1);
}
