#pragma once
#include <Wire.h>
#include <Adafruit_PN532.h>
#include "config.h"

// PN532 shares I2C bus with OLED (different addresses: PN532=0x24, OLED=0x3C)
// Wiring: SDA→GPIO6, SCL→GPIO7, IRQ→GPIO1, VCC→3.3V, GND→GND
#define NFC_IRQ_PIN 1

static Adafruit_PN532 pn532(NFC_IRQ_PIN, -1, &Wire);
static bool nfcAvailable = false;
static bool nfcEmulating = false;

// Current NDEF message being broadcast
static uint8_t ndefFile[256];
static uint16_t ndefFileLen = 0;

// ---- Bech32 encoding ----
static const char BECH32_CHARSET[] = "qpzry9x8gf2tvdw0s3jn54khce6mua7l";

static uint32_t bech32_polymod(const uint8_t* values, size_t len) {
    static const uint32_t GEN[] = {0x3b6a57b2, 0x26508e6d, 0x1ea119fa, 0x3d4233dd, 0x2a1462b3};
    uint32_t chk = 1;
    for (size_t i = 0; i < len; i++) {
        uint8_t b = chk >> 25;
        chk = ((chk & 0x1ffffff) << 5) ^ values[i];
        for (int j = 0; j < 5; j++) {
            if ((b >> j) & 1) chk ^= GEN[j];
        }
    }
    return chk;
}

static size_t bech32_convert_bits(const uint8_t* in, size_t inLen,
                                   uint8_t* out, size_t outMax,
                                   int fromBits, int toBits) {
    uint32_t acc = 0;
    int bits = 0;
    size_t pos = 0;
    for (size_t i = 0; i < inLen; i++) {
        acc = (acc << fromBits) | in[i];
        bits += fromBits;
        while (bits >= toBits) {
            bits -= toBits;
            if (pos < outMax) out[pos++] = (acc >> bits) & ((1 << toBits) - 1);
        }
    }
    if (bits > 0 && pos < outMax) {
        out[pos++] = (acc << (toBits - bits)) & ((1 << toBits) - 1);
    }
    return pos;
}

// Encode URL as LNURL (bech32 with hrp "lnurl")
static String bech32_encode_lnurl(const char* url) {
    const char* hrp = "lnurl";
    size_t hrpLen = 5;
    size_t urlLen = strlen(url);

    // Convert 8-bit URL bytes to 5-bit values
    uint8_t data5[512];
    size_t data5Len = bech32_convert_bits((const uint8_t*)url, urlLen, data5, sizeof(data5), 8, 5);

    // Build polymod input: hrp_expand + data + 6 zeros
    size_t pmLen = hrpLen * 2 + 1 + data5Len + 6;
    uint8_t pmValues[pmLen];
    size_t p = 0;
    for (size_t i = 0; i < hrpLen; i++) pmValues[p++] = hrp[i] >> 5;
    pmValues[p++] = 0;
    for (size_t i = 0; i < hrpLen; i++) pmValues[p++] = hrp[i] & 31;
    memcpy(pmValues + p, data5, data5Len); p += data5Len;
    memset(pmValues + p, 0, 6); p += 6;

    uint32_t polymod = bech32_polymod(pmValues, p) ^ 1;
    uint8_t checksum[6];
    for (int i = 0; i < 6; i++) checksum[i] = (polymod >> (5 * (5 - i))) & 31;

    // Build result: hrp + "1" + encoded_data + checksum
    String result = String(hrp) + "1";
    for (size_t i = 0; i < data5Len; i++) result += BECH32_CHARSET[data5[i]];
    for (int i = 0; i < 6; i++) result += BECH32_CHARSET[checksum[i]];

    return result;
}

// Derive full LNURL from username
static String nfc_deriveLnurl(const String& username) {
    String url = "https://coinos.io/p/" + username;
    return "https://coinos.io/ln/" + bech32_encode_lnurl(url.c_str());
}

