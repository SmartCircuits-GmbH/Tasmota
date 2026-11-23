/*
  xdrv_96_wattwaechter_safeboot_fallback.ino - Boot back to app0 after OTA fail

  SPDX-License-Identifier: GPL-3.0-or-later
  Copyright (C) 2026 SmartCircuits GmbH

  Without this driver, a failed OTA inside the safeboot leaves the device
  stuck in safeboot until somebody intervenes manually. For a fielded
  device this means the customer sees only the safeboot's bare web UI.

  This driver runs only inside the safeboot. It detects the
    "OTA was attempted -> finished -> nothing scheduled a restart"
  pattern and, after a short grace period (30 s) for manual user
  intervention, switches the boot partition back to app0 and reboots.

  If the user manually triggers another upgrade or reboots into app0
  themselves within the grace period, the fallback is skipped.

  The OTA only starts once the network is up. If the safeboot never gets
  a network (e.g. it can't join the Wi-Fi), no OTA is ever attempted and
  the pattern above never happens. So after WWSBF_NO_NET_S without any
  network since boot, the driver also returns to app0 (not while the
  Wi-Fi manager AP is active, the user may be configuring it).

  The safeboot can also be entered without any OTA request: `Upgrade 2`
  (safeboot self-update from the app) makes the new safeboot the boot
  partition, and `Restart 3` / the web UI switch to it on purpose. If the
  network is up but no OTA was started, the driver returns to app0 after
  WWSBF_IDLE_S. The timer starts when the network comes up (a requested
  OTA starts right then) and is held while a web upload is in progress or
  the Wi-Fi manager AP is active, so a manual upload still has time.

  A single short press of the button (Button1) while no OTA is running
  also returns to app0 immediately. Longer holds (factory reset) and
  multi-presses (e.g. 5x Wi-Fi reset) keep their standard behaviour.

  An OTA that breaks off part way through leaves app0 half written, so
  none of the above can return to it. In that case (and when the safeboot
  boots with an invalid app0, e.g. after a power cycle) the driver retries
  `Upgrade 1` from the stored OtaUrl: first WWSBF_RETRY_FIRST_S after the
  network is up, then with growing pauses up to one hour, without limit.
*/

#ifdef WATTWAECHTER_ESP32C6
#ifdef FIRMWARE_SAFEBOOT

#include "esp_ota_ops.h"
#include "esp_image_format.h"

#define XDRV_96  96

// Wait this long after a failed OTA before automatically returning to app0.
#define WWSBF_GRACE_MS  30000
// Return to app0 if the network has not come up at all within this time.
// Generous enough for a router that boots slower after a power outage.
#define WWSBF_NO_NET_S  300
// Return to app0 if the network is up but no OTA was started within this time.
#define WWSBF_IDLE_S    300
// With an invalid app0: first OTA retry this long after the network is up.
#define WWSBF_RETRY_FIRST_S  30
// Pauses between further retries (seconds); the last one repeats.
static const uint16_t wwSbfRetryPause[] = { 120, 300, 600, 1200, 1800, 3600 };

// Safeboot revision marker, found by the app (xdrv_97) by scanning the
// safeboot partition. Logged at init so the linker keeps it.
#define WWSBF_STR2(x)  #x
#define WWSBF_STR(x)   WWSBF_STR2(x)
static const char wwSbfRev[] = "WWSBREV=" WWSBF_STR(WW_SAFEBOOT_REV);

static struct {
  bool     had_ota;            // an OTA attempt has been observed
  bool     fallback_armed;     // grace timer is running
  bool     fallback_done;      // fallback for the last OTA already attempted
  uint32_t fallback_at;        // millis() when fallback should trigger
  bool     net_seen;           // network was up at least once this boot
  bool     no_net_done;        // no-network fallback already attempted
  uint32_t idle_since;         // uptime when the idle timer (re)started
  bool     idle_done;          // idle fallback already attempted
  bool     retry_armed;        // an OTA retry is scheduled
  uint32_t retry_at;           // uptime of the next OTA retry (0 = when network is up)
  uint8_t  retries;            // OTA retries started this boot
} wwSbf;

// Schedule the next `Upgrade 1`, because app0 is not bootable.
static void wwSbfArmRetry(void) {
  uint32_t idx = wwSbf.retries;
  if (idx >= sizeof(wwSbfRetryPause) / sizeof(wwSbfRetryPause[0])) {
    idx = sizeof(wwSbfRetryPause) / sizeof(wwSbfRetryPause[0]) - 1;
  }
  uint32_t pause = wwSbfRetryPause[idx];
  wwSbf.retry_armed = true;
  wwSbf.retry_at = TasmotaGlobal.uptime + pause;
  AddLog(LOG_LEVEL_INFO, PSTR("SBF: app0 not bootable, OTA retry in %d s"), pause);
}

static void wwSbfBootApp0(const char* reason) {
  AddLog(LOG_LEVEL_INFO, PSTR("SBF: %s, returning to app0"), reason);
  const esp_partition_t* partition = esp_ota_get_next_update_partition(nullptr);
  if (!partition) {
    AddLog(LOG_LEVEL_INFO, PSTR("SBF: no app0 partition"));
    return;
  }
  // Also validates the app0 image, so a broken app0 keeps us in safeboot.
  esp_err_t err = esp_ota_set_boot_partition(partition);
  if (err != ESP_OK) {
    AddLog(LOG_LEVEL_INFO, PSTR("SBF: set_boot_partition failed: %d"), err);
    // Half-written app0 (OTA broke off): only a new download gets us out.
    if (!wwSbf.retry_armed) { wwSbfArmRetry(); }
    return;
  }
  TasmotaGlobal.restart_flag = 2;
}

