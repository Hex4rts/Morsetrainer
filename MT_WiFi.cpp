#include "MT_WiFi.h"
#include <WiFi.h>
#include <SD_MMC.h>

static bool         enabled = false;
static wifi_state_t wifiState = WIFI_ST_OFF;
static char         ssid[33]  = "";
static char         pass[65]  = "";

#define WIFI_CFG "/mt_wifi.txt"

void MTWiFi_Init(void) {
  WiFi.mode(WIFI_OFF);
  // Load saved credentials from SD
  if (SD_MMC.exists(WIFI_CFG)) {
    File f = SD_MMC.open(WIFI_CFG, FILE_READ);
    if (f) {
      while (f.available()) {
        String line = f.readStringUntil('\n'); line.trim();
        int eq = line.indexOf('=');
        if (eq < 0) continue;
        String key = line.substring(0, eq);
        String val = line.substring(eq + 1);
        if (key == "ssid") strncpy(ssid, val.c_str(), sizeof(ssid) - 1);
        else if (key == "pass") strncpy(pass, val.c_str(), sizeof(pass) - 1);
      }
      f.close();
    }
  }
  printf("WiFi: init OK  ssid='%s'\n", ssid);
}

void MTWiFi_SetCredentials(const char* s, const char* pw) {
  strncpy(ssid, s, sizeof(ssid) - 1);
  strncpy(pass, pw, sizeof(pass) - 1);
  File f = SD_MMC.open(WIFI_CFG, FILE_WRITE);
  if (f) {
    f.printf("ssid=%s\n", ssid);
    f.printf("pass=%s\n", pass);
    f.close();
  }
}

void MTWiFi_Enable(bool on) {
  if (on && !enabled) {
    enabled = true;
    MTWiFi_Connect();
  } else if (!on && enabled) {
    MTWiFi_Disconnect();
    enabled = false;
  }
}

bool MTWiFi_IsEnabled(void) { return enabled; }

wifi_state_t MTWiFi_State(void) {
  if (!enabled) return WIFI_ST_OFF;
  if (WiFi.status() == WL_CONNECTED) {
    wifiState = WIFI_ST_CONNECTED;
  } else if (wifiState == WIFI_ST_CONNECTING) {
    // still connecting
  } else {
    wifiState = WIFI_ST_OFF;
  }
  return wifiState;
}

const char* MTWiFi_StateName(void) {
  switch (MTWiFi_State()) {
    case WIFI_ST_OFF:        return "Off";
    case WIFI_ST_CONNECTING: return "Connecting...";
    case WIFI_ST_CONNECTED:  return "Connected";
    case WIFI_ST_FAILED:     return "Failed";
    default:                 return "?";
  }
}

String MTWiFi_IP(void) {
  if (WiFi.status() == WL_CONNECTED) return WiFi.localIP().toString();
  return "---";
}

int8_t MTWiFi_RSSI(void) {
  return WiFi.RSSI();
}

void MTWiFi_Connect(void) {
  if (strlen(ssid) == 0) { wifiState = WIFI_ST_FAILED; return; }
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, pass);
  wifiState = WIFI_ST_CONNECTING;
  printf("WiFi: connecting to '%s'\n", ssid);
}

void MTWiFi_Disconnect(void) {
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  wifiState = WIFI_ST_OFF;
}

// ============================================================================
//  Leaderboard stubs  (backend URL TBD)
// ============================================================================
bool MTWiFi_UploadScore(const char* game, uint32_t score) {
  if (MTWiFi_State() != WIFI_ST_CONNECTED) return false;
  // TODO: HTTP POST to leaderboard API
  printf("WiFi: [STUB] would upload %s score=%lu\n", game, score);
  return false;
}

bool MTWiFi_FetchLeaderboard(const char* game, char* outBuf, size_t bufLen) {
  if (MTWiFi_State() != WIFI_ST_CONNECTED) return false;
  // TODO: HTTP GET from leaderboard API
  snprintf(outBuf, bufLen, "Leaderboard not yet available.");
  return false;
}