// Capability Container (read-only tag, 2048-byte max NDEF)
static const uint8_t CC_FILE[] = {
    0x00, 0x0F,             // CC length
    0x20,                   // Mapping version 2.0
    0x00, 0xF6,             // Max R-APDU size (246)
    0x00, 0x00,             // Max C-APDU size (0 = no write)
    0x04, 0x06,             // NDEF File Control TLV
    0xE1, 0x04,             // NDEF file ID
    0x08, 0x00,             // Max NDEF file size (2048)
    0x00,                   // Read access: open
    0xFF                    // Write access: denied
};

static void nfc_buildNdefUrl(const char* url) {
    // Determine URI prefix code
    uint8_t prefixCode = 0x00;
    const char* body = url;

    struct { const char* prefix; uint8_t code; } prefixes[] = {
        {"https://www.", 0x02}, {"http://www.", 0x01},
        {"https://",    0x04}, {"http://",    0x03},
    };
    for (auto& p : prefixes) {
        size_t len = strlen(p.prefix);
        if (strncmp(url, p.prefix, len) == 0) {
            prefixCode = p.code;
            body = url + len;
            break;
        }
    }

    uint8_t bodyLen = strlen(body);
    uint8_t payloadLen = 1 + bodyLen;  // prefix code + body

    // NDEF record: MB|ME|SR, type="U", payload=prefix+body
    uint8_t ndef[4 + payloadLen];
    ndef[0] = 0xD1;             // MB|ME|SR|TNF=Well-Known
    ndef[1] = 0x01;             // Type length
    ndef[2] = payloadLen;       // Payload length
    ndef[3] = 0x55;             // Type: "U"
    ndef[4] = prefixCode;
    memcpy(ndef + 5, body, bodyLen);

    uint16_t ndefMsgLen = 4 + payloadLen;

    // NDEF file = 2-byte length prefix + NDEF message
    ndefFile[0] = (ndefMsgLen >> 8) & 0xFF;
    ndefFile[1] = ndefMsgLen & 0xFF;
    memcpy(ndefFile + 2, ndef, ndefMsgLen);
    ndefFileLen = 2 + ndefMsgLen;

    Serial.printf("NFC NDEF set: %s (%d bytes)\n", url, ndefFileLen);
}

inline bool nfc_begin() {
    pn532.begin();
    uint32_t ver = pn532.getFirmwareVersion();
    if (!ver) {
        Serial.println("PN532 not found - continuing without NFC");
        nfcAvailable = false;
        return false;
    }
    Serial.printf("PN532 fw %d.%d\n", (ver >> 24) & 0xFF, (ver >> 16) & 0xFF);
    pn532.SAMConfig();

    // Restore I2C pins in case PN532 lib re-initialized Wire
    Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
    Wire.setClock(100000);

    nfcAvailable = true;
    return true;
}

// Set the URL to broadcast and start tag emulation
inline void nfc_emulate(const char* url) {
    if (!nfcAvailable) return;
    if (!nfcEmulating) {
        nfc_buildNdefUrl(url);
        nfcEmulating = true;
    }
}

inline void nfc_stop() {
    nfcEmulating = false;
}

