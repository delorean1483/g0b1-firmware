#include "unity.h"
#include "nor_page_split.h"
#include <string.h>
#include <stdbool.h>

/* A recording fake of an S25FL064 PAGE PROGRAM: applies each chunk to a backing
   buffer AND emulates the real hardware wrap (a chunk crossing a 256-byte page
   boundary wraps its tail back to the page start), so a bad split both trips the
   boundary check and corrupts the read-back. */
#define MEM_SIZE 1024u
static uint8_t  s_mem[MEM_SIZE];
static uint32_t s_chunks[16][2];   /* [addr, len] recorded per callback */
static uint32_t s_nchunks;
static bool     s_crossed;         /* set if any chunk spanned a page boundary */

static void rec_cb(void *ctx, uint32_t addr, const uint8_t *buf, uint32_t len)
{
    (void)ctx;
    if (s_nchunks < 16u) { s_chunks[s_nchunks][0] = addr; s_chunks[s_nchunks][1] = len; }
    s_nchunks++;

    if (len > 0u && (addr / NOR_PAGE_SIZE) != ((addr + len - 1u) / NOR_PAGE_SIZE)) {
        s_crossed = true;
        uint32_t page = addr - (addr % NOR_PAGE_SIZE);      /* wrap within the page */
        for (uint32_t i = 0; i < len; i++) {
            s_mem[page + ((addr - page + i) % NOR_PAGE_SIZE)] = buf[i];
        }
    } else {
        memcpy(&s_mem[addr], buf, len);
    }
}

void setUp(void)  { memset(s_mem, 0xFF, sizeof s_mem); s_nchunks = 0u; s_crossed = false; }
void tearDown(void) {}

static void program_and_check(uint32_t addr, uint32_t len)
{
    uint8_t in[512];
    for (uint32_t i = 0; i < len; i++) in[i] = (uint8_t)(0xA0u + i);
    nor_page_split(addr, in, len, rec_cb, NULL);
    TEST_ASSERT_FALSE_MESSAGE(s_crossed, "a chunk crossed a 256-byte page boundary");
    TEST_ASSERT_EQUAL_UINT8_ARRAY(in, &s_mem[addr], len);   /* read-back == input */
}

/* A 264-byte record at a page-aligned start: chunks [0,256] then [256,8]. */
void test_split_record_at_zero(void)
{
    program_and_check(0u, 264u);
    TEST_ASSERT_EQUAL_UINT32(2u, s_nchunks);
    TEST_ASSERT_EQUAL_UINT32(0u,   s_chunks[0][0]); TEST_ASSERT_EQUAL_UINT32(256u, s_chunks[0][1]);
    TEST_ASSERT_EQUAL_UINT32(256u, s_chunks[1][0]); TEST_ASSERT_EQUAL_UINT32(8u,   s_chunks[1][1]);
}

/* A 264-byte record from a non-page-aligned start (offset 200): [200,56] then [256,208]. */
void test_split_record_unaligned(void)
{
    program_and_check(200u, 264u);
    TEST_ASSERT_EQUAL_UINT32(2u, s_nchunks);
    TEST_ASSERT_EQUAL_UINT32(200u, s_chunks[0][0]); TEST_ASSERT_EQUAL_UINT32(56u,  s_chunks[0][1]);
    TEST_ASSERT_EQUAL_UINT32(256u, s_chunks[1][0]); TEST_ASSERT_EQUAL_UINT32(208u, s_chunks[1][1]);
}

/* A write wholly inside one page stays a single chunk. */
void test_split_within_page(void)
{
    program_and_check(300u, 100u);   /* 300..399, all in page 1 */
    TEST_ASSERT_EQUAL_UINT32(1u, s_nchunks);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_split_record_at_zero);
    RUN_TEST(test_split_record_unaligned);
    RUN_TEST(test_split_within_page);
    return UNITY_END();
}
