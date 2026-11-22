/*
  xdrv_97_wattwaechter_autoupdate.ino - Monthly OTA auto-update

  SPDX-License-Identifier: GPL-3.0-or-later
  Copyright (C) 2026 SmartCircuits GmbH

  Optional monthly firmware self-update (on by default). When enabled, the device
  triggers `Upgrade 1` against the configured OtaUrl roughly every
  WWAU_INTERVAL_DAYS once per device-specific quiet hour (3-5 a.m.).
  Update window (hour:minute) is derived deterministically from the
  base MAC so a fleet of devices spreads its update load instead of
  hammering the server simultaneously.

  Safeboot self-update: a normal OTA only replaces app0, so fielded
  devices keep their old safeboot. Shortly after boot the app scans the
  safeboot partition for its "WWSBREV=n" marker (xdrv_96). If it is
  missing or older than WW_SAFEBOOT_REV, the next update window without a
  due app update runs `Upgrade 2` (fetches <OtaUrl dir>/firmware-safeboot.bin).
  A failed attempt (e.g. file missing on the server) writes nothing and is
  retried in the next night; after success the device reboots into the app
  and the scan finds the new rev.

  mDNS: with USE_DISCOVERY, SetOption55 is switched on once at boot (NVS
  "ww_au"/"mdns"), so fielded devices become discoverable by the app.

  Console:
    AutoUpdate         - show current state, update window, last run
    AutoUpdate 0       - disable
    AutoUpdate 1       - enable (default on). The first update follows
                         WWAU_INTERVAL_DAYS after the first update window,
                         not in the first night after flashing
    AutoUpdate 2       - trigger an upgrade NOW (debug / manual recovery,
                         bypasses both interval and time-of-day window)
    AutoUpdate 3       - update the safeboot NOW (Upgrade 2), regardless
                         of its revision

  Persistence: NVS namespace "ww_au"
    enabled : 1 byte  (0 = off, 1 = on; missing = on)
    last    : 4 bytes (UTC timestamp of last triggered update)
*/

#ifdef WATTWAECHTER_ESP32C6
#ifndef FIRMWARE_SAFEBOOT

#include <esp_mac.h>
#include <esp_partition.h>

#define XDRV_97  97

// Days between auto-update attempts.
#define WWAU_INTERVAL_DAYS  30
// Scan the safeboot partition this many seconds after boot (keeps boot fast).
#define WWAU_SB_SCAN_S      30

// Safeboot revision states
#define WWAU_SB_UNKNOWN  -2  // not scanned yet
#define WWAU_SB_NONE     -1  // no safeboot partition: nothing to update

static struct {
  uint8_t  enabled = 1;      // 0 = off, 1 = on (default on)
  uint8_t  hour;             // 3..5 (derived from MAC)
  uint8_t  minute;           // 0..59 (derived from MAC)
  uint32_t last_utc;         // UTC timestamp of last triggered upgrade
  uint32_t triggered_ms;     // millis() when last triggered (boot-local debounce)
  int16_t  sb_rev = WWAU_SB_UNKNOWN;  // safeboot rev, 0 = no marker / invalid image
} wwAu;

// Find "WWSBREV=<n>" inside the safeboot image. Only the bytes covered by the
// image's segments are searched, so stale data behind a shorter image can't
// produce a false match. Returns n, 0 if no marker or no valid image (= needs
// an update), WWAU_SB_NONE if there is no safeboot partition.
static int16_t wwAuScanSafebootRev(void) {
  const esp_partition_t* part = esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_FACTORY, nullptr);
  if (!part) { return WWAU_SB_NONE; }

  // Image length from the header: 24 byte image header, then per segment
  // an 8 byte header (load address, data length) followed by its data.
  uint8_t hdr[24];
  if ((esp_partition_read(part, 0, hdr, sizeof(hdr)) != ESP_OK) || (hdr[0] != 0xE9)) { return 0; }
  uint32_t img_len = sizeof(hdr);
  for (uint32_t i = 0; i < hdr[1]; i++) {
    uint32_t seg[2];
    if ((img_len + sizeof(seg) > part->size) ||
        (esp_partition_read(part, img_len, seg, sizeof(seg)) != ESP_OK)) { return 0; }
    img_len += sizeof(seg) + seg[1];
    if (img_len > part->size) { return 0; }
  }

  static const char tag[] = "WWSBREV=";
  const uint32_t tlen = sizeof(tag) - 1;
  const uint32_t chunk = 2048;
  const uint32_t keep = 16;             // overlap so a marker across chunks is found
  uint8_t* buf = (uint8_t*)malloc(chunk + keep);
  if (!buf) { return WWAU_SB_UNKNOWN; } // retry later
  int16_t rev = 0;
  uint32_t have = 0;
  for (uint32_t off = 0; off < img_len; off += chunk) {
    uint32_t n = (img_len - off < chunk) ? img_len - off : chunk;
    if (esp_partition_read(part, off, buf + have, n) != ESP_OK) { rev = 0; break; }
    uint32_t len = have + n;
    bool last = (off + n >= img_len);
    bool found = false;
    for (uint32_t i = 0; i + tlen <= len; i++) {
      if (memcmp(buf + i, tag, tlen) != 0) { continue; }
      // Digits may be cut at the chunk end; the overlap finds it again next round.
      if (!last && (i + tlen + 5 > len)) { break; }
      int16_t v = 0;
      uint32_t j = i + tlen;
      while ((j < len) && (j < i + tlen + 4) && isdigit(buf[j])) { v = v * 10 + (buf[j++] - '0'); }
      if (j > i + tlen) { rev = v; found = true; }
      break;
    }
    if (found) { break; }
    if (len > keep) {
      memmove(buf, buf + len - keep, keep);
      have = keep;
    }
    if (0 == (off & 0xFFFF)) { yield(); }
  }
  free(buf);
  return rev;
}

