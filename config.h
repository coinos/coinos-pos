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

// Keypad
#define ROWS 4
#define COLS 3
static const byte ROW_PINS[ROWS] = {4, 8, 2, 9};
static const byte COL_PINS[COLS] = {10, 5, 3};
static const char KEYS[ROWS][COLS] = {
  {'1','2','3'},
  {'4','5','6'},
  {'7','8','9'},
  {'*','0','#'}
};

// API
static const char* API_HOST = "https://coinos.io/api";

// UX limits
#define MAX_DIGITS           12
#define MAX_LINE             32

// Sleep timeouts
#define INACTIVITY_NO_INVOICE_MS   (30UL * 1000UL)
#define INACTIVITY_WITH_INVOICE_MS (300UL * 1000UL)
