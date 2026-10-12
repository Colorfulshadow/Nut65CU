// Copyright 2024 sdk66 (@sdk66)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#define USB_POWER_EN_PIN                    B1 // USB ENABLE pin
#define LED_POWER_EN_PIN                    A8 // LED ENABLE pin
#define HS_BAT_CABLE_PIN                    A7 // USB insertion detection pin
#define HS_LED_BOOSTING_PIN                 A9 // LED BOOSTING

#define BAT_FULL_PIN                        A15
#define BAT_FULL_STATE                      1

// #define MM_BT_DEF_PIN                       C15
// #define MM_2G4_DEF_PIN                      C14
// Names used by the stock NUT65 CU firmware
#define MD_BT1_NAME                         "NUT65 CU BT1"
#define MD_BT2_NAME                         "NUT65 CU BT2"
#define MD_BT3_NAME                         "NUT65 CU BT3"
#define MD_DONGLE_PRODUCT                   "NUT65 CU 2.4G"

/* LED chain (read from the stock NUT65 CU firmware's g_led_config):
 * 0 T, 1 R, 2 E, 3 W, 4 Q, 5 Caps, 6 LCtrl, 7 Win, 8-87 light bar.
 * The CU has no per-key LEDs, so feedback that the NUT65 blinks on a key
 * (brightness/speed limits, Win/Mac, debounce, music) flashes the light bar
 * instead: those blink entries use pseudo indices >= HS_RGB_STRIP_BLINK_BASE,
 * which nut65cu.c paints across the bar. */
#define HS_RGB_INDICATOR_COUNT              8
#define HS_RGB_STRIP_START                  HS_RGB_INDICATOR_COUNT
#define HS_RGB_STRIP_BLINK_BASE             240

/* Device Connection RGB Indicator Light Index And Color */
#define HS_RGB_BLINK_INDEX_BT1              4
#define HS_RGB_BLINK_INDEX_BT2              3
#define HS_RGB_BLINK_INDEX_BT3              2
#define HS_RGB_BLINK_INDEX_2G4              1
#define HS_RGB_BLINK_INDEX_USB              0

#define WIITE_B                             0X30

#define HS_LBACK_COLOR_BT1                  RGB_BLUE
#define HS_LBACK_COLOR_BT2                  RGB_BLUE
#define HS_LBACK_COLOR_BT3                  RGB_BLUE
#define HS_LBACK_COLOR_2G4                  RGB_GREEN
#define HS_LBACK_COLOR_USB                  RGB_WHITE

#define HS_PAIR_COLOR_BT1                   RGB_BLUE
#define HS_PAIR_COLOR_BT2                   RGB_BLUE
#define HS_PAIR_COLOR_BT3                   RGB_BLUE
#define HS_PAIR_COLOR_2G4                   RGB_GREEN
#define HS_PAIR_COLOR_USB                   RGB_WHITE

/* Battery */
#define BATTERY_CAPACITY_LOW                15
#define BATTERY_CAPACITY_STOP               0
// Battery query (HS_BATQ) shows 10 segments of 8 LEDs on the light bar (see query() in rgb_record.c)

#define HS_DEBOUNCE
#define USB_POWER_DOWN_DELAY 7000

/* Status Indicator Lamp */
#define RGB_MATRIX_BLINK_INDEX_ALL          0xFF 
#define RGB_MATRIX_BLINK_INDEX_BAT          250
#define RGB_MATRIX_BLINK_INDEX_IND          251
#define RGB_MATRIX_BLINK_COUNT              10

#define HS_MATRIX_BLINK_INDEX_BAT           6  // LCtrl
#define HS_RGB_INDEX_CAPS                   5
#define HS_RGB_INDEX_WIN_LOCK               7
#define HS_RGB_BLINK_INDEX_REC              0  // T (light recording has no keys to show on the CU)
#define HS_RGB_BLINK_INDEX_FREQ             0  // T
#define HS_RGB_BLINK_INDEX_VAI              (HS_RGB_STRIP_BLINK_BASE + 0)
#define HS_RGB_BLINK_INDEX_VAD              (HS_RGB_STRIP_BLINK_BASE + 1)
#define HS_RGB_BLINK_INDEX_SPI              (HS_RGB_STRIP_BLINK_BASE + 2)
#define HS_RGB_BLINK_INDEX_SPD              (HS_RGB_STRIP_BLINK_BASE + 3)
#define HS_RGB_BLINK_INDEX_DEB              (HS_RGB_STRIP_BLINK_BASE + 4)
#define HS_RGB_BLINK_INDEX_SYS_TEM          (HS_RGB_STRIP_BLINK_BASE + 5)
#define HS_RGB_BLINK_INDEX_MUSIC            (HS_RGB_STRIP_BLINK_BASE + 6)

/* MUSIC */
#define HS_MUSIC_CUT_PIN B6
#define HS_MUSIC_ON_STATE 1
#define HS_MUSIC_MODE_PIN B7
#define HS_MUSIC_DEFAULT_MODE_STATE 0
#define HS_MUSIC_DEFAULT_TIMER 80
#define HS_MUSIC_CALIBRATION_TIMER 200

