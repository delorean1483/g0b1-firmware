#ifndef NOR_PAGE_SPLIT_H
#define NOR_PAGE_SPLIT_H
#include <stdint.h>

/* S25FL064 PAGE PROGRAM wraps within a 256-byte page: a program that crosses a
   page boundary silently corrupts (the tail wraps back to the page start). The
   NVM journal writes 264-byte records, so every program() must be split at page
   boundaries. This pure helper does that split; the concrete driver supplies a
   callback that issues one SPI PAGE PROGRAM per chunk. Host-tested. */
#define NOR_PAGE_SIZE 256u

/* Invoked once per page-bounded chunk, in ascending-address order. */
typedef void (*nor_page_cb)(void *ctx, uint32_t addr, const uint8_t *buf, uint32_t len);

/* Split [addr, addr+len) into chunks that never cross a NOR_PAGE_SIZE boundary
   and invoke cb() for each. len == 0 invokes cb() zero times. */
void nor_page_split(uint32_t addr, const uint8_t *buf, uint32_t len,
                    nor_page_cb cb, void *ctx);

#endif /* NOR_PAGE_SPLIT_H */
