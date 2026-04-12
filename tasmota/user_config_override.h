/*
  user_config_override.h - user configuration overrides my_user_config.h for Tasmota

  Copyright (C) 2021  Theo Arends

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef _USER_CONFIG_OVERRIDE_H_
#define _USER_CONFIG_OVERRIDE_H_

/*****************************************************************************************************\
 * USAGE:
 *   To modify the stock configuration without changing the my_user_config.h file:
 *   (1) copy this file to "user_config_override.h" (It will be ignored by Git)
 *   (2) define your own settings below
 *
 ******************************************************************************************************
 * ATTENTION:
 *   - Changes to SECTION1 PARAMETER defines will only override flash settings if you change define CFG_HOLDER.
 *   - Expect compiler warnings when no ifdef/undef/endif sequence is used.
 *   - You still need to update my_user_config.h for major define USE_MQTT_TLS.
 *   - All parameters can be persistent changed online using commands via MQTT, WebConsole or Serial.
\*****************************************************************************************************/

/*****************************************************************************************************\
 * WattWächter Tasmota Firmware Konfiguration
 ******************************************************************************************************
 * Zwei Varianten:
 *
 * 1) WattWächter Wifi/USB  (ESP8285, 1M Flash)
 *    Kompilieren: platformio run -e wattwaechter_wifi_usb
 *
 * 2) WattWächter.tasmota   (ESP32-C6-Mini, 4M Flash)
 *    Kompilieren: platformio run -e wattwaechter_esp32c6
 *
 * Beide Varianten sind optimiert für SML-Smartmeter-Auslesung mit IR-Lesekopf.
 * Unnötige Features sind deaktiviert, um Firmware schlank und stabil zu halten.
\*****************************************************************************************************/

// ============================================================================
// Gemeinsame Konfiguration: WattWächter Wifi/USB + WattWächter.tasmota
// (siehe platformio_tasmota_cenv.ini für die Build-Environments)
// ============================================================================
#if defined(WATTWAECHTER_WIFI_USB) || defined(WATTWAECHTER_ESP32C6)

// ---- Projekt-Name ----------------------------------------------------------
#undef  PROJECT
#define PROJECT                "WattWaechter"

#undef  MODULE
#define MODULE                 WEMOS

// ---- Unnötige Features deaktivieren ----------------------------------------
// Domoticz & Emulation
#undef USE_DOMOTICZ
#undef USE_EMULATION_HUE
#undef USE_EMULATION_WEMO

// Dimmer, Licht, LED-Treiber
#undef ROTARY_V1
#undef USE_LIGHT
#undef USE_WS2812
#undef USE_MY92X1
#undef USE_SM16716
#undef USE_SM2135
#undef USE_SM2335
#undef USE_BP1658CJ
#undef USE_BP5758D
#undef USE_SONOFF_L1
#undef USE_ELECTRIQ_MOODL
#undef USE_LIGHT_PALETTE
#undef USE_LIGHT_VIRTUAL_CT
#undef USE_DGR_LIGHT_SEQUENCE
#undef USE_NETWORK_LIGHT_SCHEMES
#undef USE_ARILUX_RF
#undef USE_ARMTRONIX_DIMMERS
#undef USE_PS_16_DZ
#undef USE_EXS_DIMMER
#undef USE_PWM_DIMMER
#undef USE_SONOFF_D1
#undef USE_SHELLY_DIMMER
#undef SHELLY_CMDS
#undef SHELLY_FW_UPGRADE

// Sonoff-spezifische Treiber
#undef USE_SONOFF_RF
#undef USE_SONOFF_SC
#undef USE_SONOFF_IFAN
#undef USE_TUYA_MCU

// Sonstige unnötige Features
#undef USE_BUZZER
#undef USE_SHUTTER
#undef USE_DEVICE_GROUPS
#undef USE_SERIAL_BRIDGE
#undef USE_IR_REMOTE

// Energiesensoren (wir nutzen SML, keine eingebauten Messmodule)
#undef USE_ENERGY_SENSOR
#undef USE_ENERGY_DUMMY
#undef USE_HLW8012
#undef USE_CSE7766
#undef USE_CSE7761
#undef USE_BL09XX
#undef USE_PZEM004T
#undef USE_PZEM_AC
#undef USE_PZEM_DC
#undef USE_MCP39F501

// I2C und Sensoren (nicht benötigt für reinen SML-Lesekopf)
#undef USE_I2C
#undef USE_DHT
#undef USE_DS18x20
#undef USE_DEEPSLEEP
#undef USE_ADC

// ESP32-spezifische Features die nicht benötigt werden
#undef USE_GPIO_VIEWER
#undef GV_USE_ESPINFO
#undef USE_BERRY
#undef USE_AUTOCONF

// ---- Button-Timing ---------------------------------------------------------
// 7s Halten = Factory-Reset, 5× Drücken = WiFi-Reset (AP-Modus)
// HOLD-Event bei 3.5s (unsichtbar), Factory-Reset bei 3.5s × 2 = 7s
#undef  KEY_HOLD_TIME
#define KEY_HOLD_TIME          35                // 35 × 0.1s = 3.5s (HOLD-Event, keine sichtbare Wirkung)

// ---- Aktive Features -------------------------------------------------------

