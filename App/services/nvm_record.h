#ifndef NVM_RECORD_H
#define NVM_RECORD_H
#include <stdint.h>
#include <stdbool.h>
#include "nvm_backend.h"
#include "nvm_map.h"

#define NVM_MAGIC        0x4E56u
#define NVM_HEADER_SIZE  8u
#define NVM_RECORD_SIZE  (NVM_HEADER_SIZE + NVM_PARAM_SIZE)

/* Program a record at byte offset `addr` of an already-erased region. Returns 0 on success. */
int  nvm_record_write(const nvm_backend_t *be, uint32_t addr, uint32_t seq, const uint8_t *payload);

/* Read+validate a record at `addr`. On a valid record (magic+CRC ok) fills *seq_out and
   payload_out[NVM_PARAM_SIZE] and returns true; otherwise returns false. */
bool nvm_record_read(const nvm_backend_t *be, uint32_t addr, uint32_t *seq_out, uint8_t *payload_out);

#endif