// Handle one emulation cycle — call from loop()
// Uses PN532 tgInitAsTarget to act as a Type 4 Tag
// Returns true if a phone successfully read the tag
inline bool nfc_poll() {
    if (!nfcAvailable || !nfcEmulating || ndefFileLen == 0) return false;

    // Configure as ISO14443-4 passive target (Type 4 Tag)
    // SENS_RES, NFCID1, SEL_RES=0x60 (indicates Type 4 Tag support)
    static const uint8_t targetParams[] = {
        0x05,                   // Mode: PICC only, passive only
        0x04, 0x00,             // SENS_RES (ATQA)
        0x01, 0x02, 0x03,       // NFCID1 (3 bytes)
        0x60,                   // SEL_RES (SAK): 0x60 = Type 4 Tag
        // Felica params (unused but required)
        0x01, 0xFE, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7,
        0xC0, 0xC1, 0xC2, 0xC3, 0xC4, 0xC5, 0xC6, 0xC7,
        0xFF, 0xFF,
        // NFCID3t
        0xAA, 0x99, 0x88, 0x77, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11,
        // General bytes length
        0x00,
        // Historical bytes length
        0x00
    };

    // tgInitAsTarget — blocks until a reader connects (with timeout)
    uint8_t cmd[sizeof(targetParams) + 1];
    cmd[0] = PN532_COMMAND_TGINITASTARGET;
    memcpy(cmd + 1, targetParams, sizeof(targetParams));

    if (!pn532.sendCommandCheckAck(cmd, sizeof(cmd), 50)) return false;

    // Check if a reader activated us (non-blocking via IRQ)
    if (!pn532.waitready(50)) return false;

    uint8_t resp[64];
    uint8_t respLen = sizeof(resp);
    pn532.readdata(resp, respLen);

    // Now we're activated as a target — handle APDU exchange
    bool success = false;
    uint8_t apdu[64];
    uint8_t apduLen;

    enum { SEL_NONE, SEL_CC, SEL_NDEF } selected = SEL_NONE;

    for (int i = 0; i < 20; i++) {  // max 20 APDU exchanges
        apduLen = sizeof(apdu);
        if (!pn532.tgGetData(apdu, &apduLen)) break;
        if (apduLen < 4) break;

        uint8_t ins = apdu[1];
        uint8_t p1 = apdu[2];

        uint8_t reply[200];
        uint8_t replyLen = 0;

        if (ins == 0xA4) {
            // SELECT command
            if (p1 == 0x04 && apduLen >= 12) {
                // SELECT by AID — check for NDEF AID D2760000850101
                static const uint8_t NDEF_AID[] = {0xD2,0x76,0x00,0x00,0x85,0x01,0x01};
                uint8_t aidLen = apdu[4];
                if (aidLen == 7 && memcmp(apdu + 5, NDEF_AID, 7) == 0) {
                    selected = SEL_NONE;
                    reply[0] = 0x90; reply[1] = 0x00;
                    replyLen = 2;
                } else {
                    reply[0] = 0x6A; reply[1] = 0x82;
                    replyLen = 2;
                }
            } else if (p1 == 0x00 && apduLen >= 7) {
                // SELECT by File ID
                uint8_t fid0 = apdu[5], fid1 = apdu[6];
                if (fid0 == 0xE1 && fid1 == 0x03) {
                    selected = SEL_CC;
                } else if (fid0 == 0xE1 && fid1 == 0x04) {
                    selected = SEL_NDEF;
                } else {
                    reply[0] = 0x6A; reply[1] = 0x82;
                    replyLen = 2;
                    pn532.tgSetData(reply, replyLen);
                    continue;
                }
                reply[0] = 0x90; reply[1] = 0x00;
                replyLen = 2;
            }
        } else if (ins == 0xB0) {
            // READ BINARY
            uint16_t offset = ((uint16_t)apdu[2] << 8) | apdu[3];
            uint8_t readLen = apdu[4];

            const uint8_t* fileData = nullptr;
            uint16_t fileLen = 0;

            if (selected == SEL_CC) {
                fileData = CC_FILE;
                fileLen = sizeof(CC_FILE);
            } else if (selected == SEL_NDEF) {
                fileData = ndefFile;
                fileLen = ndefFileLen;
            }

            if (fileData && offset < fileLen) {
                uint16_t avail = fileLen - offset;
                uint8_t n = (readLen < avail) ? readLen : avail;
                memcpy(reply, fileData + offset, n);
                reply[n] = 0x90;
                reply[n + 1] = 0x00;
                replyLen = n + 2;

                // If we just served the NDEF data, the read was successful
                if (selected == SEL_NDEF && offset >= 2) success = true;
            } else {
                reply[0] = 0x6A; reply[1] = 0x82;
                replyLen = 2;
            }
        } else {
            // Unsupported
            reply[0] = 0x6A; reply[1] = 0x81;
            replyLen = 2;
        }

        if (replyLen > 0) {
            if (!pn532.tgSetData(reply, replyLen)) break;
        }
    }

    if (success) Serial.println("NFC: phone read the tag");
    return success;
}
