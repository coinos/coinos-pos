#pragma once

// ===== Hardware config =====
#define LED_GREEN_PIN      20          // your existing pin

// I2C / OLED
#define I2C_SDA_PIN        6
#define I2C_SCL_PIN        7
#define I2C_ADDRESS        0x3C
#define SCREEN_WIDTH       132
#define SCREEN_HEIGHT      64
#define OLED_RESET_PIN     -1

// Keypad. Deep sleep wakes on a COLUMN pulled low, and on the ESP32-C3 only
// GPIO0-5 can wake the chip, so every column should live on one of those.
//   BOARD_REV 1: the first PCB run. Column 0 (keys 1 4 7 *) is on GPIO10
//     and cannot wake the terminal from sleep.
//   BOARD_REV 2: pcb/pos.kicad_sch from 2026-09-07 on — keypad pin 4 and
//     pin 7 swapped between GPIO2 and GPIO10, so every key wakes it.
#ifndef BOARD_REV
#define BOARD_REV 1
#endif
#define ROWS 4
#define COLS 3
#if BOARD_REV >= 2
static const byte ROW_PINS[ROWS] = {4, 8, 10, 9};
static const byte COL_PINS[COLS] = {2, 5, 3};
#else
static const byte ROW_PINS[ROWS] = {4, 8, 2, 9};
static const byte COL_PINS[COLS] = {10, 5, 3};
#endif
static const char KEYS[ROWS][COLS] = {
  {'1','2','3'},
  {'4','5','6'},
  {'7','8','9'},
  {'*','0','#'}
};

// API — the coinos v3 name registrar's POS endpoints (names/server.js in
// coinosv3). The token in config.txt is a POS token minted by the merchant's
// wallet (Settings → payment address → Point of sale), scoped to one name:
// it can ring up sales and watch them settle, never spend.
static const char* API_HOST = "https://names.coinos.io/pos";
#define POS_WS_HOST "names.coinos.io"
#define POS_WS_PATH "/pos/ws"
#define POS_LNURL_BASE "https://names.coinos.io/lnurlp/"

// UX limits
#define MAX_DIGITS           12
#define MAX_LINE             32

// Sleep timeouts
#define INACTIVITY_NO_INVOICE_MS   (600UL * 1000UL)
#define INACTIVITY_WITH_INVOICE_MS (300UL * 1000UL)
