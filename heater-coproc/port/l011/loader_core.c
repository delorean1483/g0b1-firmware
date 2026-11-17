#include "loader_core.h"
#include "l011_update.h"
#include "crc16_modbus.h"

static uint32_t image_length, image_crc, next_page;
static bool updating;

static uint32_t le(const uint8_t *p, unsigned n)
{
    uint32_t value = 0;
    for (unsigned i = 0; i < n; ++i) value |= (uint32_t)p[i] << (8u * i);
    return value;
}

static bool image_matches(uint32_t length, uint32_t crc)
{
    if (length < 192u || length > L011_APP_CAPACITY || length % 4u) return false;
    uint32_t sp = loader_read(L011_APP_BASE);
    uint32_t pc = loader_read(L011_APP_BASE + 4u);
    if (sp != 0x20000800u || !(pc & 1u) ||
        pc < L011_APP_BASE + 192u || pc >= L011_APP_BASE + length) return false;
    uint32_t actual = 0xFFFFFFFFu;
    for (uint32_t offset = 0; offset < length; offset += 4u) {
        actual ^= loader_read(L011_APP_BASE + offset);
        for (unsigned i = 0; i < 32u; ++i)
            actual = (actual >> 1) ^ ((actual & 1u) ? 0xEDB88320u : 0u);
    }
    return (actual ^ 0xFFFFFFFFu) == crc;
}

bool loader_valid(void)
{
    return loader_read(L011_META_BASE) == L011_IMAGE_MAGIC &&
           image_matches(loader_read(L011_META_BASE + 4u), loader_read(L011_META_BASE + 8u));
}

bool loader_command(const uint8_t *frame, unsigned size)
{
    if (size < 3u || crc16_modbus(frame, size - 2u) != le(frame + size - 2u, 2u))
        return false;
    switch (frame[0]) {
    case L011_BEGIN: {
        if (size != 9u) return false;
        uint32_t length = le(frame + 1, 2u);
        if (length < 192u || length > L011_APP_CAPACITY || length % 4u) return false;
        updating = false;
        if (!loader_erase(L011_META_BASE) || loader_read(L011_META_BASE) != 0u) return false;
        image_length = length;
        image_crc = le(frame + 3, 4u);
        next_page = 0;
        updating = true;
        return true;
    }
    case L011_PAGE: {
        if (!updating || size != L011_FRAME_MAX) return false;
        uint32_t offset = le(frame + 1, 2u);
        if (offset % L011_PAGE_SIZE || offset >= image_length ||
            (offset != next_page && offset + L011_PAGE_SIZE != next_page)) return false;
        uint32_t address = L011_APP_BASE + offset;
        if (offset == next_page && !loader_erase(address)) return false;
        for (unsigned i = 0; i < L011_PAGE_SIZE; i += 4u) {
            uint32_t word = le(frame + 3 + i, 4u);
            if (offset == next_page && word && !loader_word(address + i, word)) return false;
            if (loader_read(address + i) != word) return false;
        }
        if (offset == next_page) next_page += L011_PAGE_SIZE;
        return true;
    }
    case L011_COMMIT:
        if (size != 3u) return false;
        if (loader_valid()) return true;
        if (!updating || next_page < image_length || !image_matches(image_length, image_crc)) return false;
        /* A torn metadata write must be retryable. Publish the magic last. */
        if (!loader_erase(L011_META_BASE) ||
            !loader_word(L011_META_BASE + 4u, image_length) ||
            (image_crc && !loader_word(L011_META_BASE + 8u, image_crc)) ||
            !loader_word(L011_META_BASE, L011_IMAGE_MAGIC)) return false;
        updating = false;
        return loader_valid();
    case L011_REBOOT:
        return size == 3u && loader_valid();
    default:
        return false;
    }
}
