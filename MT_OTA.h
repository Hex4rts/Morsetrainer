#ifndef MT_OTA_H
#define MT_OTA_H

#include <Arduino.h>

// Check for /firmware.bin on SD card and flash it
// Call early in setup(), after SD_Card_Init()
// Returns true if update was applied (device will reboot)
bool OTA_CheckAndUpdate(void);

#endif
