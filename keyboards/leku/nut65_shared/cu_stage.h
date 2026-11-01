// Copyright 2026 Colorfulshadow (@Colorfulshadow)
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stdbool.h>
#include <stdint.h>

// Stage-only protocol. No command installs, reboots or writes below 0x08020000.
#define CU_STAGE_SUB 0x70
#define CU_STAGE_MAX (120u * 1024u)
enum { CU_ST_CAPS, CU_ST_STATUS, CU_ST_META, CU_ST_BEGIN, CU_ST_DATA, CU_ST_COMMIT, CU_ST_VERIFY, CU_ST_ABORT };
enum { CU_ST_EMPTY, CU_ST_RECEIVING, CU_ST_VERIFYING, CU_ST_READY, CU_ST_FAILED };
enum { CU_ST_OK, CU_ST_ARGUMENT, CU_ST_SESSION, CU_ST_OFFSET, CU_ST_CONFLICT, CU_ST_IO, CU_ST_HASH, CU_ST_BUSY, CU_ST_MODEL, CU_ST_SIZE };
void cu_stage_init(void);
void cu_stage_task(void);
bool cu_stage_awake(void);
void cu_stage_command(const uint8_t args[29], uint8_t reply[28]);
