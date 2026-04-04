#ifndef MT_WIFI_H
#define MT_WIFI_H

#include <Arduino.h>

typedef enum {
  WIFI_ST_OFF = 0,
  WIFI_ST_CONNECTING,
  WIFI_ST_CONNECTED,
  WIFI_ST_FAILED
} wifi_state_t;

void         MTWiFi_Init(void);
void         MTWiFi_Enable(bool on);
bool         MTWiFi_IsEnabled(void);
wifi_state_t MTWiFi_State(void);
const char*  MTWiFi_StateName(void);
String       MTWiFi_IP(void);
int8_t       MTWiFi_RSSI(void);

void MTWiFi_SetCredentials(const char* ssid, const char* pass);
void MTWiFi_Connect(void);
void MTWiFi_Disconnect(void);

// Leaderboard stubs (backend TBD)
bool MTWiFi_UploadScore(const char* game, uint32_t score);
bool MTWiFi_FetchLeaderboard(const char* game, char* outBuf, size_t bufLen);

#endif // MT_WIFI_H
