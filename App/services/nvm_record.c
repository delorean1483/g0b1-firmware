#include "nvm_record.h"
#include "modbus_crc.h"

int nvm_record_write(const nvm_backend_t *be, uint32_t addr, uint32_t seq, const uint8_t *payload) {
    uint8_t rec[NVM_RECORD_SIZE];
    uint16_t crc = modbus_crc16(payload, NVM_PARAM_SIZE);

    rec[0] = (uint8_t)(NVM_MAGIC & 0xFF);
    rec[1] = (uint8_t)(NVM_MAGIC >> 8);
    rec[2] = (uint8_t)(seq & 0xFF);
    rec[3] = (uint8_t)((seq >> 8) & 0xFF);
    rec[4] = (uint8_t)((seq >> 16) & 0xFF);
    rec[5] = (uint8_t)((seq >> 24) & 0xFF);
    rec[6] = (uint8_t)(crc & 0xFF);
    rec[7] = (uint8_t)(crc >> 8);

    for (uint32_t i = 0; i < NVM_PARAM_SIZE; i++)
        rec[NVM_HEADER_SIZE + i] = payload[i];

    return be->program(be->ctx, addr, rec, NVM_RECORD_SIZE);
}

bool nvm_record_read(const nvm_backend_t *be, uint32_t addr, uint32_t *seq_out, uint8_t *payload_out) {
    uint8_t rec[NVM_RECORD_SIZE];

    if (be->read(be->ctx, addr, rec, NVM_RECORD_SIZE) != 0)
        return false;

    uint16_t magic = (uint16_t)(rec[0] | (rec[1] << 8));
    if (magic != NVM_MAGIC)
        return false;

    uint16_t crc_stored = (uint16_t)(rec[6] | (rec[7] << 8));
    uint16_t crc_calc   = modbus_crc16(&rec[NVM_HEADER_SIZE], NVM_PARAM_SIZE);
    if (crc_stored != crc_calc)
        return false;

    *seq_out = (uint32_t)rec[2] | ((uint32_t)rec[3] << 8)
             | ((uint32_t)rec[4] << 16) | ((uint32_t)rec[5] << 24);

    for (uint32_t i = 0; i < NVM_PARAM_SIZE; i++)
        payload_out[i] = rec[NVM_HEADER_SIZE + i];

    return true;
}
