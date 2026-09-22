# Coinos Point of Sale Terminal

This device is intended to be used by retail merchants who want to take Bitcoin payments.

When you enter a sale amount in dollars on the device, it will call the Coinos API to generate an invoice.

Once the invoice is paid, the device lights up and displays a checkmark.

You can scroll through past payments and see if tips were added.

This project was inspired by https://github.com/lnbits/lnpos but the software and hardware is different.

## Parts

- [Xiao Seeed ESP32 C3 Development Board](https://www.aliexpress.com/item/1005006979844970.html)
- [3x4 Matrix Switch Keyboard](https://www.aliexpress.com/item/1005007094229478.html)
- [1.54" 4PIN OLED](https://www.aliexpress.com/item/1005009132302307.html)
- [1800mAH LiPo Battery](https://www.aliexpress.com/item/1005009445456523.html)

Design files for the custom PCB and case are included in this repository.

## Setup

Firmware can be installed with a USB C cable either using Arduino IDE or arduino-cli or visit https://coinos.io/flash to flash from the web. The web flasher's "Flash latest release" button installs the firmware published at https://coinos.io/firmware/manifest.json without touching the device's wifi/token config.

To build and publish a new release to the site, run `./publish.sh` (expects a coinos-ui checkout at `~/coinos-ui` or `$COINOS_UI`), then commit and deploy coinos-ui.

Build locally with:

```
arduino-cli compile -b esp32:esp32:esp32c3:CDCOnBoot=cdc --build-property build.partitions=min_spiffs --build-path=build coinos-pos.ino -u -p /dev/ttyACM0
```

`CDCOnBoot=cdc` routes `Serial` to the XIAO's USB-C port so `cat /dev/ttyACM0` shows the device log.