/*  ENCODER */
#define HS_ENCODERS_PAD_A B6
#define HS_ENCODERS_PAD_B B7
#define ENCODER_RESOLUTION  2
#define ENCODERS_PAD_B_INT_DISABLE

/*  IIC INIT */
#define I2C_DRIVER        I2CD1
#define I2C1_OPMODE       OPMODE_I2C
#define I2C1_CLOCK_SPEED  100000

/* UART */
#define SERIAL_DRIVER                       SD3
#define SD1_TX_PIN                          C10
#define SD1_RX_PIN                          C11

/* SPI */
#define SPI_DRIVER                          SPIDQ
#define SPI_SCK_PIN                         B3
#define SPI_MOSI_PIN                        B5
#define SPI_MISO_PIN                        B4

/* Flash */
#define EXTERNAL_FLASH_SPI_SLAVE_SELECT_PIN C12
#define WEAR_LEVELING_LOGICAL_SIZE          (WEAR_LEVELING_BACKING_SIZE / 2)
#define VIA_EEPROM_ALLOW_RESET 
#define DYNAMIC_KEYMAP_MACRO_COUNT 32

/* RGB Matrix */
#define WS2812_BYTE_ORDER WS2812_BYTE_ORDER_GRB
// #define RGB_TRIGGER_ON_KEYDOWN

/* RGB Light */
#define RGBLED_NUM 80
#define RGBLIGHT_CUSTOM_DRIVER
#define RGBLIGHT_DISABLE_KEYCODES
#define BREATHING_VAL           30
#define RAINBOW_MOOD            30
#define LED_TYPE                RGB
#define RGBLIGHT_LIMIT_VAL      55
#define RGBLIGHT_LED_MAP {0, 1, 2, 3, 4, 5, 6, 7, 8}

/* WS2812 */
#define WS2812_T1H 660
#define WS2812_T0H 330

#define WS2812_SPI_DRIVER       SPIDM2
#define WS2812_SPI_DIVISOR      32

#define RGB_MATRIX_LED_COUNT (HS_RGB_INDICATOR_COUNT + RGBLED_NUM)

/* rgb_record */
#define ENABLE_RGB_MATRIX_RGBR_PLAY
// Light recording does nothing on the CU (no per-key LEDs). It had 4 channels; the
// 4th channel's EEPROM is now the CU extension block, placed first, so the datablock
// size and every address after it (confinfo, CU settings, VIA keymaps) stay the same.
#define RGBREC_CHANNEL_NUM         3
#define EECONFIG_CU_EXT_SIZE       (MATRIX_ROWS * MATRIX_COLS * 2) // cu_ext_t (cu_profile.h), 180 bytes
#define EECONFIG_CONFINFO_USE_SIZE (4 + 25 + 1)
#define EECONFIG_RGBREC_USE_SIZE   (RGBREC_CHANNEL_NUM * MATRIX_ROWS * MATRIX_COLS * 2)
#define EECONFIG_CU_USE_SIZE       8 // cu_settings_t (cu_features.h)
#define EECONFIG_USER_DATA_SIZE    (EECONFIG_CU_EXT_SIZE + EECONFIG_RGBREC_USE_SIZE + EECONFIG_CONFINFO_USE_SIZE + EECONFIG_CU_USE_SIZE)
// Marks EEPROM written by this firmware. QMK's default version is just the block
// size, which the NUT65 / packOrTake builds share, so their settings were read as ours.
// Bump this when the user data layout changes; a mismatch factory-resets once (matrix_init_kb).
// (The CU extension block has its own magic, so adding it did not need a bump.)
#define EECONFIG_USER_DATA_VERSION 0x4E435502 // "NCU" v2: + CU settings block
#define CU_EXT_EECONFIG_ADDR       (void *)(EECONFIG_USER_DATABLOCK)
#define RGBREC_EECONFIG_ADDR       (uint8_t *)((uint32_t)EECONFIG_USER_DATABLOCK + (uint32_t)EECONFIG_CU_EXT_SIZE)
#define CONFINFO_EECONFIG_ADDR     (uint32_t *)((uint32_t)RGBREC_EECONFIG_ADDR + (uint32_t)EECONFIG_RGBREC_USE_SIZE)
#define CU_EECONFIG_ADDR           (void *)((uint32_t)CONFINFO_EECONFIG_ADDR + (uint32_t)EECONFIG_CONFINFO_USE_SIZE)

/* External SPI flash beyond the EEPROM (cu_store.c) */
// Wear leveling erases the whole first 64 KB block, though it only uses 8 KB of it.
#define CU_STORE_KEYS_BASE         0x10000 // key press counters, 64 KB
#define CU_STORE_KEYS_SIZE         0x10000
#define CU_STORE_LOG_BASE          0x20000 // event log, to the end of the chip