// KNX Support
#ifndef USE_KNX
#define USE_KNX
#endif

// Script & SML - Kernfunktionalität für Smartmeter-Auslesung
#ifndef USE_SCRIPT
#define USE_SCRIPT
#endif
#ifndef USE_SML_M
#define USE_SML_M
#endif
#define USE_SML_CRC
#ifdef USE_RULES
#undef USE_RULES             // USE_SCRIPT & USE_RULES können nicht gleichzeitig aktiv sein
#endif

// Web-Anzeige für Messwerte
#ifndef USE_SCRIPT_WEB_DISPLAY
#define USE_SCRIPT_WEB_DISPLAY
#endif
#define USE_GOOGLE_CHARTS
#define LARGE_ARRAYS

// Prometheus Metrics Endpoint
#ifndef USE_PROMETHEUS
#define USE_PROMETHEUS
#endif

// Home Assistant Integration
#define USE_HOME_ASSISTANT
#define USE_WEBCLIENT_HTTPS

// SML Auth-Key Support (für Zähler die Authentifizierung benötigen)
#define USE_SML_AUTHKEY
#define USE_TLS

// HTML Callback für SML-Descriptor Dropdown
#define USE_HTML_CALLBACK

// Kalenderwochen-Variable
#define USE_CW_CALC

// Script JSON Export (>J Sektion)
#define USE_SCRIPT_JSON_EXPORT

// Script globale Variablen
#define USE_SCRIPT_GLOBVARS

// ============================================================================
// ESP8266-spezifisch: WattWächter Wifi/USB (1M Flash)
// ============================================================================
#if defined(WATTWAECHTER_WIFI_USB)

  #undef  FALLBACK_MODULE
  #define FALLBACK_MODULE        WEMOS
  #define USER_TEMPLATE "{\"NAME\":\"WattWächter Wi-Fi / USB\",\"GPIO\":[0,1,0,1,1,1,0,0,1,0,1,0,0,0],\"FLAG\":0,\"BASE\":18,\"CMND\":\"Module 0\"}"

  // Skript wird komprimiert in Settings->rules[0] gespeichert (Standard-Tasmota-Verhalten)
  // Kompatibel mit Standard-Tasmota-Images, kein Sonder-Flash-Sektor.

#endif // WATTWAECHTER_WIFI_USB

// ============================================================================
// ESP32-C6-spezifisch: WattWächter.tasmota (8M Flash)
// ============================================================================
#if defined(WATTWAECHTER_ESP32C6)

  #undef  FALLBACK_MODULE
  #define FALLBACK_MODULE        WEMOS
  // GPIO-Belegung WattWächter.tasmota (ESP32-C6-Mini):
  //   GPIO1=SML_TX, GPIO3=SML_RX (IR-Lesekopf, Pin-Zuweisung im Script)
  //   GPIO2=PWM1(LED1_R), GPIO5=PWM2(LED1_G), GPIO4=PWM3(LED1_B)
  //   GPIO7=PWM4(LED2_R), GPIO14=PWM5(LED2_G), GPIO15=PWM6(LED2_B)
  //   PWM4-6 werden automatisch von PWM1-3 gespiegelt (support_pwm.ino)
  //   GPIO9=Button1 (7s halten=Factory-Reset, 5× drücken=WiFi-Reset)
  //   GPIO24-30=Flash (reserviert)
  //                                                                    PWM1   PWM3PWM2  PWM4      Btn1        PWM5PWM6
  #define USER_TEMPLATE "{\"NAME\":\"WattWächter.tasmota\",\"GPIO\":[1,1,416,1,418,417,1,419,1,32,1,1,1,1,420,421,1,1,1,1,1,1,1,1,0,0,0,0,0,0,0],\"FLAG\":0,\"BASE\":1,\"CMND\":\"Module 0\"}"

  // Stack-Size erhöhen (Empfehlung seit Core3)
  #undef  SET_ESP32_STACK_SIZE
  #define SET_ESP32_STACK_SIZE (12 * 1024)

  // Filesystem für Script-Speicher
  #define USE_SCRIPT_FATFS_EXT
  #define USE_UFILESYS
  #undef  UFSYS_SIZE
  #define UFSYS_SIZE 16384

  // Größere Variablennamen-Puffer und Arrays (24h-Diagramme etc.)
  #define SCRIPT_LARGE_VNBUFF
  #define MAX_ARRAY_SIZE 2000

  // Erweiterte Script-Features (genug Flash auf ESP32-C6)
  #define USE_ANGLE_FUNC
  #define USE_FEXTRACT
  #define USE_ESP32_SW_SERIAL
  #define USE_SCRIPT_SERIAL
  #define SCRIPT_FULL_WEBPAGE
  #define USE_MQTT_TLS

  // TCP-Server & Task Support
  #define USE_SCRIPT_TCP_SERVER
  #define USE_SCRIPT_TASK

  // mDNS für Shelly Pro 3EM Emulation (z.B. für smarte Akkus)
  #define USE_SCRIPT_MDNS

#endif // WATTWAECHTER_ESP32C6

#endif // WATTWAECHTER_WIFI_USB || WATTWAECHTER_ESP32C6


#endif  // _USER_CONFIG_OVERRIDE_H_
