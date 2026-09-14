#ifndef IFLASH_BACKEND_H
#define IFLASH_BACKEND_H
#include <stdint.h>
/* Abstract STM32 INTERNAL flash. Erased = 0xFF. program() writes an 8-byte
   double-word aligned region (len a multiple of 8), AND-semantics, erased
   space only. erase_page() erases one 2 KB page by absolute index (0..255).
   read() abstracts the memory-mapped flash so host tests can fake it.
   All ops return 0 on success, non-zero on error. */
typedef struct iflash_backend {
    uint32_t page_size;    /* 2048 */
    uint32_t page_count;   /* 256 */
    uint32_t base_addr;    /* 0x08000000 */
    int (*read)(void *ctx, uint32_t addr, uint8_t *buf, uint32_t len);
    int (*program)(void *ctx, uint32_t addr, const uint8_t *data, uint32_t len);
    int (*erase_page)(void *ctx, uint32_t page_index);
    void *ctx;
} iflash_backend_t;
#endif
