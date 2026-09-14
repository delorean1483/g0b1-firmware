#include "fake_iflash.h"
#include <string.h>

#define FL_BASE  0x08000000u
#define FL_SIZE  (512u*1024u)
#define FL_PAGE  2048u

static uint8_t s_mem[FL_SIZE];
static int s_fail_prog, s_fail_erase;

void fake_iflash_reset(void) { memset(s_mem, 0xFF, sizeof s_mem); s_fail_prog = s_fail_erase = 0; }
uint8_t *fake_iflash_ptr(uint32_t addr) { return &s_mem[addr - FL_BASE]; }
void fake_iflash_fail_next_program(void) { s_fail_prog = 1; }
void fake_iflash_fail_next_erase(void)   { s_fail_erase = 1; }

static int f_read(void *c, uint32_t a, uint8_t *b, uint32_t n) {
    (void)c; if (a < FL_BASE || a + n > FL_BASE + FL_SIZE) return -1;
    memcpy(b, &s_mem[a - FL_BASE], n); return 0;
}
static int f_program(void *c, uint32_t a, const uint8_t *b, uint32_t n) {
    (void)c;
    if (s_fail_prog) { s_fail_prog = 0; return -1; }
    if ((n & 7u) != 0u) return -1;                       /* dword granularity */
    if ((a & 7u) != 0u) return -1;                       /* dword aligned */
    if (a < FL_BASE || a + n > FL_BASE + FL_SIZE) return -1;
    uint8_t *p = &s_mem[a - FL_BASE];
    /* Check all bytes are erased (0xFF) before programming */
    for (uint32_t i = 0; i < n; i++) if (p[i] != 0xFFu) return -2; /* space not erased */
    for (uint32_t i = 0; i < n; i++) p[i] &= b[i];
    return 0;
}
static int f_erase(void *c, uint32_t page) {
    (void)c;
    if (s_fail_erase) { s_fail_erase = 0; return -1; }
    if (page >= FL_SIZE / FL_PAGE) return -1;
    memset(&s_mem[page * FL_PAGE], 0xFF, FL_PAGE);
    return 0;
}
static const iflash_backend_t s_be = {
    FL_PAGE, FL_SIZE / FL_PAGE, FL_BASE, f_read, f_program, f_erase, 0
};
const iflash_backend_t *fake_iflash_backend(void) { return &s_be; }
