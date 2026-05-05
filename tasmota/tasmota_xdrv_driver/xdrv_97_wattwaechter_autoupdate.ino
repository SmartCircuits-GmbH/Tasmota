/*
  xdrv_97_wattwaechter_autoupdate.ino - Monthly OTA auto-update

  SPDX-License-Identifier: GPL-3.0-or-later
  Copyright (C) 2026 SmartCircuits GmbH

  Optional monthly firmware self-update. When enabled, the device
  triggers `Upgrade 1` against the configured OtaUrl roughly every
  WWAU_INTERVAL_DAYS once per device-specific quiet hour (3-5 a.m.).
  Update window (hour:minute) is derived deterministically from the
  base MAC so a fleet of devices spreads its update load instead of
  hammering the server simultaneously.

  Console:
    AutoUpdate         - show current state, update window, last run
    AutoUpdate 0       - disable
    AutoUpdate 1       - enable (default on)
    AutoUpdate 2       - trigger an upgrade NOW (debug / manual recovery,
                         bypasses both interval and time-of-day window)

  Persistence: NVS namespace "ww_au"
    enabled : 1 byte  (0 = off, 1 = on)
    last    : 4 bytes (UTC timestamp of last triggered update)
*/

#ifdef WATTWAECHTER_ESP32C6
#ifndef FIRMWARE_SAFEBOOT

#include <esp_mac.h>

#define XDRV_97  97

// Days between auto-update attempts.
#define WWAU_INTERVAL_DAYS  30

static struct {
  uint8_t  enabled = 1;      // 0 = off, 1 = on (default on)
  uint8_t  hour;             // 3..5 (derived from MAC)
  uint8_t  minute;           // 0..59 (derived from MAC)
  uint32_t last_utc;         // UTC timestamp of last triggered upgrade
  uint32_t triggered_ms;     // millis() when last triggered (boot-local debounce)
} wwAu;

static void wwAuLoadFromNvs(void) {
  if (NvmExists("ww_au")) {
    NvmLoad("ww_au", "en",   &wwAu.enabled,  sizeof(wwAu.enabled));
    NvmLoad("ww_au", "last", &wwAu.last_utc, sizeof(wwAu.last_utc));
  }
  // Stagger update window across the fleet using base MAC.
  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_BASE);
  wwAu.hour   = 3 + (mac[5] % 3);   // 3, 4 or 5
  wwAu.minute = mac[5] % 60;
}

static void wwAuSaveEnabled(void) {
  NvmSave("ww_au", "en", &wwAu.enabled, sizeof(wwAu.enabled));
}

static void wwAuSaveLast(void) {
  NvmSave("ww_au", "last", &wwAu.last_utc, sizeof(wwAu.last_utc));
}

void CmndAutoUpdate(void) {
  if (XdrvMailbox.data_len > 0) {
    if (XdrvMailbox.payload == 0 || XdrvMailbox.payload == 1) {
      wwAu.enabled = (uint8_t)XdrvMailbox.payload;
      wwAuSaveEnabled();
    } else if (XdrvMailbox.payload == 2) {
      // Force-trigger an upgrade right now, ignoring interval & time window.
      // Useful for testing and for "update me right now" requests.
      AddLog(LOG_LEVEL_INFO, PSTR("AU: Forced upgrade triggered"));
      wwAu.last_utc = UtcTime();
      wwAu.triggered_ms = millis();
      wwAuSaveLast();
      // ExecuteCommand will produce its own {"Upgrade":...} response; we
      // do not write our own response here because that would clobber
      // XdrvMailbox after Upgrade has already overwritten it.
      ExecuteCommand((char*)PSTR("Upgrade 1"), SRC_RULE);
      return;
    }
  }
  Response_P(PSTR("{\"AutoUpdate\":{\"State\":\"%s\",\"Hour\":%d,\"Minute\":%d,\"LastUtc\":%u}}"),
             wwAu.enabled ? "ON" : "OFF",
             wwAu.hour, wwAu.minute, wwAu.last_utc);
}

const char kWwAuCommands[] PROGMEM = "|" "AutoUpdate";
void (* const WwAuCommand[])(void) PROGMEM = { &CmndAutoUpdate };

bool Xdrv97(uint32_t function) {
  bool result = false;
  switch (function) {
    case FUNC_PRE_INIT:
      wwAuLoadFromNvs();
      break;

    case FUNC_EVERY_SECOND:
      if (!wwAu.enabled) break;
      if (!RtcTime.valid) break;
      // Fire only inside our deterministic update minute, second 0.
      if (RtcTime.hour != wwAu.hour) break;
      if (RtcTime.minute != wwAu.minute) break;
      if (RtcTime.second != 0) break;
      // Wait until interval has elapsed since last trigger.
      {
        uint32_t now = UtcTime();
        if (wwAu.last_utc != 0 &&
            (now - wwAu.last_utc) < (WWAU_INTERVAL_DAYS * 86400UL)) {
          break;
        }
        // Boot-local debounce so we don't fire twice if FUNC_EVERY_MINUTE
        // gets called twice within the same minute.
        if (wwAu.triggered_ms != 0 && (millis() - wwAu.triggered_ms) < 60000) {
          break;
        }
        AddLog(LOG_LEVEL_INFO, PSTR("AU: Triggering monthly auto-update"));
        wwAu.triggered_ms = millis();
        wwAu.last_utc = now;
        wwAuSaveLast();
      }
      ExecuteCommand((char*)PSTR("Upgrade 1"), SRC_RULE);
      break;

    case FUNC_COMMAND:
      result = DecodeCommand(kWwAuCommands, WwAuCommand);
      break;

    case FUNC_ACTIVE:
      result = true;
      break;
  }
  return result;
}

#endif  // !FIRMWARE_SAFEBOOT
#endif  // WATTWAECHTER_ESP32C6
