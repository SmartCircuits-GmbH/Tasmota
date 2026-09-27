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

  A single short press of the button (Button1) while no OTA is running
  also returns to app0 immediately. Longer holds (factory reset) and
  multi-presses (e.g. 5x Wi-Fi reset) keep their standard behaviour.
*/

#ifdef WATTWAECHTER_ESP32C6
#ifdef FIRMWARE_SAFEBOOT

#include "esp_ota_ops.h"

#define XDRV_96  96

// Wait this long after a failed OTA before automatically returning to app0.
#define WWSBF_GRACE_MS  30000

static struct {
  bool     had_ota;            // an OTA attempt has been observed
  bool     fallback_armed;     // grace timer is running
  uint32_t fallback_at;        // millis() when fallback should trigger
} wwSbf;

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
    return;
  }
  TasmotaGlobal.restart_flag = 2;
}

bool Xdrv96(uint32_t function) {
  switch (function) {
    case FUNC_EVERY_SECOND:
      // OTA running? remember and reset any pending fallback.
      if (TasmotaGlobal.ota_state_flag > 0) {
        wwSbf.had_ota = true;
        wwSbf.fallback_armed = false;
        break;
      }

      // Already restarting (success path or other reason)? Stand down.
      if (TasmotaGlobal.restart_flag != 0) {
        wwSbf.fallback_armed = false;
        break;
      }

      // OTA was attempted earlier this boot, has finished and didn't
      // schedule a restart -> assume failure, arm grace timer.
      if (wwSbf.had_ota && !wwSbf.fallback_armed) {
        wwSbf.fallback_armed = true;
        wwSbf.fallback_at = millis() + WWSBF_GRACE_MS;
        AddLog(LOG_LEVEL_INFO, PSTR("SBF: OTA finished without restart, app0-fallback in %d s"),
               WWSBF_GRACE_MS / 1000);
        break;
      }

      // Grace timer expired -> switch to app0.
      if (wwSbf.fallback_armed && (int32_t)(millis() - wwSbf.fallback_at) >= 0) {
        wwSbf.fallback_armed = false;
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
