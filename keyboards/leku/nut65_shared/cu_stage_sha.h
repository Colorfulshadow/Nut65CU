// Copyright 2026 Colorfulshadow (@Colorfulshadow)
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stdint.h>
#include <stddef.h>
typedef struct { uint32_t h[8]; uint64_t bytes; uint8_t block[64]; uint8_t used; } cu_stage_sha_t;
void cu_stage_sha_init(cu_stage_sha_t *s);
void cu_stage_sha_update(cu_stage_sha_t *s,const uint8_t *p,size_t n);
void cu_stage_sha_final(cu_stage_sha_t *s,uint8_t digest[32]);