// app0 holds a complete, valid image? Checks the whole image incl. its hash,
// as the boot partition switch does: an OTA that broke off keeps the old
// header (the first block is written last), so the header alone says nothing.
static bool wwSbfApp0Valid(void) {
  const esp_partition_t* partition = esp_ota_get_next_update_partition(nullptr);
  if (!partition) { return false; }
  const esp_partition_pos_t pos = { .offset = partition->address, .size = partition->size };
  esp_image_metadata_t data;
  return (ESP_OK == esp_image_verify(ESP_IMAGE_VERIFY_SILENT, &pos, &data));
}

bool Xdrv96(uint32_t function) {
  switch (function) {
    case FUNC_INIT:
      AddLog(LOG_LEVEL_INFO, PSTR("SBF: %s"), wwSbfRev);
      if (!wwSbfApp0Valid()) {
        // e.g. power cycle after an OTA broke off: retry once the network is up
        AddLog(LOG_LEVEL_INFO, PSTR("SBF: app0 not bootable, OTA retry when network is up"));
        wwSbf.retry_armed = true;
        wwSbf.retry_at = 0;
      }
      break;

    case FUNC_EVERY_SECOND:
      // OTA running? remember and reset any pending fallback.
      if (TasmotaGlobal.ota_state_flag > 0) {
        wwSbf.had_ota = true;
        wwSbf.fallback_armed = false;
        wwSbf.fallback_done = false;
        wwSbf.retry_armed = false;
        break;
      }

      // Already restarting (success path or other reason)? Stand down.
      if (TasmotaGlobal.restart_flag != 0) {
        wwSbf.fallback_armed = false;
        break;
      }

      // app0 not bootable: start the OTA again when its time has come.
      if (wwSbf.retry_armed && !TasmotaGlobal.global_state.network_down) {
        bool busy = WifiIsInManagerMode();
#ifdef USE_WEBSERVER
        busy |= (Web.upload_file_type != UPL_NONE);
#endif  // USE_WEBSERVER
        if (0 == wwSbf.retry_at) {
          wwSbf.retry_at = TasmotaGlobal.uptime + WWSBF_RETRY_FIRST_S;
        }
        if (busy) {
          wwSbf.retry_at = TasmotaGlobal.uptime + WWSBF_RETRY_FIRST_S;
        } else if ((int32_t)(TasmotaGlobal.uptime - wwSbf.retry_at) >= 0) {
          wwSbf.retry_armed = false;
          wwSbf.retries++;
          AddLog(LOG_LEVEL_INFO, PSTR("SBF: OTA retry %d"), wwSbf.retries);
          ExecuteCommand((char*)PSTR("Upgrade 1"), SRC_RULE);
          break;
        }
      }

      // Network never came up -> the OTA can't even start. Don't stay in
      // safeboot forever. Attempted once; a broken app0 keeps us here.
      if (!TasmotaGlobal.global_state.network_down && !wwSbf.net_seen) {
        wwSbf.net_seen = true;
        wwSbf.idle_since = TasmotaGlobal.uptime;
      }
      if (!wwSbf.had_ota && !wwSbf.net_seen && !wwSbf.no_net_done &&
          !WifiIsInManagerMode() && (TasmotaGlobal.uptime >= WWSBF_NO_NET_S)) {
        wwSbf.no_net_done = true;
        wwSbfBootApp0(PSTR("No network"));
        break;
      }

      // Network up but no OTA requested (Upgrade 2, Restart 3, web UI)?
      // Don't stay in safeboot forever. Hold while the user is uploading.
      if (!wwSbf.had_ota && wwSbf.net_seen && !wwSbf.idle_done) {
        bool busy = WifiIsInManagerMode();
#ifdef USE_WEBSERVER
        busy |= (Web.upload_file_type != UPL_NONE);
#endif  // USE_WEBSERVER
        if (busy) {
          wwSbf.idle_since = TasmotaGlobal.uptime;
        } else if ((TasmotaGlobal.uptime - wwSbf.idle_since) >= WWSBF_IDLE_S) {
          wwSbf.idle_done = true;
          wwSbfBootApp0(PSTR("No OTA requested"));
          break;
        }
      }

      // OTA was attempted earlier this boot, has finished and didn't
      // schedule a restart -> assume failure, arm grace timer.
      if (wwSbf.had_ota && !wwSbf.fallback_armed && !wwSbf.fallback_done) {
        wwSbf.fallback_armed = true;
        wwSbf.fallback_at = millis() + WWSBF_GRACE_MS;
        AddLog(LOG_LEVEL_INFO, PSTR("SBF: OTA finished without restart, app0-fallback in %d s"),
               WWSBF_GRACE_MS / 1000);
        break;
      }

      // Grace timer expired -> switch to app0.
      if (wwSbf.fallback_armed && (int32_t)(millis() - wwSbf.fallback_at) >= 0) {
        wwSbf.fallback_armed = false;
        wwSbf.fallback_done = true;   // once per OTA; a broken app0 arms a retry instead
        wwSbfBootApp0(PSTR("OTA failed"));
      }
      break;

    case FUNC_BUTTON_MULTI_PRESSED:
      // Called after the multi-press window closed, with the press count.
      // Single press on Button1 -> leave safeboot. Not during an OTA.
      if ((0 == XdrvMailbox.index) && (1 == XdrvMailbox.payload) &&
          (0 == TasmotaGlobal.ota_state_flag)) {
        wwSbfBootApp0(PSTR("Button pressed"));
        return true;   // serviced, skip default single-press action
      }
      break;

    case FUNC_ACTIVE:
      return true;
  }
  return false;
}

#endif  // FIRMWARE_SAFEBOOT
#endif  // WATTWAECHTER_ESP32C6
