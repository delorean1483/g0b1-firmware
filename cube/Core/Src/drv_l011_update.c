#include "drv_i2c2_bus.h"
#include "mb_regmodel.h"
#include "l011_update.h"
#include "modbus_crc.h"
#include "heater.h"
#include "htr_i2c_map.h"
#include "htr_types.h"
#include "rgb.h"

#define FIRST MB_L011_UPDATE_MODE
#define STATUS (FIRST + 1u)
#define LENGTH (FIRST + 2u)
#define DATA (FIRST + 3u)
#define END (DATA + (L011_FRAME_MAX + 1u) / 2u)
#define TIMEOUT 20u

_Static_assert(END == MB_L011_UPDATE_LAST + 1u, "L011 update register range mismatch");

typedef enum {
    ENTRY_OK = 0,
    ENTRY_PHASE = 1,
    ENTRY_RUN = 2,
    ENTRY_LAMP = 3,
    ENTRY_IDENTITY = 4,
    ENTRY_MODE = 5,
    ENTRY_I2C = 6
} entry_result_t;

static entry_result_t last_rejection;
static bool maintenance;
static uint8_t staging[L011_FRAME_MAX + 1u];
static uint16_t length, status;

bool drv_l011_update_active(void) { return maintenance; }

static entry_result_t app_identity(void)
{
    uint8_t id[2];
    if (drv_i2c2_read(HTR_I2C_ADDR7, HTR_R_DEVICE_ID, id, sizeof(id), TIMEOUT) != 0)
        return ENTRY_I2C;
    return id[0] == HTR_DEVICE_ID && id[1] == HTR_MAP_VERSION ? ENTRY_OK : ENTRY_IDENTITY;
}

static bool boot_reply(uint8_t reply[4])
{
    return drv_i2c2_read(L011_BOOT_ADDR, 0, reply, 4, TIMEOUT) == 0 &&
           reply[0] == L011_BOOT_ID;
}

static entry_result_t acquire(void)
{
    uint8_t requested[3], applied[3], reply[4], control, phase;
    uint16_t heater_mode;
    if (mb_reg_read(10u, &heater_mode) != MB_EXC_NONE) return ENTRY_I2C;
    if (heater_mode) return ENTRY_MODE;
    if (heater_get_request()) return ENTRY_RUN;
    rgb_get_requested(requested);
    rgb_get_applied(applied);
    if (!(rgb_get_status() & RGB_ST_APPLY_OK) ||
        (requested[0] | requested[1] | requested[2] | applied[0] | applied[1] | applied[2]))
        return ENTRY_LAMP;
    if (boot_reply(reply)) {
        maintenance = true;
        heater_forget_coproc();
        return ENTRY_OK;
    }
    entry_result_t identity = app_identity();
    if (identity != ENTRY_OK) return identity;
    if (drv_i2c2_read(HTR_I2C_ADDR7, HTR_R_RUN_REQUEST, &control, 1, TIMEOUT))
        return ENTRY_I2C;
    if (control) return ENTRY_RUN;
    if (drv_i2c2_read(HTR_I2C_ADDR7, HTR_R_PHASE, &phase, 1, TIMEOUT))
        return ENTRY_I2C;
    if (phase != HTR_PHASE_OFF) return ENTRY_PHASE;
    maintenance = true;
    heater_forget_coproc();
    const uint8_t key[] = {L011_ENTER_KEY, L011_ENTER_KEY ^ 0xFFu};
    /* An ambiguous write can have reset the L011. Check both addresses afterward. */
    (void)drv_i2c2_write_once(HTR_I2C_ADDR7, L011_ENTER_REG, key, sizeof(key), TIMEOUT);
    return ENTRY_OK;
}

static modbus_exc_t read_reg(uint16_t reg, uint16_t *out)
{
    if (reg == FIRST) *out = maintenance;
    else if (reg == STATUS) *out = status;
    else if (reg == MB_L011_UPDATE_REJECT) *out = (uint16_t)last_rejection;
    else *out = 0;
    return MB_EXC_NONE;
}

static modbus_exc_t write_reg(uint16_t reg, uint16_t value)
{
    if (reg == FIRST && value == 0xB011u) {
        last_rejection = acquire();
        if (last_rejection != ENTRY_OK) return MB_EXC_ILLEGAL_VALUE;
        length = status = 0;
        return MB_EXC_NONE;
    }
    if (!maintenance) return MB_EXC_ILLEGAL_VALUE;
    if (reg == FIRST && value == 0xB012u) {
        if (app_identity() != ENTRY_OK || heater_get_request()) return MB_EXC_ILLEGAL_VALUE;
        rgb_set_logical(0, 0, 0);
        heater_forget_coproc();
        maintenance = false;
    } else if (reg == FIRST && value == 0xB013u) {
        uint8_t reply[4];
        if (boot_reply(reply)) status = 0x100u | reply[1];
        else status = app_identity() == ENTRY_OK ? 0x200u : 0u;
        if (length && (status & 0x100u) &&
            (reply[2] != staging[length - 2u] || reply[3] != staging[length - 1u]))
            status = 0x100u;
    } else if (reg == FIRST && value == 0xB014u) {
        if (length < 3u || modbus_crc16(staging, length - 2u) !=
            (uint16_t)(staging[length - 2u] | (staging[length - 1u] << 8))) return MB_EXC_ILLEGAL_VALUE;
        status = 0;
        /* Cortex polls separately. A lost ACK is resolved by command CRC. */
        (void)drv_i2c2_write_once(L011_BOOT_ADDR, staging[0], staging + 1,
                                   (uint16_t)(length - 1u), TIMEOUT);
    } else if (reg == LENGTH && value >= 3u && value <= L011_FRAME_MAX) {
        length = value;
    } else if (reg >= DATA && reg < END) {
        unsigned index = 2u * (reg - DATA);
        staging[index] = (uint8_t)value;
        staging[index + 1u] = (uint8_t)(value >> 8);
    } else return MB_EXC_ILLEGAL_VALUE;
    return MB_EXC_NONE;
}

void drv_l011_update_register(void)
{
    for (uint16_t reg = FIRST; reg < END; ++reg)
        mb_reg_bind(reg, read_reg, reg == STATUS ? 0 : write_reg);
    mb_reg_bind(MB_L011_UPDATE_REJECT, read_reg, 0);
}
