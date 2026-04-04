#include "MT_OTA.h"
#include <SD_MMC.h>
#include <Update.h>

#define OTA_FILE      "/firmware.bin"
#define OTA_DONE_FILE "/firmware.done"  // renamed after successful flash

bool OTA_CheckAndUpdate(void) {
  if (!SD_MMC.exists(OTA_FILE)) return false;

  File fw = SD_MMC.open(OTA_FILE, FILE_READ);
  if (!fw) {
    printf("OTA: failed to open %s\n", OTA_FILE);
    return false;
  }

  size_t fwSize = fw.size();
  if (fwSize == 0) {
    printf("OTA: firmware file is empty\n");
    fw.close();
    return false;
  }

  printf("OTA: found %s (%d bytes), starting update...\n", OTA_FILE, fwSize);

  if (!Update.begin(fwSize)) {
    printf("OTA: not enough space for update\n");
    fw.close();
    return false;
  }

  // Flash in 4KB chunks
  uint8_t buf[4096];
  size_t written = 0;
  while (fw.available()) {
    size_t toRead = fw.available();
    if (toRead > sizeof(buf)) toRead = sizeof(buf);
    size_t bytesRead = fw.read(buf, toRead);
    if (bytesRead == 0) break;
    size_t bytesWritten = Update.write(buf, bytesRead);
    if (bytesWritten != bytesRead) {
      printf("OTA: write error at offset %d\n", written);
      Update.abort();
      fw.close();
      return false;
    }
    written += bytesWritten;
    // Progress
    int pct = (written * 100) / fwSize;
    if (written % 32768 < 4096) printf("OTA: %d%%\n", pct);
  }
  fw.close();

  if (!Update.end(true)) {
    printf("OTA: update finalize failed: %s\n", Update.errorString());
    return false;
  }

  printf("OTA: update successful! %d bytes written\n", written);

  // Rename firmware.bin to firmware.done so it doesn't flash again on reboot
  SD_MMC.rename(OTA_FILE, OTA_DONE_FILE);

  printf("OTA: rebooting...\n");
  delay(500);
  ESP.restart();

  return true;  // never reached
}
