#pragma once
#include <WebSocketsClient.h>
#include <ArduinoJson.h>

static WebSocketsClient ws;
static bool wsConnected = false;
static bool wsEnabled = false;
static String wsToken;
static String wsPendingInvoiceId;

// Callback for payment received — set by main sketch
static void (*onPaymentReceived)(long long amount, long long tip) = nullptr;

static void wsSend(const char* type, const char* data) {
    if (!wsConnected) return;
    String msg = "{\"type\":\"" + String(type) + "\",\"data\":" + String(data) + "}";
    ws.sendTXT(msg);
}

static void wsSend(const char* type, const String& jsonData) {
    if (!wsConnected) return;
    String msg = "{\"type\":\"" + String(type) + "\",\"data\":" + jsonData + "}";
    ws.sendTXT(msg);
}

static void wsEvent(WStype_t type, uint8_t* payload, size_t length) {
    switch (type) {
        case WStype_CONNECTED:
            Serial.println("WS connected");
            wsConnected = true;
            wsSend("login", ("\"" + wsToken + "\"").c_str());
            if (wsPendingInvoiceId.length() > 0) {
                wsSend("subscribe", "{\"id\":\"" + wsPendingInvoiceId + "\"}");
            }
            break;

        case WStype_DISCONNECTED:
            Serial.println("WS disconnected");
            wsConnected = false;
            break;

        case WStype_TEXT: {
            StaticJsonDocument<64> filter;
            filter["type"] = true;
            filter["data"]["amount"] = true;
            filter["data"]["tip"] = true;

            StaticJsonDocument<512> doc;
            auto err = deserializeJson(doc, payload, length,
                                       DeserializationOption::Filter(filter));
            if (err) break;

            const char* msgType = doc["type"];
            if (!msgType) break;

            if (strcmp(msgType, "payment") == 0 && onPaymentReceived) {
                long long amount = doc["data"]["amount"] | 0LL;
                long long tip = doc["data"]["tip"] | 0LL;
                if (amount > 0) onPaymentReceived(amount, tip);
            }
            break;
        }

        default:
            break;
    }
}

inline void ws_init(const String& token) {
    wsToken = token;
    ws.onEvent(wsEvent);
    ws.setReconnectInterval(5000);
}

inline void ws_connect() {
    if (wsEnabled) return;
    ws.beginSSL("coinos.io", 443, "/ws");
    wsEnabled = true;
}

inline void ws_disconnect() {
    if (!wsEnabled) return;
    ws.disconnect();
    wsConnected = false;
    wsEnabled = false;
}

inline void ws_loop() {
    if (!wsEnabled) return;
    ws.loop();
}

inline void ws_subscribe(const String& invoiceId) {
    wsPendingInvoiceId = invoiceId;
    if (wsConnected) {
        wsSend("subscribe", "{\"id\":\"" + invoiceId + "\"}");
    }
}

inline void ws_heartbeat() {
    wsSend("heartbeat", ("\"" + wsToken + "\"").c_str());
}
