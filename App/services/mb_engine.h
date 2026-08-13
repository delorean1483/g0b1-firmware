#ifndef MB_ENGINE_H
#define MB_ENGINE_H
#include "modbus_defs.h"

void mb_engine_init(void);   /* reset diagnostic counters + test-mode flag */

/* Process one received RTU frame (bytes addr..crc_hi). Writes the response into
   resp (caller supplies >= MB_MAX_FRAME bytes) and its length into *resp_len.
   *resp_len == 0 => send nothing (frame not for us, bad CRC, or broadcast). */
void mb_engine_process(const uint8_t *req, uint16_t req_len, uint8_t *resp, uint16_t *resp_len);

uint16_t mb_engine_counter(uint8_t idx);   /* idx 0..MB_COUNTER_COUNT-1 */
bool     mb_engine_test_mode(void);

#endif /* MB_ENGINE_H */
