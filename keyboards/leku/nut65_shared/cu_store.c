// Copyright 2026 Colorfulshadow (@Colorfulshadow)
// SPDX-License-Identifier: GPL-2.0-or-later
//
// External SPI flash outside the wear-leveled EEPROM. QMK's flash driver (flash_spi.c)
// only has blocking erases, so sector erases are issued here directly and left to
// finish in the background; flash_read_range / flash_write_range wait for them.

#include QMK_KEYBOARD_H
#include "spi_master.h"
#include "flash_spi.h"
#include "cu_store.h"

#define CMD_WREN  0x06
#define CMD_SE    0x20
#define CMD_JEDEC 0x9F

static cu_store_state_t state = CU_STORE_NONE;
static uint32_t         jedec_id;
static uint32_t         chip_size;
static bool             erasing;

static bool cmd_begin(void) {
    return spi_start(EXTERNAL_FLASH_SPI_SLAVE_SELECT_PIN, EXTERNAL_FLASH_SPI_LSBFIRST, EXTERNAL_FLASH_SPI_MODE, EXTERNAL_FLASH_SPI_CLOCK_DIVISOR);
}

static uint32_t read_jedec_id(void) {
    if (!cmd_begin()) return 0;
    spi_write(CMD_JEDEC);
    uint32_t id = 0;
    for (uint8_t i = 0; i < 3; i++) {
        spi_status_t b = spi_read();
        id             = (id << 8) | (b < 0 ? 0 : (uint8_t)b);
    }
    spi_stop();
    return id;
}

void cu_store_init(void) {
    jedec_id = read_jedec_id();
    // Capacity byte is log2(size) on the usual SPI NOR parts (0x13 = 512 KB).
    uint8_t cap = jedec_id & 0xFF;
    if (jedec_id == 0 || jedec_id == 0xFFFFFF || cap < 0x12 || cap > 0x18) {
        state = CU_STORE_NONE;
        return;
    }
    chip_size = 1ul << cap;
    // The log needs room past the key counters.
    state = chip_size > CU_STORE_LOG_BASE ? CU_STORE_OK : CU_STORE_NONE;
    if (state == CU_STORE_NONE) chip_size = 0;
}

cu_store_state_t cu_store_state(void) {
    return state;
}

uint32_t cu_store_jedec_id(void) {
    return jedec_id;
}

uint32_t cu_store_size(void) {
    return chip_size;
}

void cu_store_lock(void) {
    if (state == CU_STORE_OK) state = CU_STORE_LOCKED;
}

static bool in_regions(uint32_t addr, uint32_t len) {
    return state != CU_STORE_NONE && addr >= CU_STORE_KEYS_BASE && addr + len <= chip_size && addr + len >= addr;
}

bool cu_store_read(uint32_t addr, void *buf, uint32_t len) {
    if (state == CU_STORE_NONE || addr + len > chip_size) return false;
    erasing = false; // flash_read_range waits for the erase
    return flash_read_range(addr, buf, len) == FLASH_STATUS_SUCCESS;
}

bool cu_store_write(uint32_t addr, const void *buf, uint32_t len) {
    if (!in_regions(addr, len)) return false;
    erasing = false;
    return flash_write_range(addr, buf, len) == FLASH_STATUS_SUCCESS;
}

bool cu_store_is_blank(uint32_t addr, uint32_t len) {
    uint8_t buf[32];
    while (len) {
        uint32_t n = len < sizeof(buf) ? len : sizeof(buf);
        if (!cu_store_read(addr, buf, n)) return false;
        for (uint32_t i = 0; i < n; i++) {
            if (buf[i] != 0xFF) return false;
        }
        addr += n;
        len -= n;
    }
    return true;
}

void cu_store_wait(void) {
    if (!erasing) return;
    uint32_t start = timer_read32();
    while (flash_is_busy() == FLASH_STATUS_BUSY && timer_elapsed32(start) < 1000) {
    }
    erasing = false;
}

bool cu_store_erase_async(uint32_t addr) {
    addr -= addr % CU_STORE_SECTOR;
    if (!in_regions(addr, CU_STORE_SECTOR)) return false;
    cu_store_wait();

    if (!cmd_begin()) return false;
    spi_write(CMD_WREN);
    spi_stop();

    if (!cmd_begin()) return false;
    uint8_t cmd[4] = {CMD_SE, addr >> 16, addr >> 8, addr};
    spi_transmit(cmd, sizeof(cmd));
    spi_stop();
    erasing = true;
    return true;
}

bool cu_store_erase(uint32_t addr) {
    if (!cu_store_erase_async(addr)) return false;
    cu_store_wait();
    return true;
}

void cu_store_task(void) {
    if (erasing && flash_is_busy() != FLASH_STATUS_BUSY) erasing = false;
}

bool cu_store_format(void) {
    if (state == CU_STORE_NONE) return false;
    for (uint32_t addr = CU_STORE_KEYS_BASE; addr < chip_size; addr += CU_STORE_SECTOR) {
        // Erased sectors are skipped: formatting a mostly empty chip stays quick.
        if (cu_store_is_blank(addr, CU_STORE_SECTOR)) continue;
        if (!cu_store_erase(addr)) return false;
    }
    state = CU_STORE_OK;
    return true;
}