static bool wwAuSafebootOutdated(void) {
  return (wwAu.sb_rev >= 0) && (wwAu.sb_rev < WW_SAFEBOOT_REV);
}

static void wwAuTriggerSafeboot(void) {
  AddLog(LOG_LEVEL_INFO, PSTR("AU: Safeboot rev %d < %d, triggering safeboot update"),
         wwAu.sb_rev, WW_SAFEBOOT_REV);
  wwAu.triggered_ms = millis();
  ExecuteCommand((char*)PSTR("Upgrade 2"), SRC_RULE);
}

static void wwAuLoadFromNvs(void) {
  if (NvmExists("ww_au")) {
    NvmLoad("ww_au", "en",   &wwAu.enabled,  sizeof(wwAu.enabled));
    NvmLoad("ww_au", "last", &wwAu.last_utc, sizeof(wwAu.last_utc));
  }
#ifdef USE_DISCOVERY
  // Fielded devices keep SetOption55 0 from their old settings. Switch mDNS
  // on once so the app can find them; a later SetOption55 0 by the user stays.
  uint8_t mdns_done = 0;
  NvmLoad("ww_au", "mdns", &mdns_done, sizeof(mdns_done));
  if (!mdns_done) {
    if (!Settings->flag3.mdns_enabled) {
      Settings->flag3.mdns_enabled = 1;
      AddLog(LOG_LEVEL_INFO, PSTR("AU: mDNS enabled (SetOption55 1)"));
    }
    mdns_done = 1;
    NvmSave("ww_au", "mdns", &mdns_done, sizeof(mdns_done));
  }
#endif  // USE_DISCOVERY
  // Stagger update window across the fleet: one of 180 minutes (03:00-05:59)
  // derived from the device-specific MAC bytes. Hash them (FNV-1a) because
  // Espressif base MACs are allocated in blocks of 4 (mac[5] % 4 == 0), so
  // plain modulo on the raw bytes would only hit a fraction of the slots.
  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_BASE);
  uint32_t h = 2166136261UL;
  for (uint32_t i = 3; i < 6; i++) {
    h = (h ^ mac[i]) * 16777619UL;
  }
  uint32_t slot = h % 180;
  wwAu.hour   = 3 + (slot / 60);    // 3, 4 or 5
  wwAu.minute = slot % 60;
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
    } else if (XdrvMailbox.payload == 3) {
      // Force a safeboot update right now (Upgrade 2 writes its own response).
      AddLog(LOG_LEVEL_INFO, PSTR("AU: Forced safeboot update triggered"));
      wwAu.triggered_ms = millis();
      ExecuteCommand((char*)PSTR("Upgrade 2"), SRC_RULE);
      return;
    }
  }
  Response_P(PSTR("{\"AutoUpdate\":{\"State\":\"%s\",\"Hour\":%d,\"Minute\":%d,\"LastUtc\":%u,"
                  "\"SafebootRev\":%d,\"SafebootRequired\":%d}}"),
             wwAu.enabled ? "ON" : "OFF",
             wwAu.hour, wwAu.minute, wwAu.last_utc,
             wwAu.sb_rev, WW_SAFEBOOT_REV);
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
      if ((WWAU_SB_UNKNOWN == wwAu.sb_rev) && (TasmotaGlobal.uptime >= WWAU_SB_SCAN_S)) {
        wwAu.sb_rev = wwAuScanSafebootRev();
        AddLog(LOG_LEVEL_INFO, PSTR("AU: Safeboot rev %d (required %d)"), wwAu.sb_rev, WW_SAFEBOOT_REV);
      }
      if (!wwAu.enabled) break;
      if (!RtcTime.valid) break;
      // Fire only inside our deterministic update minute, second 0.
      if (RtcTime.hour != wwAu.hour) break;
      if (RtcTime.minute != wwAu.minute) break;
      if (RtcTime.second != 0) break;
      // Boot-local debounce so we don't fire twice within the same minute.
      if (wwAu.triggered_ms != 0 && (millis() - wwAu.triggered_ms) < 60000) {
        break;
      }
      {
        uint32_t now = UtcTime();
        bool app_due = false;
        if (0 == wwAu.last_utc) {
          // Never ran (fresh flash wipes NVS): start the interval now
          // instead of updating in the very first night.
          wwAu.last_utc = now;
          wwAuSaveLast();
        } else {
          app_due = ((now - wwAu.last_utc) >= (WWAU_INTERVAL_DAYS * 86400UL));
        }
        if (app_due) {
          // A due app update goes first, so a safeboot image missing on the
          // server can never block app updates.
          AddLog(LOG_LEVEL_INFO, PSTR("AU: Triggering monthly auto-update"));
          wwAu.triggered_ms = millis();
          wwAu.last_utc = now;
          wwAuSaveLast();
          ExecuteCommand((char*)PSTR("Upgrade 1"), SRC_RULE);
        } else if (wwAuSafebootOutdated()) {
          // Any other night: replace an outdated safeboot. A failed attempt
          // (e.g. 404) writes nothing, the app keeps running and retries
          // in the next night.
          wwAuTriggerSafeboot();
        }
      }
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
