#ifndef NVM_BACKEND_H
#define NVM_BACKEND_H
#include <stdint.h>
/* Abstract NOR flash. Erased state is 0xFF. program() may only clear bits
   (AND semantics) and must be called on erased space. erase() clears a whole
   sector to 0xFF. All ops return 0 on success, non-zero on error. */
typedef struct nvm_backend {
    uint32_t sector_size;
    uint32_t sector_count;
    int  (*read)(void *ctx, uint32_t addr, uint8_t *buf, uint32_t len);
    int  (*program)(void *ctx, uint32_t addr, const uint8_t *buf, uint32_t len);
    int  (*erase)(void *ctx, uint32_t sector_index);
    void *ctx;
} nvm_backend_t;
#endif
