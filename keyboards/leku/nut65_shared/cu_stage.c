// Copyright 2026 Colorfulshadow (@Colorfulshadow)
// SPDX-License-Identifier: GPL-2.0-or-later
// Experimental image receiver only. See app/docs/STAGING_PROTOCOL.md.
#include QMK_KEYBOARD_H
#include "hal.h"
#include <string.h>
#include "cu_stage.h"
#include "cu_stage_sha.h"

#define FLASH_BASE_ADDR 0x08000000u
#define STAGE_ADDR      0x08020000u
#define JOURNAL_ADDR    0x0803E000u
#define FLASH_END_ADDR  0x08040000u
#define PAGE_BYTES      256u
#define JOURNAL_PAGES   32u
#define BEGIN_MAGIC    0x37475453u

typedef struct __attribute__((packed)) {
    uint8_t magic[8];
    uint32_t serial, session;
    uint8_t state, reserved[3], manifest[48];
    uint32_t durable;
    uint8_t pad[180];
    uint32_t crc;
} stage_record_t;
_Static_assert(sizeof(stage_record_t) == PAGE_BYTES, "stage journal is one physical page");
_Static_assert(STAGE_ADDR + CU_STAGE_MAX == JOURNAL_ADDR, "image and journal may not overlap");
_Static_assert(JOURNAL_ADDR + JOURNAL_PAGES * PAGE_BYTES == FLASH_END_ADDR, "journal range");

static stage_record_t current;
static uint8_t journal_slot = JOURNAL_PAGES - 1;
static uint8_t page[PAGE_BYTES], manifest[48];
static uint16_t filled;
static uint64_t manifest_mask;
static uint32_t manifest_session, lease_at, verify_at;
static bool available, leased;
static cu_stage_sha_t hash;
extern uint8_t __flash0_free__;

