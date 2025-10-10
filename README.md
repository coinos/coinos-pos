# Coinos Point of Sale Terminal

This device is intended to be used by retail merchants who want to take Bitcoin payments.

When you enter a sale amount in dollars on the device, it will call the Coinos API to generate an invoice.

Once the invoice is paid, the device lights up and display an on-screen notification. Simple!

This project was inspired by https://github.com/lnbits/lnpos but the software and hardware is all a bit different.

## Parts

[Xiao Seeed ESP32 C3 Development Board](https://www.aliexpress.com/item/1005006979844970.html)
[3x4 Matrix Switch Keyboard](https://www.aliexpress.com/item/1005007094229478.html)
[1.54" 4PIN OLED](https://www.aliexpress.com/item/1005009132302307.html)
[1800mAH LiPo Battery](https://www.aliexpress.com/item/1005009445456523.html)

Design files for the custom PCB and case are included in this repository.

## Setup

Firmware can be installed with a USB C cable either using Arduino IDE or arduino-cli or visit https://coinos.io/pos to flash from the web.

The latest firmware image can be found under [Releases](https://github.com/coinos/coinos-pos/releases)
