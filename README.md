<img width="1920" height="1080" alt="Thumb" src="https://github.com/user-attachments/assets/cc8b0510-f485-415c-9a7c-c5acb655181a" />

https://youtu.be/eg8tx8Mla8g?si=LSZyjBFc_pkfXaBK


# Badaple
(IDK someone please name it)

1. Compress Bad Apple full MV into 1.2MB.
2. Flash file using LittleFS.
3. Tada-

# Used hardwares
- ESP32-S3
- 160*128 TFT LCD (ST7735S)

Pins:
- D13 -> SCK
- D11 -> SDA(MOSI)
- D10 -> CS
- D9 -> A0
- D8 -> RESET
- V3V -> VCC & LED(Backlight)
- GND -> GND

# How to upload

1. Prepare a MCU with flash storage >= 1.5MB, decent processor
2. Go to /Feather
3. run `make arduino-install`, which will copy Arduino Library to Arduino library folder. (In macOS/Linux.)
4. open Badaple.ino, save it to Sketch, setup LittleFS, Flash it, Compile it, Run.