static uint32_t read32(const uint8_t *p) { uint32_t v; memcpy(&v,p,4); return v; }
static void put32(uint8_t *p, uint32_t v) { memcpy(p,&v,4); }
static uint32_t image_size(void) { return read32(current.manifest + 8); }
static uint32_t crc32(const void *src, uint32_t n) {
    const uint8_t *p = src;
    uint32_t c = ~0u;
    while (n--) {
        c ^= *p++;
        for (uint8_t b=0;b<8;b++) c = (c>>1) ^ (0xEDB88320u & (0u-(c&1)));
    }
    return ~c;
}
static bool manifest_ok(const uint8_t *m) {
    return !memcmp(m,"NS01",4) && m[4] == NUT65_MODEL_ID && m[5] == 1 && !m[6] && !m[7] &&
           read32(m+8) >= 8 && read32(m+8) <= CU_STAGE_MAX && crc32(m,44) == read32(m+44);
}
static bool record_ok(const stage_record_t *r) {
    if (memcmp(r->magic,"NUTSTG7",8) || crc32(r,252) != r->crc) return false;
    if (r->state == CU_ST_EMPTY) return r->session == 0 && r->durable == 0;
    return r->session && manifest_ok(r->manifest) && r->state >= CU_ST_RECEIVING && r->state <= CU_ST_FAILED &&
           r->durable <= read32(r->manifest+8) &&
           ((r->state != CU_ST_READY && r->state != CU_ST_VERIFYING) || r->durable == read32(r->manifest+8)) &&
           (!(r->durable % PAGE_BYTES) || r->durable == read32(r->manifest+8));
}
static bool program_page(uint32_t address, const void *data) {
    // This is the ONLY physical write primitive. Never erase/program the running app.
    if (!available || address < STAGE_ADDR || address > FLASH_END_ADDR-PAGE_BYTES || address % PAGE_BYTES) return false;
    uint32_t old_fhsi=ANCTL->FHSIENR;
    if (eflStart(&EFLD1,NULL)!=MSG_OK) return false;
    bool ok=flashProgram(&EFLD1,address-FLASH_BASE_ADDR,PAGE_BYTES,data)==FLASH_NO_ERROR;
    if (ok) ok=memcmp((const void *)(uintptr_t)address,data,PAGE_BYTES)==0;
    eflStop(&EFLD1);
    // The vendor EFL start enables FHSI, but its stop only locks Flash.
    // Restore the exact prior oscillator state after each synchronous operation.
    PWR->ANAKEY1=0x03;PWR->ANAKEY2=0x0C;
    ANCTL->FHSIENR=old_fhsi;
    PWR->ANAKEY1=0;PWR->ANAKEY2=0;
    return ok;
}
static bool persist(stage_record_t next) {
    memcpy(next.magic,"NUTSTG7",8);
    next.serial = current.serial + 1;
    next.crc = crc32(&next,252);
    uint8_t slot = (journal_slot+1) % JOURNAL_PAGES;
    // The preceding valid record stays intact if this erase/program is interrupted.
    if (!program_page(JOURNAL_ADDR+slot*PAGE_BYTES,&next)) return false;
    current = next;
    journal_slot = slot;
    return true;
}
void cu_stage_init(void) {
    available = false;
    filled = 0; manifest_mask = 0; leased = false;
    memset(&current,0,sizeof current);
    journal_slot = JOURNAL_PAGES-1;
    // Runtime guard complements the link-time 128 KiB application limit.
    if ((uintptr_t)&__flash0_free__ > STAGE_ADDR) return;
    available = true;
    bool found = false;
    for (uint8_t i=0;i<JOURNAL_PAGES;i++) {
        const stage_record_t *r = (const stage_record_t *)(uintptr_t)(JOURNAL_ADDR+i*PAGE_BYTES);
        if (record_ok(r) && (!found || (int32_t)(r->serial-current.serial)>0)) {
            current=*r; journal_slot=i; found=true;
        }
    }
    if (current.state == CU_ST_VERIFYING) current.state = CU_ST_RECEIVING;
}
bool cu_stage_awake(void) { return leased && timer_elapsed32(lease_at) < 30000; }
static void renew(void) { leased=true; lease_at=timer_read32(); }
void cu_stage_task(void) {
    if (current.state != CU_ST_VERIFYING) return;
    uint32_t left = image_size()-verify_at, n = left > PAGE_BYTES ? PAGE_BYTES : left;
    if (n) {
        cu_stage_sha_update(&hash,(const uint8_t *)(uintptr_t)(STAGE_ADDR+verify_at),n);
        verify_at += n;
        return;
    }
    uint8_t digest[32];
    cu_stage_sha_final(&hash,digest);
    const uint8_t *vectors = (const uint8_t *)(uintptr_t)STAGE_ADDR;
    uint32_t sp=read32(vectors), reset=read32(vectors+4);
    bool valid = !memcmp(digest,current.manifest+12,32) && sp>0x20000000u && sp<=0x20009000u && !(sp&3) &&
                 (reset&1) && (reset&~1u)>=FLASH_BASE_ADDR && (reset&~1u)<FLASH_BASE_ADDR+image_size();
    stage_record_t next=current;
    next.state=valid ? CU_ST_READY : CU_ST_FAILED;
    if (!persist(next)) current.state=CU_ST_FAILED;
    leased=false;
}
static uint8_t operate(uint8_t op,uint32_t sid,const uint8_t *body) {
    if (!available) return CU_ST_IO;
    if (op==CU_ST_CAPS || op==CU_ST_STATUS) return CU_ST_OK;
    if (!sid) return CU_ST_SESSION;
    if (op==CU_ST_ABORT && current.state==CU_ST_EMPTY && read32(current.manifest)==sid && read32(body)==BEGIN_MAGIC) return CU_ST_OK;
    if (op==CU_ST_META) {
        if (manifest_session!=sid) { manifest_session=sid; manifest_mask=0; }
        uint8_t off=body[0], n=off<48 ? (uint8_t)MIN(19,48-off) : 0;
        if (!n) return CU_ST_ARGUMENT;
        for (uint8_t i=0;i<n;i++) {
            uint64_t bit=(uint64_t)1<<(off+i);
            if ((manifest_mask&bit) && manifest[off+i]!=body[1+i]) return CU_ST_CONFLICT;
            manifest[off+i]=body[1+i]; manifest_mask|=bit;
        }
        return CU_ST_OK;
    }
    if (op==CU_ST_BEGIN) {
        if (read32(body)!=BEGIN_MAGIC || manifest_session!=sid || manifest_mask!=(((uint64_t)1<<48)-1)) return CU_ST_ARGUMENT;
        if (manifest[4]!=NUT65_MODEL_ID) return CU_ST_MODEL;
        if (read32(manifest+8)<8 || read32(manifest+8)>CU_STAGE_MAX) return CU_ST_SIZE;
        if (!manifest_ok(manifest)) return CU_ST_ARGUMENT;
        if (current.state!=CU_ST_EMPTY) {
            if (sid!=current.session || memcmp(manifest,current.manifest,48)) return CU_ST_CONFLICT;
            if (current.state==CU_ST_FAILED) return CU_ST_HASH;
            renew(); return CU_ST_OK;
        }
        stage_record_t next={0};
        next.session=sid; next.state=CU_ST_RECEIVING; memcpy(next.manifest,manifest,48);
        if (!persist(next)) return CU_ST_IO;
        filled=0; renew(); return CU_ST_OK;
    }
    if (sid!=current.session || current.state==CU_ST_EMPTY) return CU_ST_SESSION;
    if (op==CU_ST_ABORT) {
        if (read32(body)!=BEGIN_MAGIC) return CU_ST_ARGUMENT;
        stage_record_t next={0};
        put32(next.manifest,sid); // retain the abort token for an ACK-lost retry, including after reset
        if (!persist(next)) return CU_ST_IO;
        filled=0; manifest_mask=0; leased=false; return CU_ST_OK;
    }
    if (op==CU_ST_VERIFY) {
        if (current.state==CU_ST_VERIFYING) return CU_ST_OK;
        if ((current.state!=CU_ST_RECEIVING && current.state!=CU_ST_READY) || current.durable!=image_size() || filled) return CU_ST_OFFSET;
        current.state=CU_ST_VERIFYING; verify_at=0; cu_stage_sha_init(&hash); renew(); return CU_ST_OK;
    }
    if (current.state!=CU_ST_RECEIVING) return CU_ST_BUSY;
    renew();
    uint32_t off=read32(body), size=image_size();
    if (op==CU_ST_DATA) {
        if (off>=size) return CU_ST_OFFSET;
        uint32_t n=MIN(16u,size-off);
        if (off<current.durable) {
            if (n>current.durable-off) return CU_ST_OFFSET;
            return memcmp((const void *)(uintptr_t)(STAGE_ADDR+off),body+4,n) ? CU_ST_CONFLICT : CU_ST_OK;
        }
        if (off<current.durable+filled) {
            if (n>current.durable+filled-off) return CU_ST_OFFSET;
            return memcmp(page+off-current.durable,body+4,n) ? CU_ST_CONFLICT : CU_ST_OK;
        }
        if (off!=current.durable+filled || n>PAGE_BYTES-filled) return CU_ST_OFFSET;
        memcpy(page+filled,body+4,n); filled+=n;
        return CU_ST_OK;
    }
    if (op==CU_ST_COMMIT) {
        if (off%PAGE_BYTES || off>=size) return CU_ST_OFFSET;
        uint32_t n=MIN(PAGE_BYTES,size-off), expected=read32(body+4);
        if (off<current.durable) {
            if (n>current.durable-off) return CU_ST_OFFSET;
            return crc32((const void *)(uintptr_t)(STAGE_ADDR+off),n)==expected ? CU_ST_OK : CU_ST_CONFLICT;
        }
        if (off!=current.durable || filled!=n || crc32(page,n)!=expected) return CU_ST_CONFLICT;
        memset(page+n,0xFF,PAGE_BYTES-n);
        if (!program_page(STAGE_ADDR+off,page)) return CU_ST_IO;
        stage_record_t next=current; next.durable+=n;
        if (!persist(next)) return CU_ST_IO;
        filled=0; return CU_ST_OK;
    }
    return CU_ST_ARGUMENT;
}
void cu_stage_command(const uint8_t args[29],uint8_t p[28]) {
    // Copy args: caller's in-place reply overlaps request storage in other transports.
    uint8_t a[29]; memcpy(a,args,sizeof a);
    uint32_t sid=read32(a+5);
    uint8_t error=operate(a[0],sid,a+9);
    memset(p,0,28); p[0]=a[0]; memcpy(p+1,a+1,4);
    put32(p+5,current.session); p[9]=current.state; p[10]=error;
    put32(p+11,current.durable+filled); put32(p+15,current.durable); put32(p+19,image_size());
    put32(p+23,a[0]==CU_ST_CAPS ? CU_STAGE_MAX : verify_at); p[27]=NUT65_MODEL_ID;
}
