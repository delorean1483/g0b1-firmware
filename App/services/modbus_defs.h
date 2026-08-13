#ifndef MODBUS_DEFS_H
#define MODBUS_DEFS_H
#include "types.h"

/* Function codes (0x41/0x42 file FCs reserved for bootloader, not implemented). */
#define MB_FC_READ_HOLDING     0x03u
#define MB_FC_READ_INPUT       0x04u
#define MB_FC_WRITE_SINGLE     0x06u
#define MB_FC_READ_EXCEPTION   0x07u
#define MB_FC_DIAGNOSTICS      0x08u
#define MB_FC_WRITE_MULTIPLE   0x10u
#define MB_FC_REPORT_SLAVE_ID  0x11u
#define MB_ERROR_RESPONSE      0x80u

typedef enum {
    MB_EXC_NONE             = 0,
    MB_EXC_ILLEGAL_FUNCTION = 1,
    MB_EXC_ILLEGAL_ADDRESS  = 2,
    MB_EXC_ILLEGAL_VALUE    = 3
} modbus_exc_t;

/* PDU field offsets within the RTU frame. */
#define MB_F_ADDR      0u
#define MB_F_FUNCTION  1u
#define MB_F_START_HI  2u
#define MB_F_START_LO  3u
#define MB_F_QTY_HI    4u
#define MB_F_QTY_LO    5u
#define MB_HEADER_SIZE 6u

/* Register model. Registers are 1..52; wire range check is (start+count) <= 53. */
#define MB_REG_MAX     52u
#define MB_REG_LIMIT   53u

/* Diagnostics (FC 0x08) sub-functions. */
#define MB_DIAG_RETURN_QUERY   0x00u
#define MB_DIAG_RESTART_COMM   0x01u
#define MB_DIAG_CLEAR_COUNTERS 0x0Au
#define MB_DIAG_FIRST_COUNTER  0x0Bu   /* 0x0B..0x12 -> counter[sub-0x0B] */
#define MB_DIAG_LAST_COUNTER   0x12u
#define MB_DIAG_CLR_OVERRUN    0x14u
#define MB_DIAG_ENTER_TEST     0xAAu
#define MB_COUNTER_COUNT       8u

/* Addressing + buffer. */
#define MB_SLAVE_ADDR      1u
#define MB_BROADCAST_ADDR  0u
#define MB_MAX_FRAME       256u

#endif /* MODBUS_DEFS_H */
