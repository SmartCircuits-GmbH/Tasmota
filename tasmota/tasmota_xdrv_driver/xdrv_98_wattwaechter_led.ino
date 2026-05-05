/*
  xdrv_98_wattwaechter_led.ino - LED system indications for WattWächter Wi-Fi / USB

  SPDX-License-Identifier: GPL-3.0-or-later
  Copyright (C) 2026 SmartCircuits GmbH

  Provides hardware-related LED feedback that is independent of optional
  modules (BLE etc.). Active in both main firmware and safeboot.

  Main firmware states:
    RED      - User holds the button >= 2 s (reset preview).
    MAGENTA  - OTA firmware update in progress.
    CYAN     - WiFi AP / config mode.
    GREEN    - Script active and SML meter data flowing.
    ORANGE   - Script active but no meter data yet.
    YELLOW   - No script enabled.
    OFF      - Default / restart pending.

  Safeboot:
    RED      - Button held for reset.
    MAGENTA  - OTA in progress (this is where the actual download happens
               after the main firmware reboots into safeboot).
    OFF      - Otherwise (no script / SML / WiFi config in safeboot).
*/

#ifdef WATTWAECHTER_ESP32C6

#define XDRV_98  98

// PWM indices for the LED ring on the WattWächter Wi-Fi / USB Gen2:
//   0 = R (PWM1, GPIO2)   3 = R2 (PWM4, GPIO7)   ← mirrored from 0
//   1 = G (PWM2, GPIO5)   4 = G2 (PWM5, GPIO14)  ← mirrored from 1
//   2 = B (PWM3, GPIO4)   5 = B2 (PWM6, GPIO15)  ← mirrored from 2
// Mirror PWM4-6 ← PWM1-3 is done in support_pwm.ino.
static constexpr uint8_t WWLED_R = 0;
static constexpr uint8_t WWLED_G = 1;
static constexpr uint8_t WWLED_B = 2;

// Hold duration after which the red "reset preview" LED appears (ms).
static constexpr uint32_t WWLED_RESET_HINT_MS = 2000;

// LED colour modes (uint8_t, not enum, to keep Arduino's auto-generated
// forward declarations valid across .ino concatenation).
#define WWLED_OFF     0
#define WWLED_RED     1
#define WWLED_CYAN    2
#define WWLED_YELLOW  3
#define WWLED_ORANGE  4
#define WWLED_GREEN   5
#define WWLED_MAGENTA 6

#ifndef FIRMWARE_SAFEBOOT
// Forward declarations for SML helpers (live in xsns_53_sml.ino).
bool SML_HasValidData(void);
uint32_t SML_LastActivityMs(void);
// Brief LED-off pulse window after each received UART byte (ms).
static constexpr uint32_t WWLED_ACTIVITY_PULSE_MS = 80;
#endif

static struct {
  uint32_t press_start;
  uint8_t  current;
} wwLed;

static void wwLedSet(uint8_t color) {
  if (color == wwLed.current) return;
  uint16_t r = 0, g = 0, b = 0;
  switch (color) {
    case WWLED_RED:     r = 102; break;
    case WWLED_CYAN:    g = 102; b = 102; break;
    case WWLED_YELLOW:  r = 102; g = 102; break;
    case WWLED_ORANGE:  r = 200; g = 40;  break;
    case WWLED_GREEN:   g = 102; break;
    case WWLED_MAGENTA: r = 102; b = 102; break;
    case WWLED_OFF:     break;
  }
  TasmotaGlobal.pwm_value[WWLED_R] = r;
  TasmotaGlobal.pwm_value[WWLED_G] = g;
  TasmotaGlobal.pwm_value[WWLED_B] = b;
  PwmApplyGPIO(false);
  wwLed.current = color;
}

bool Xdrv98(uint32_t function) {
  switch (function) {
    case FUNC_BUTTON_PRESSED:
      if (XdrvMailbox.index == 0) {
        if (XdrvMailbox.payload == 0) {
          if (wwLed.press_start == 0) {
            wwLed.press_start = millis();
          }
        } else {
          wwLed.press_start = 0;
        }
      }
      break;

    case FUNC_EVERY_100_MSECOND: {
      uint8_t desired = WWLED_OFF;

      bool button_held_long =
        (wwLed.press_start > 0) &&
        (millis() - wwLed.press_start >= WWLED_RESET_HINT_MS);
      bool restart_pending = (TasmotaGlobal.restart_flag > 0);
      bool ota_in_progress = (TasmotaGlobal.ota_state_flag > 0);

      if (button_held_long && !restart_pending) {
        desired = WWLED_RED;
      } else if (ota_in_progress) {
        desired = WWLED_MAGENTA;
      }
#ifndef FIRMWARE_SAFEBOOT
      // Operating states only meaningful in the main firmware. Safeboot
      // doesn't run a script or SML, so it stays OFF when not in OTA/reset.
      else if (Wifi.config_type != 0) {
        desired = WWLED_CYAN;
      } else if (!bitRead(Settings->rule_enabled, 0)) {
        desired = WWLED_YELLOW;
      } else if (SML_HasValidData()) {
        desired = WWLED_GREEN;
      } else {
        desired = WWLED_ORANGE;
      }

      // SML activity pulse: briefly switch LED off whenever bytes are
      // streaming in from the meter. Only overrides the operating-state
      // colours; reset preview / OTA / AP keep their priority.
      if (desired != WWLED_RED && desired != WWLED_MAGENTA && desired != WWLED_CYAN) {
        uint32_t last = SML_LastActivityMs();
        if (last != 0 && (millis() - last) < WWLED_ACTIVITY_PULSE_MS) {
          desired = WWLED_OFF;
        }
      }
#endif

      wwLedSet(desired);
      break;
    }

    case FUNC_ACTIVE:
      return true;
  }
  return false;
}

#endif  // WATTWAECHTER_ESP32C6
