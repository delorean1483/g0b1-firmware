#ifndef HTR_I2C_MAP_H
#define HTR_I2C_MAP_H
/* Frozen I2C contract between the heater coprocessor (slave) and the G0B1
   (master). Register-pointer protocol, auto-increment, u16/s16 little-endian.
   Any incompatible change bumps HTR_MAP_VERSION. Spec section 6. */

#define HTR_I2C_ADDR7        0x64u
#define HTR_DEVICE_ID        0xEFu
#define HTR_MAP_VERSION      1u

/* Identity (RO) */
#define HTR_R_DEVICE_ID       0x00u
#define HTR_R_MAP_VERSION     0x01u
#define HTR_R_FW_MAJOR        0x02u
#define HTR_R_FW_MINOR        0x03u
#define HTR_R_FW_BUILD        0x04u   /* u16 */
#define HTR_R_SUPPORTED_TYPES 0x06u   /* u16, bit n = type n */
#define HTR_R_HW_ID           0x08u
#define HTR_HW_ID_EF_L011     1u
#define HTR_HW_ID_C011        2u

/* Control (RW) */
#define HTR_R_HEATER_TYPE     0x10u   /* HTR_TYPE_*, persisted */
#define HTR_R_RUN_REQUEST     0x11u
#define HTR_R_LEVEL           0x12u   /* 1..10 */
#define HTR_R_SETPOINT_C      0x13u   /* s8, 5..30 */
#define HTR_R_AMBIENT_C_X10   0x14u   /* s16, -400..850, 0x7FFF unknown */
#define HTR_R_HOST_TIMEOUT    0x16u   /* minutes 0..60, 0 = disabled (bench), persisted */
#define HTR_R_COMMAND         0x18u   /* WO */
#define HTR_R_CMD_RESULT      0x19u   /* RO */

#define HTR_CMD_START         1u
#define HTR_CMD_STOP          2u
#define HTR_CMD_CLEAR_FAULT   3u
#define HTR_CMD_REINIT_IFACE  4u
#define HTR_CMD_AUTO_DETECT   5u

#define HTR_RES_OK            0u
#define HTR_RES_BUSY          1u
#define HTR_RES_BAD_ARG       2u
#define HTR_RES_NOT_SUPPORTED 3u
#define HTR_RES_NO_TYPE       4u

/* Status (RO, snapshotted per read transaction) */
#define HTR_R_PHASE           0x20u
#define HTR_R_ACTIVE_TYPE     0x21u
#define HTR_R_FLAGS           0x22u   /* u16 */
#define HTR_R_VALID           0x24u   /* u16, HTR_V_* */
#define HTR_R_VENDOR_STATE    0x26u
#define HTR_R_VENDOR_SUB      0x27u
#define HTR_R_FAULT_CODE      0x28u
#define HTR_R_ACTIVE_LEVEL    0x29u
#define HTR_R_SUPPLY_MV       0x2Au
#define HTR_R_FAN_RPM         0x2Cu
#define HTR_R_PUMP_HZ_X10     0x2Eu
#define HTR_R_EXCH_RAW        0x30u
#define HTR_R_EXCH_C_X10      0x32u
#define HTR_R_STATE_SECONDS   0x34u
#define HTR_R_AGE_MS          0x36u
#define HTR_R_VALID_FRAMES    0x38u
#define HTR_R_CRC_FAILS       0x3Au
#define HTR_R_TRANSPORT_ERRS  0x3Cu
#define HTR_R_ECHO_ERRS       0x3Eu
#define HTR_R_CAPS            0x40u
#define HTR_R_HOST_IDLE_S     0x42u
#define HTR_R_LINE_SENSE      0x44u
#define HTR_R_DETECT_RESULT   0x45u
#define HTR_REG_COUNT         0x46u
#define HTR_STATUS_FIRST      HTR_R_PHASE
#define HTR_STATUS_LEN        (HTR_REG_COUNT - HTR_R_PHASE)

/* FLAGS bits */
#define HTR_F_PRESENT           (1u << 0)
#define HTR_F_FRESH             (1u << 1)
#define HTR_F_SAFE_OFF          (1u << 2)
#define HTR_F_COMMS_FAULT       (1u << 3)
#define HTR_F_IFACE_POWERED     (1u << 4)
#define HTR_F_HOST_TIMEOUT_STOP (1u << 5)
#define HTR_F_ECHO_FAULT        (1u << 6)
#define HTR_F_DETECT_DONE       (1u << 7)
#define HTR_F_FAULT_LATCHED     (1u << 8)

/* LINE_SENSE */
#define HTR_LS_A_IDLE_HIGH    (1u << 0)
#define HTR_LS_B_IDLE_HIGH    (1u << 1)
#define HTR_LS_CLASS_SHIFT    4u
#define HTR_LS_CLASS_NONE     0u
#define HTR_LS_CLASS_3W       1u
#define HTR_LS_CLASS_4W       2u
#define HTR_LS_CLASS_AMBIG    3u

/* DETECT_RESULT */
#define HTR_DET_IDLE          0u
#define HTR_DET_RUNNING       1u
#define HTR_DET_FOUND         2u
#define HTR_DET_NO_HEATER     3u
#define HTR_DET_UNKNOWN       4u

#endif /* HTR_I2C_MAP_H */
