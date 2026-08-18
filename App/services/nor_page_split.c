#include "nor_page_split.h"

void nor_page_split(uint32_t addr, const uint8_t *buf, uint32_t len,
                    nor_page_cb cb, void *ctx)
{
    while (len > 0u) {
        uint32_t page_room = NOR_PAGE_SIZE - (addr % NOR_PAGE_SIZE);
        uint32_t chunk = (len < page_room) ? len : page_room;
        cb(ctx, addr, buf, chunk);
        addr += chunk;
        buf  += chunk;
        len  -= chunk;
    }
}
