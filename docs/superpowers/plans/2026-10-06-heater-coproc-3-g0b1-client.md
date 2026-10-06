# Heater Coprocessor — Plan 3 of 3: G0B1 Client Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the G0B1's WK2132-bridge + in-MCU VEVOR path with a small I²C client of the heater coprocessor (0x64), keep Modbus regs 53–67 meaning-compatible for cortex v1.2.40+, and add regs 68–74 for type/setpoint/caps/phase/vendor-state/detect/type-select.

**Architecture:** `App/services/heater.c` keeps its role (and most of its API) but becomes a pure relay: every 250 ms it writes the control block (0x11–0x15) and reads 0x10–0x45 in one transaction, decoding status into the legacy telemetry struct. It talks through the existing register-pointer `i2c_backend_t` seam; the on-target backend `drv_heater_i2c2.c` uses `HAL_I2C_Mem_*` on the already-enabled I2C2 (PA11/PB14). `wk2132.c`, `vevor_heater.c`, `drv_wk2132_i2c2.c` and their tests are deleted.

**Tech Stack:** C11, Unity/CMake host suite, STM32CubeIDE project `cube/` (HAL), arm-none-eabi 13.3.

**Spec:** `docs/superpowers/specs/2026-10-06-heater-coprocessor-design.md` (§7)

**Depends on:** Plan 1 (`heater-common/htr_i2c_map.h`, `htr_types.h`; Task 2 here also links `heater-coproc/core/htr_regs.c`, `htr_fsm.c`, `htr_iface.c` and Plan 1's fakes). Independent of Plan 2.

## Global Constraints

- Regs 53–67 keep their frozen meaning (cortex v1.2.40+ unchanged): reg 55 = legacy state (OFF/DETECTING/FAULT→0, STARTING→1, RUNNING→3, STOP_REQUESTED/COOLDOWN→4); reg 57 error = vendor fault code, 0xFF for a coprocessor FAULT without a code; reg 64 bits 0–4 same names, bit 4 XPORT_FAULT = G0B1↔coprocessor I²C failure within 3 s; reg 67 = I²C failure count.
- New regs: 68 type RO, 69 setpoint_c RW 5–30, 70 caps RO, 71 phase RO, 72 vendor_state RO (major<<8|sub), 73 detect RO (detect_result<<8|line_sense), 74 type_select RW. `MB_REG_MAX` 74, `MB_REG_LIMIT` 75.
- A coprocessor that does not ACK, or answers with a wrong DEVICE_ID / MAP_VERSION, is *absent*: never written, never a boot fault; identity retried every 1 s.
- Control block rewritten every 250 ms (keeps the coprocessor's host timeout alive and self-heals a coprocessor reset). HEATER_TYPE is written only on an explicit reg-74 write.
- Ambient sent to the coprocessor = `sensors_get_encl_temp_f()` converted to °C×10 (enclosure stand-in until a cab sensor exists).
- Commit messages end with:
  ```
  Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
  Claude-Session: https://claude.ai/code/session_017uxgfN1Y67gVKWmTWmpxxk
  ```

## Review Focus

1. **Heaterless unit (no coprocessor fitted):** boot must not fault, nothing written, regs 53–74 still readable → `test_no_coprocessor_at_boot` (Task 1).
2. **Coprocessor with an unknown map version** (future firmware on an old G0B1): must be left alone, not driven → `test_unknown_map_version_is_not_controlled` (Task 1).
3. **Coprocessor persisted a forced type while the G0B1 rebooted to defaults:** reg 74 must report what the coprocessor actually holds, and the G0B1 must not silently overwrite it → `test_type_select_forwarded_once_and_read_back` (Task 1).
4. **G0B1 alive and polling during a long run:** the coprocessor's host timeout must never fire → `test_g0b1_rewriting_run_keeps_host_timeout_alive` (Task 2).
5. **I²C glitch then recovery:** XPORT_FAULT sticky ~3 s then clears; lost after 5 s → re-identify (+ optional nRST hook) → `test_i2c_failure_sets_sticky_xport_and_reidentifies`, `test_reset_hook_called_when_lost` (Task 1).

## File Structure

| File | Change |
|---|---|
| `App/services/heater.h`, `heater.c` | rewrite: coprocessor client |
| `App/services/mbp_heater.h`, `mbp_heater.c` | regs 53–74 |
| `App/services/modbus_defs.h` | `MB_REG_MAX` 70→74, `MB_REG_LIMIT` 71→75 |
| `App/services/wk2132.[ch]`, `vevor_heater.[ch]` | delete |
| `Tests/fakes/fake_coproc_bus.[ch]` | new: register-image `i2c_backend_t` with failure injection |
| `Tests/test_heater.c`, `test_mbp_heater.c` | rewrite |
| `Tests/test_heater_e2e.c` | new: G0B1 client ↔ real coprocessor register file |
| `Tests/test_wk2132.c`, `test_vevor.c`, `fakes/fake_wk2132_i2c.[ch]` | delete |
| `cube/Core/Src/drv_heater_i2c2.c`, `cube/Core/Inc/drv_heater_i2c2.h` | new: I2C2 backend at 0x64 |
| `cube/Core/Src/drv_wk2132_i2c2.c`, `cube/Core/Inc/drv_wk2132_i2c2.h` | delete |
| `cube/Core/Src/app_main.c` | init/loop wiring |
| `cube/.project`, `cube/.cproject` | drop wk2132/vevor links; add `heater-common` include path |
| `docs/vevor-heater.md` | superseded note |

---

### Task 1: Heater service as coprocessor client + Modbus regs 53–74

**Files:**
- Create: `Tests/fakes/fake_coproc_bus.h`, `Tests/fakes/fake_coproc_bus.c`
- Replace: `App/services/heater.h`, `App/services/heater.c`, `App/services/mbp_heater.h`, `App/services/mbp_heater.c`, `Tests/test_heater.c`, `Tests/test_mbp_heater.c`
- Modify: `App/services/modbus_defs.h:32-34`, `Tests/CMakeLists.txt`
- Delete: `App/services/wk2132.c`, `App/services/wk2132.h`, `App/services/vevor_heater.c`, `App/services/vevor_heater.h`, `Tests/test_wk2132.c`, `Tests/test_vevor.c`, `Tests/fakes/fake_wk2132_i2c.c`, `Tests/fakes/fake_wk2132_i2c.h`

**Interfaces:**
- Consumes: `htr_i2c_map.h`, `htr_types.h` (Plan 1); `i2c_backend_t` (`App/services/i2c_backend.h`); `mb_reg_bind` (`mb_regmodel.h`).
- Produces: `void heater_init(const i2c_backend_t *bus, void (*reset_coproc)(void), uint32_t now)`; `heater_process(uint32_t)`; `heater_set_request/get_request`, `heater_set_level/get_level` (unchanged); new `heater_set_setpoint_c(int8_t)`, `int8_t heater_get_setpoint_c(void)`, `heater_set_type_select(uint8_t)`, `uint8_t heater_get_type_select(void)`, `heater_set_ambient_f(int16_t)`; `heater_telemetry_t` (legacy fields + `type, caps, phase, vendor_state, detect, present`); `HEATER_FLAG_*` (same bits); `HEATER_POLL_MS 250`, `HEATER_IDENT_RETRY_MS 1000`, `HEATER_LOST_MS 5000`, `HEATER_XPORT_STICKY_MS 3000`; `fake_coproc_t` + `fake_coproc_init()`.

- [ ] **Step 1: Write the failing tests and fake**

`Tests/fakes/fake_coproc_bus.h`:

````c
#ifndef FAKE_COPROC_BUS_H
#define FAKE_COPROC_BUS_H
/* i2c_backend_t over a plain 0x46-byte register image of the heater
   coprocessor, with failure injection and a write log. */
#include "i2c_backend.h"
#include <stdbool.h>
#include <stdint.h>

#define FAKE_COPROC_REGS 0x46u
typedef struct {
    i2c_backend_t api;
    uint8_t regs[FAKE_COPROC_REGS];
    bool fail;
    unsigned reads, writes;
    uint8_t last_write_reg;
    uint16_t last_write_len;
} fake_coproc_t;

/* Identity bytes preloaded (DEVICE_ID 0xEF, MAP_VERSION 1). */
void fake_coproc_init(fake_coproc_t *f);
#endif
````

`Tests/fakes/fake_coproc_bus.c`:

````c
#include "fake_coproc_bus.h"
#include "htr_i2c_map.h"
#include <string.h>

static int fc_read(void *ctx, uint8_t reg, uint8_t *buf, uint16_t len)
{
    fake_coproc_t *f = (fake_coproc_t *)ctx;
    uint16_t i;
    f->reads++;
    if (f->fail) return -1;
    for (i = 0; i < len; i++) buf[i] = ((reg + i) < FAKE_COPROC_REGS) ? f->regs[reg + i] : 0xFFu;
    return 0;
}
static int fc_write(void *ctx, uint8_t reg, const uint8_t *buf, uint16_t len)
{
    fake_coproc_t *f = (fake_coproc_t *)ctx;
    uint16_t i;
    f->writes++;
    if (f->fail) return -1;
    f->last_write_reg = reg;
    f->last_write_len = len;
    for (i = 0; i < len; i++) if ((reg + i) < FAKE_COPROC_REGS) f->regs[reg + i] = buf[i];
    return 0;
}
void fake_coproc_init(fake_coproc_t *f)
{
    memset(f, 0, sizeof(*f));
    f->regs[HTR_R_DEVICE_ID] = HTR_DEVICE_ID;
    f->regs[HTR_R_MAP_VERSION] = HTR_MAP_VERSION;
    f->regs[HTR_R_HEATER_TYPE] = 0xFFu;
    f->api.read = fc_read;
    f->api.write = fc_write;
    f->api.ctx = f;
}
````

`Tests/test_heater.c`:

````c
#include "heater.h"
#include "htr_i2c_map.h"
#include "htr_types.h"
#include "fake_coproc_bus.h"
#include "unity.h"
#include <string.h>

static fake_coproc_t c;
static uint32_t now;
static void run_to(uint32_t t){ while((int32_t)(t - now) > 0){ heater_process(now); now += 10U; } heater_process(now); }
static heater_telemetry_t tel(void){ heater_telemetry_t t; heater_get_telemetry(&t); return t; }
static void put16(uint8_t r, uint16_t v){ c.regs[r]=(uint8_t)v; c.regs[r+1]=(uint8_t)(v>>8); }

void setUp(void){ fake_coproc_init(&c); now = 0; heater_init(&c.api, NULL, now); }
void tearDown(void){}

void test_identifies_then_writes_control_block_every_250ms(void){
    heater_set_request(1); heater_set_level(6); heater_set_setpoint_c(22); heater_set_ambient_f(77);
    run_to(1000);
    TEST_ASSERT_TRUE(tel().present);
    TEST_ASSERT_EQUAL_UINT8(1, c.regs[HTR_R_RUN_REQUEST]);
    TEST_ASSERT_EQUAL_UINT8(6, c.regs[HTR_R_LEVEL]);
    TEST_ASSERT_EQUAL_INT8(22, (int8_t)c.regs[HTR_R_SETPOINT_C]);
    TEST_ASSERT_EQUAL_HEX16(250, (uint16_t)(c.regs[HTR_R_AMBIENT_C_X10] | (c.regs[HTR_R_AMBIENT_C_X10+1] << 8))); /* 77F = 25.0C */
    unsigned w = c.writes; run_to(2000);
    TEST_ASSERT_TRUE(c.writes - w >= 3 && c.writes - w <= 5);
}
void test_wrong_device_is_absent_and_never_written(void){
    c.regs[HTR_R_DEVICE_ID] = 0x12;
    heater_set_request(1); run_to(5000);
    TEST_ASSERT_FALSE(tel().present);
    TEST_ASSERT_EQUAL(0u, c.writes);
    TEST_ASSERT_TRUE(tel().flags & HEATER_FLAG_COMMS_FAULT);
}
void test_unknown_map_version_is_not_controlled(void){
    c.regs[HTR_R_MAP_VERSION] = 2;
    run_to(5000);
    TEST_ASSERT_FALSE(tel().present);
    TEST_ASSERT_EQUAL(0u, c.writes);
}
void test_status_decodes_into_legacy_and_new_fields(void){
    c.regs[HTR_R_PHASE] = HTR_PHASE_COOLDOWN; c.regs[HTR_R_ACTIVE_TYPE] = HTR_TYPE_AUTOTERM;
    put16(HTR_R_FLAGS, HTR_F_FRESH | HTR_F_PRESENT); put16(HTR_R_SUPPLY_MV, 12600);
    c.regs[HTR_R_VENDOR_STATE] = 4; c.regs[HTR_R_VENDOR_SUB] = 0;
    put16(HTR_R_CAPS, HTR_CAP_SETPOINT); c.regs[HTR_R_LINE_SENSE] = 0x22; c.regs[HTR_R_DETECT_RESULT] = HTR_DET_FOUND;
    run_to(1500);
    heater_telemetry_t t = tel();
    TEST_ASSERT_EQUAL_UINT8(4, t.state);                      /* legacy cooldown */
    TEST_ASSERT_EQUAL_UINT8(HTR_PHASE_COOLDOWN, t.phase);
    TEST_ASSERT_TRUE(t.flags & HEATER_FLAG_COOLDOWN);
    TEST_ASSERT_TRUE(t.flags & HEATER_FLAG_FRESH);
    TEST_ASSERT_EQUAL_UINT16(12600, t.supply_mv);
    TEST_ASSERT_EQUAL_HEX16(0x0400, t.vendor_state);
    TEST_ASSERT_EQUAL_HEX16(0x0222, t.detect);
    TEST_ASSERT_EQUAL_UINT8(HTR_TYPE_AUTOTERM, t.type);
}
void test_legacy_state_collapse(void){
    const uint8_t phase[7] = {0,1,2,3,4,5,6};
    const uint8_t legacy[7] = {0,0,1,3,4,4,0};
    for (int i = 0; i < 7; i++) {
        c.regs[HTR_R_PHASE] = phase[i]; run_to(now + 300);
        TEST_ASSERT_EQUAL_UINT8(legacy[i], tel().state);
    }
}
void test_fault_without_code_reports_ff(void){
    c.regs[HTR_R_PHASE] = HTR_PHASE_FAULT; c.regs[HTR_R_FAULT_CODE] = 0; run_to(1500);
    TEST_ASSERT_EQUAL_HEX8(0xFF, tel().error);
    c.regs[HTR_R_FAULT_CODE] = 7; run_to(2000);
    TEST_ASSERT_EQUAL_UINT8(7, tel().error);
}
void test_i2c_failure_sets_sticky_xport_and_reidentifies(void){
    unsigned resets_before;
    run_to(1500); TEST_ASSERT_TRUE(tel().present);
    c.fail = true; run_to(3000);
    TEST_ASSERT_TRUE(tel().flags & HEATER_FLAG_XPORT_FAULT);
    TEST_ASSERT_TRUE(tel().transport_errors > 0);
    run_to(8000);
    TEST_ASSERT_FALSE(tel().present);                         /* lost after 5 s */
    c.fail = false; resets_before = c.reads; run_to(10000);
    TEST_ASSERT_TRUE(tel().present);
    TEST_ASSERT_TRUE(c.reads > resets_before);
    run_to(14000);
    TEST_ASSERT_FALSE(tel().flags & HEATER_FLAG_XPORT_FAULT);  /* sticky window expired */
}
static unsigned s_resets; static void reset_hook(void){ s_resets++; }
void test_reset_hook_called_when_lost(void){
    s_resets = 0; heater_init(&c.api, reset_hook, now);
    run_to(1500); c.fail = true; run_to(8000);
    TEST_ASSERT_EQUAL(1u, s_resets);
}
void test_type_select_forwarded_once_and_read_back(void){
    run_to(1500);
    TEST_ASSERT_EQUAL_HEX8(0xFF, heater_get_type_select());
    unsigned w = c.writes; heater_set_type_select(2); run_to(1800);
    TEST_ASSERT_EQUAL_UINT8(2, c.regs[HTR_R_HEATER_TYPE]);
    run_to(3000);
    TEST_ASSERT_EQUAL_UINT8(2, heater_get_type_select());
    TEST_ASSERT_TRUE(c.writes - w <= 7);                      /* ctl each cycle + exactly one type write */
}
void test_setters_reject_out_of_range(void){
    heater_set_level(0); heater_set_level(11); TEST_ASSERT_EQUAL_UINT8(1, heater_get_level());
    heater_set_setpoint_c(4); heater_set_setpoint_c(31); TEST_ASSERT_EQUAL_INT8(20, heater_get_setpoint_c());
}
void test_ambient_conversion_rounds(void){
    heater_set_ambient_f(32); run_to(1500);
    TEST_ASSERT_EQUAL_HEX16(0, (uint16_t)(c.regs[HTR_R_AMBIENT_C_X10] | (c.regs[HTR_R_AMBIENT_C_X10+1] << 8)));
    heater_set_ambient_f(-4); run_to(2000);                    /* -20.0 C */
    TEST_ASSERT_EQUAL_INT16(-200, (int16_t)(c.regs[HTR_R_AMBIENT_C_X10] | (c.regs[HTR_R_AMBIENT_C_X10+1] << 8)));
}
/* Heaterless unit: nothing ACKs 0x64. Boot must not fault, nothing is
   written, and the service keeps retrying identity once a second. */
void test_no_coprocessor_at_boot(void){
    c.fail = true; run_to(10000);
    TEST_ASSERT_FALSE(tel().present);
    TEST_ASSERT_EQUAL(0u, c.writes);
    TEST_ASSERT_TRUE(c.reads >= 9 && c.reads <= 11);
    TEST_ASSERT_EQUAL_UINT8(0, tel().state);
}
int main(void){ UNITY_BEGIN();
    RUN_TEST(test_no_coprocessor_at_boot);
    RUN_TEST(test_identifies_then_writes_control_block_every_250ms);
    RUN_TEST(test_wrong_device_is_absent_and_never_written);
    RUN_TEST(test_unknown_map_version_is_not_controlled);
    RUN_TEST(test_status_decodes_into_legacy_and_new_fields);
    RUN_TEST(test_legacy_state_collapse);
    RUN_TEST(test_fault_without_code_reports_ff);
    RUN_TEST(test_i2c_failure_sets_sticky_xport_and_reidentifies);
    RUN_TEST(test_reset_hook_called_when_lost);
    RUN_TEST(test_type_select_forwarded_once_and_read_back);
    RUN_TEST(test_setters_reject_out_of_range);
    RUN_TEST(test_ambient_conversion_rounds);
    return UNITY_END(); }
````

`Tests/test_mbp_heater.c`:

````c
#include "unity.h"
#include "mbp_heater.h"
#include "heater.h"
#include "htr_i2c_map.h"
#include "htr_types.h"
#include "mb_regmodel.h"
#include "fake_coproc_bus.h"
#include <string.h>

static fake_coproc_t c;
void setUp(void) {
    fake_coproc_init(&c);
    heater_init(&c.api, NULL, 0U);
    mb_reg_reset();
    mbp_heater_register();
}
void tearDown(void) {}

static void test_write_request_and_level(void) {
    TEST_ASSERT_EQUAL_INT(MB_EXC_NONE, mb_reg_write(53U, 1U));
    TEST_ASSERT_EQUAL_UINT8(1U, heater_get_request());
    TEST_ASSERT_EQUAL_INT(MB_EXC_NONE, mb_reg_write(54U, 7U));
    TEST_ASSERT_EQUAL_UINT8(7U, heater_get_level());
}
static void test_out_of_range_rejected(void) {
    TEST_ASSERT_EQUAL_INT(MB_EXC_ILLEGAL_VALUE, mb_reg_write(53U, 2U));
    TEST_ASSERT_EQUAL_INT(MB_EXC_ILLEGAL_VALUE, mb_reg_write(54U, 11U));
    TEST_ASSERT_EQUAL_INT(MB_EXC_ILLEGAL_VALUE, mb_reg_write(54U, 0U));
    TEST_ASSERT_EQUAL_INT(MB_EXC_ILLEGAL_VALUE, mb_reg_write(69U, 31U));
    TEST_ASSERT_EQUAL_INT(MB_EXC_ILLEGAL_VALUE, mb_reg_write(69U, 4U));
    TEST_ASSERT_EQUAL_INT(MB_EXC_ILLEGAL_VALUE, mb_reg_write(74U, 0x100U));
}
static void test_setpoint_and_type_select_rw(void) {
    uint16_t v;
    TEST_ASSERT_EQUAL_INT(MB_EXC_NONE, mb_reg_write(69U, 24U));
    TEST_ASSERT_EQUAL_INT(MB_EXC_NONE, mb_reg_read(69U, &v)); TEST_ASSERT_EQUAL_UINT16(24U, v);
    TEST_ASSERT_EQUAL_INT(MB_EXC_NONE, mb_reg_write(74U, 1U));
    TEST_ASSERT_EQUAL_INT(MB_EXC_NONE, mb_reg_read(74U, &v)); TEST_ASSERT_EQUAL_UINT16(1U, v);
}
static void test_ro_regs_reject_writes(void) {
    const uint16_t ro[] = {55,56,57,58,59,60,61,62,63,64,65,66,67,68,70,71,72,73};
    for (unsigned i = 0; i < sizeof ro / sizeof ro[0]; i++)
        TEST_ASSERT_EQUAL_INT_MESSAGE(MB_EXC_ILLEGAL_ADDRESS, mb_reg_write(ro[i], 1U), "RO reg accepted a write");
}
static void test_block_53_to_74_readable_and_75_out_of_range(void) {
    uint16_t v;
    for (uint16_t r = 53U; r <= 74U; r++) TEST_ASSERT_EQUAL_INT_MESSAGE(MB_EXC_NONE, mb_reg_read(r, &v), "reg not readable");
    TEST_ASSERT_EQUAL_INT(MB_EXC_ILLEGAL_ADDRESS, mb_reg_read(75U, &v));
}
static void test_telemetry_mirrors_coprocessor(void) {
    c.regs[HTR_R_PHASE] = HTR_PHASE_RUNNING; c.regs[HTR_R_ACTIVE_TYPE] = HTR_TYPE_VEVOR;
    c.regs[HTR_R_ACTIVE_LEVEL] = 4; c.regs[HTR_R_SUPPLY_MV] = 0x38; c.regs[HTR_R_SUPPLY_MV+1] = 0x31; /* 12600 */
    c.regs[HTR_R_VENDOR_STATE] = 3; c.regs[HTR_R_CAPS] = HTR_CAP_LEVEL;
    for (uint32_t t = 0; t <= 1500; t += 10) heater_process(t);
    uint16_t v;
    mb_reg_read(55U, &v); TEST_ASSERT_EQUAL_UINT16(3U, v);
    mb_reg_read(56U, &v); TEST_ASSERT_EQUAL_UINT16(4U, v);
    mb_reg_read(58U, &v); TEST_ASSERT_EQUAL_UINT16(12600U, v);
    mb_reg_read(68U, &v); TEST_ASSERT_EQUAL_UINT16(HTR_TYPE_VEVOR, v);
    mb_reg_read(70U, &v); TEST_ASSERT_EQUAL_UINT16(HTR_CAP_LEVEL, v);
    mb_reg_read(71U, &v); TEST_ASSERT_EQUAL_UINT16(HTR_PHASE_RUNNING, v);
    mb_reg_read(72U, &v); TEST_ASSERT_EQUAL_HEX16(0x0300, v);
}
int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_write_request_and_level);
    RUN_TEST(test_out_of_range_rejected);
    RUN_TEST(test_setpoint_and_type_select_rw);
    RUN_TEST(test_ro_regs_reject_writes);
    RUN_TEST(test_block_53_to_74_readable_and_75_out_of_range);
    RUN_TEST(test_telemetry_mirrors_coprocessor);
    return UNITY_END();
}
````

In `Tests/CMakeLists.txt`, delete these lines (the old WK2132/VEVOR targets):

```cmake
add_unity_test(test_wk2132 test_wk2132.c ../App/services/wk2132.c fakes/fake_wk2132_i2c.c)
# test_vevor loads Tests/fixtures/*.hex via paths relative to the test's cwd.
# ctest's default WORKING_DIRECTORY for add_test() is CMAKE_CURRENT_BINARY_DIR
# (the build dir), not the source dir where fixtures/ lives, so pin it here.
add_unity_test(test_vevor test_vevor.c ../App/services/vevor_heater.c ../App/services/wk2132.c fakes/fake_wk2132_i2c.c)
set_tests_properties(test_vevor PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
add_unity_test(test_heater test_heater.c ../App/services/heater.c ../App/services/vevor_heater.c ../App/services/wk2132.c fakes/fake_wk2132_i2c.c)
add_unity_test(test_mbp_heater test_mbp_heater.c ../App/services/mbp_heater.c ../App/services/heater.c ../App/services/vevor_heater.c ../App/services/wk2132.c ../App/services/mb_regmodel.c fakes/fake_wk2132_i2c.c)
```

and append (after Plan 1's heater block, which defines `add_heater_test`):

```cmake
add_heater_test(test_heater test_heater.c ../App/services/heater.c fakes/fake_coproc_bus.c)
add_heater_test(test_mbp_heater test_mbp_heater.c ../App/services/mbp_heater.c ../App/services/heater.c ../App/services/mb_regmodel.c fakes/fake_coproc_bus.c)
```

Then delete the retired sources:

```bash
git rm App/services/wk2132.c App/services/wk2132.h App/services/vevor_heater.c App/services/vevor_heater.h \
       Tests/test_wk2132.c Tests/test_vevor.c Tests/fakes/fake_wk2132_i2c.c Tests/fakes/fake_wk2132_i2c.h
```

(The VEVOR protocol and its 1221-frame regression now live in `heater-common/vevor_proto.c` / `Tests/test_vevor_proto.c` from Plan 1. `Tests/fixtures/capture*.hex` stay — `test_vevor_proto` uses them.)

- [ ] **Step 2: Run tests to verify they fail**

Run: `cmake -S Tests -B Tests/build >/dev/null && cmake --build Tests/build --target test_heater test_mbp_heater`
Expected: FAIL — compile errors (`heater_init` has 3 args / unknown `heater_set_setpoint_c`, `HTR_*` undeclared in the old `heater.h`).

- [ ] **Step 3: Write the implementation**

`App/services/heater.h`:

````c
#ifndef HEATER_H
#define HEATER_H
/* G0B1 heater service: I2C client of the heater coprocessor (0x64, map
   heater-common/htr_i2c_map.h). The coprocessor owns every heater protocol
   and the start/run/cooldown state machine; this service only relays the
   request and mirrors status into the Modbus provider (mbp_heater). */
#include "i2c_backend.h"
#include <stdint.h>

typedef struct {
    uint8_t  state;             /* legacy 0 off,1 starting,3 running,4 cooldown (reg 55) */
    uint8_t  active_level;
    uint8_t  target_level;
    uint8_t  error;             /* vendor fault code; 0xFF = coprocessor fault w/o code */
    uint16_t supply_mv;
    uint16_t fan_rpm;
    uint16_t pump_hz_x10;
    uint16_t exchanger_raw;
    uint16_t state_seconds;
    uint16_t age_ms;
    uint16_t flags;             /* HEATER_FLAG_* (legacy reg 64 meaning) */
    uint16_t valid_frames;
    uint16_t checksum_failures;
    uint16_t transport_errors;  /* G0B1<->coprocessor I2C failures, saturating */
    /* regs 68-74 */
    uint8_t  type;              /* ACTIVE_TYPE */
    uint16_t caps;
    uint8_t  phase;             /* full generic phase 0..6 */
    uint16_t vendor_state;      /* major<<8 | sub */
    uint16_t detect;            /* DETECT_RESULT<<8 | LINE_SENSE */
    uint8_t  present;           /* coprocessor identified (DEVICE_ID + MAP_VERSION ok) */
} heater_telemetry_t;

#define HEATER_FLAG_FRESH       (1U << 0)
#define HEATER_FLAG_COOLDOWN    (1U << 1) /* phase STOP_REQUESTED or COOLDOWN */
#define HEATER_FLAG_SAFE_OFF    (1U << 2)
#define HEATER_FLAG_COMMS_FAULT (1U << 3) /* run requested but heater link not fresh */
#define HEATER_FLAG_XPORT_FAULT (1U << 4) /* G0B1<->coprocessor I2C failed within 3 s */

#define HEATER_POLL_MS          250U
#define HEATER_IDENT_RETRY_MS  1000U
#define HEATER_LOST_MS         5000U
#define HEATER_XPORT_STICKY_MS 3000U

/* reset_coproc may be NULL (no nRST GPIO wired). Never blocks boot. */
void    heater_init(const i2c_backend_t *bus, void (*reset_coproc)(void), uint32_t now);
void    heater_process(uint32_t now);
void    heater_set_request(uint8_t on);
uint8_t heater_get_request(void);
void    heater_set_level(uint8_t level);          /* 1..10, else ignored */
uint8_t heater_get_level(void);
void    heater_set_setpoint_c(int8_t c);          /* 5..30, else ignored */
int8_t  heater_get_setpoint_c(void);
void    heater_set_type_select(uint8_t type);     /* HTR_TYPE_*; forwarded once per call */
uint8_t heater_get_type_select(void);
void    heater_set_ambient_f(int16_t deg_f);      /* cabin temp for AUTOTERM regulation */
void    heater_get_telemetry(heater_telemetry_t *out);
#endif /* HEATER_H */
````

`App/services/heater.c`:

````c
#include "heater.h"
#include "htr_i2c_map.h"
#include "htr_types.h"
#include <string.h>

static const i2c_backend_t *s_bus;
static void (*s_reset)(void);
static uint8_t  s_request;
static uint8_t  s_level = 1U;
static int8_t   s_setpoint = 20;
static int16_t  s_ambient_x10 = HTR_AMBIENT_UNKNOWN;
static uint8_t  s_type_select = HTR_TYPE_AUTO;
static uint8_t  s_type_pending;
static uint8_t  s_coproc_type = HTR_TYPE_AUTO;   /* HEATER_TYPE as read back */
static uint8_t  s_identified;
static uint32_t s_last_poll, s_last_ok, s_last_xport;
static uint8_t  s_xport_seen;
static uint32_t s_xport_errors;
static heater_telemetry_t s_t;

static uint16_t le16(const uint8_t *b, uint8_t reg) { return (uint16_t)(b[reg] | ((uint16_t)b[reg + 1U] << 8)); }
static uint16_t sat16(uint32_t v) { return (v > 0xFFFFU) ? 0xFFFFU : (uint16_t)v; }

void heater_init(const i2c_backend_t *bus, void (*reset_coproc)(void), uint32_t now)
{
    s_bus = bus;
    s_reset = reset_coproc;
    s_request = 0U;
    s_level = 1U;
    s_setpoint = 20;
    s_ambient_x10 = HTR_AMBIENT_UNKNOWN;
    s_type_select = HTR_TYPE_AUTO;
    s_type_pending = 0U;
    s_coproc_type = HTR_TYPE_AUTO;
    s_identified = 0U;
    s_last_poll = now - HEATER_IDENT_RETRY_MS;
    s_last_ok = now;
    s_xport_seen = 0U;
    s_xport_errors = 0U;
    memset(&s_t, 0, sizeof(s_t));
    s_t.target_level = 1U;
    s_t.age_ms = 0xFFFFU;
}

static void xport_fail(uint32_t now)
{
    s_xport_errors++;
    s_xport_seen = 1U;
    s_last_xport = now;
}

static uint8_t legacy_state(uint8_t phase)
{
    switch (phase) {
    case HTR_PHASE_STARTING:       return 1U;
    case HTR_PHASE_RUNNING:        return 3U;
    case HTR_PHASE_STOP_REQUESTED:
    case HTR_PHASE_COOLDOWN:       return 4U;
    default:                       return 0U;   /* OFF, DETECTING, FAULT */
    }
}

static void decode_status(const uint8_t *m)
{
    uint16_t f = le16(m, HTR_R_FLAGS);
    uint16_t legacy = 0U;
    s_t.phase = m[HTR_R_PHASE];
    s_t.state = legacy_state(s_t.phase);
    s_t.type = m[HTR_R_ACTIVE_TYPE];
    s_t.active_level = m[HTR_R_ACTIVE_LEVEL];
    s_t.error = m[HTR_R_FAULT_CODE];
    if ((s_t.phase == HTR_PHASE_FAULT) && (s_t.error == 0U)) s_t.error = 0xFFU;
    s_t.supply_mv = le16(m, HTR_R_SUPPLY_MV);
    s_t.fan_rpm = le16(m, HTR_R_FAN_RPM);
    s_t.pump_hz_x10 = le16(m, HTR_R_PUMP_HZ_X10);
    s_t.exchanger_raw = le16(m, HTR_R_EXCH_RAW);
    s_t.state_seconds = le16(m, HTR_R_STATE_SECONDS);
    s_t.age_ms = le16(m, HTR_R_AGE_MS);
    s_t.valid_frames = le16(m, HTR_R_VALID_FRAMES);
    s_t.checksum_failures = le16(m, HTR_R_CRC_FAILS);
    s_t.caps = le16(m, HTR_R_CAPS);
    s_t.vendor_state = (uint16_t)(((uint16_t)m[HTR_R_VENDOR_STATE] << 8) | m[HTR_R_VENDOR_SUB]);
    s_t.detect = (uint16_t)(((uint16_t)m[HTR_R_DETECT_RESULT] << 8) | m[HTR_R_LINE_SENSE]);
    if (f & HTR_F_FRESH) legacy |= HEATER_FLAG_FRESH;
    if ((s_t.phase == HTR_PHASE_STOP_REQUESTED) || (s_t.phase == HTR_PHASE_COOLDOWN)) legacy |= HEATER_FLAG_COOLDOWN;
    if (f & HTR_F_SAFE_OFF) legacy |= HEATER_FLAG_SAFE_OFF;
    if (f & HTR_F_COMMS_FAULT) legacy |= HEATER_FLAG_COMMS_FAULT;
    s_t.flags = legacy;
}

static int identify(void)
{
    uint8_t id[2];
    if (s_bus->read(s_bus->ctx, HTR_R_DEVICE_ID, id, 2U) != 0) return -1;
    return ((id[0] == HTR_DEVICE_ID) && (id[1] == HTR_MAP_VERSION)) ? 1 : 0;
}

void heater_process(uint32_t now)
{
    uint8_t m[HTR_REG_COUNT];
    uint8_t ctl[5];
    int r;
    if (s_bus == NULL) return;

    if (s_identified == 0U) {
        if ((uint32_t)(now - s_last_poll) < HEATER_IDENT_RETRY_MS) goto publish;
        s_last_poll = now;
        r = identify();
        if (r < 0) { xport_fail(now); goto publish; }
        if (r == 0) goto publish;            /* wrong device / map: never control it */
        s_identified = 1U;
        s_last_ok = now;
    }
    if ((uint32_t)(now - s_last_poll) < HEATER_POLL_MS) goto publish;
    s_last_poll = now;

    ctl[0] = s_request;
    ctl[1] = s_level;
    ctl[2] = (uint8_t)s_setpoint;
    ctl[3] = (uint8_t)((uint16_t)s_ambient_x10 & 0xFFU);
    ctl[4] = (uint8_t)((uint16_t)s_ambient_x10 >> 8);
    if (s_bus->write(s_bus->ctx, HTR_R_RUN_REQUEST, ctl, sizeof(ctl)) != 0) { xport_fail(now); goto lost_check; }
    if (s_type_pending) {
        if (s_bus->write(s_bus->ctx, HTR_R_HEATER_TYPE, &s_type_select, 1U) != 0) { xport_fail(now); goto lost_check; }
        s_type_pending = 0U;
    }
    /* One read covers control readback (0x10..) and the status block. */
    if (s_bus->read(s_bus->ctx, HTR_R_HEATER_TYPE, &m[HTR_R_HEATER_TYPE],
                    (uint16_t)(HTR_REG_COUNT - HTR_R_HEATER_TYPE)) != 0) {
        xport_fail(now);
        goto lost_check;
    }
    s_last_ok = now;
    s_coproc_type = m[HTR_R_HEATER_TYPE];
    decode_status(m);
    goto publish;

lost_check:
    if ((uint32_t)(now - s_last_ok) >= HEATER_LOST_MS) {
        s_identified = 0U;                    /* re-identify; optional hard reset */
        s_last_ok = now;
        if (s_reset != NULL) s_reset();
    }
publish:
    s_t.present = s_identified;
    s_t.target_level = s_level;
    s_t.transport_errors = sat16(s_xport_errors);
    if (s_xport_seen && ((uint32_t)(now - s_last_xport) <= HEATER_XPORT_STICKY_MS)) {
        s_t.flags |= HEATER_FLAG_XPORT_FAULT;
    } else {
        s_t.flags &= (uint16_t)~HEATER_FLAG_XPORT_FAULT;
    }
    if (s_identified == 0U) {
        s_t.flags &= (uint16_t)~(HEATER_FLAG_FRESH | HEATER_FLAG_SAFE_OFF);
        if (s_request) s_t.flags |= HEATER_FLAG_COMMS_FAULT;
    }
}

void    heater_set_request(uint8_t on) { s_request = (on != 0U) ? 1U : 0U; }
uint8_t heater_get_request(void) { return s_request; }
void    heater_set_level(uint8_t level) { if ((level >= 1U) && (level <= 10U)) s_level = level; }
uint8_t heater_get_level(void) { return s_level; }
void    heater_set_setpoint_c(int8_t c) { if ((c >= 5) && (c <= 30)) s_setpoint = c; }
int8_t  heater_get_setpoint_c(void) { return s_setpoint; }
void    heater_set_type_select(uint8_t type)
{
    s_type_select = type;      /* explicit Modbus write: always forwarded once */
    s_type_pending = 1U;
}
/* The coprocessor persists HEATER_TYPE, so report what it holds once known. */
uint8_t heater_get_type_select(void) { return s_identified ? s_coproc_type : s_type_select; }
void    heater_set_ambient_f(int16_t deg_f)
{
    /* (F - 32) * 5/9, in tenths, rounded */
    int32_t x10 = ((int32_t)deg_f - 32) * 50;
    s_ambient_x10 = (int16_t)((x10 >= 0) ? ((x10 + 4) / 9) : ((x10 - 4) / 9));
}
void heater_get_telemetry(heater_telemetry_t *out) { if (out != NULL) *out = s_t; }
````

`App/services/mbp_heater.h`:

````c
#ifndef MBP_HEATER_H
#define MBP_HEATER_H

/* Registers 53-74: heater command + telemetry block (heater coprocessor). Register
   numbers are the frozen Modbus contract consumed by the cortex/gobi-agent
   (Sub-project #2) -- do not renumber.
     53 heater_request      RW  0/1
     54 heater_level        RW  1..10
     55 state               RO
     56 active_level        RO
     57 error                RO
     58 supply_mv           RO
     59 fan_rpm             RO
     60 pump_hz_x10         RO
     61 exchanger_raw       RO
     62 state_seconds       RO
     63 age_ms              RO
     64 flags               RO
     65 valid_frames        RO
     66 checksum_failures   RO
     67 transport_errors    RO  (G0B1<->coprocessor I2C failures)
     68 heater_type         RO  active type (0 none, 1 VEVOR, 2 AUTOTERM)
     69 heater_setpoint_c   RW  5..30 (setpoint-type heaters)
     70 heater_caps         RO
     71 heater_phase        RO  0 off,1 detecting,2 starting,3 running,4 stop_req,5 cooldown,6 fault
     72 heater_vendor_state RO  major<<8 | sub
     73 heater_detect       RO  detect_result<<8 | line_sense
     74 heater_type_select  RW  0xFF auto, or forced type (bench)
*/
void mbp_heater_register(void);

#endif /* MBP_HEATER_H */
````

`App/services/mbp_heater.c`:

````c
#include "mbp_heater.h"
#include "mb_regmodel.h"
#include "heater.h"

static heater_telemetry_t tel(void) { heater_telemetry_t t; heater_get_telemetry(&t); return t; }

static modbus_exc_t rd_request(uint16_t reg, uint16_t *o) { (void)reg; *o = heater_get_request(); return MB_EXC_NONE; }
static modbus_exc_t wr_request(uint16_t reg, uint16_t v) {
    (void)reg;
    if (v > 1U) { return MB_EXC_ILLEGAL_VALUE; }
    heater_set_request((uint8_t)v);
    return MB_EXC_NONE;
}
static modbus_exc_t rd_level(uint16_t reg, uint16_t *o) { (void)reg; *o = heater_get_level(); return MB_EXC_NONE; }
static modbus_exc_t wr_level(uint16_t reg, uint16_t v) {
    (void)reg;
    if ((v < 1U) || (v > 10U)) { return MB_EXC_ILLEGAL_VALUE; }
    heater_set_level((uint8_t)v);
    return MB_EXC_NONE;
}
static modbus_exc_t rd_setpoint(uint16_t reg, uint16_t *o) { (void)reg; *o = (uint16_t)(int16_t)heater_get_setpoint_c(); return MB_EXC_NONE; }
static modbus_exc_t wr_setpoint(uint16_t reg, uint16_t v) {
    (void)reg;
    if ((v < 5U) || (v > 30U)) { return MB_EXC_ILLEGAL_VALUE; }
    heater_set_setpoint_c((int8_t)v);
    return MB_EXC_NONE;
}
static modbus_exc_t rd_type_select(uint16_t reg, uint16_t *o) { (void)reg; *o = heater_get_type_select(); return MB_EXC_NONE; }
static modbus_exc_t wr_type_select(uint16_t reg, uint16_t v) {
    (void)reg;
    if (v > 0xFFU) { return MB_EXC_ILLEGAL_VALUE; }   /* coprocessor validates the id */
    heater_set_type_select((uint8_t)v);
    return MB_EXC_NONE;
}

/* Read-only telemetry: one tiny reader per register. */
#define RO(name, expr) \
    static modbus_exc_t name(uint16_t reg, uint16_t *o) { heater_telemetry_t t = tel(); (void)reg; *o = (uint16_t)(expr); return MB_EXC_NONE; }
RO(rd_state, t.state)
RO(rd_active_level, t.active_level)
RO(rd_error, t.error)
RO(rd_supply_mv, t.supply_mv)
RO(rd_fan_rpm, t.fan_rpm)
RO(rd_pump_hz_x10, t.pump_hz_x10)
RO(rd_exchanger_raw, t.exchanger_raw)
RO(rd_state_seconds, t.state_seconds)
RO(rd_age_ms, t.age_ms)
RO(rd_flags, t.flags)
RO(rd_valid_frames, t.valid_frames)
RO(rd_checksum_failures, t.checksum_failures)
RO(rd_transport_errors, t.transport_errors)
RO(rd_type, t.type)
RO(rd_caps, t.caps)
RO(rd_phase, t.phase)
RO(rd_vendor_state, t.vendor_state)
RO(rd_detect, t.detect)

void mbp_heater_register(void) {
    mb_reg_bind(53, rd_request,           wr_request);
    mb_reg_bind(54, rd_level,             wr_level);
    mb_reg_bind(55, rd_state,             0);
    mb_reg_bind(56, rd_active_level,      0);
    mb_reg_bind(57, rd_error,             0);
    mb_reg_bind(58, rd_supply_mv,         0);
    mb_reg_bind(59, rd_fan_rpm,           0);
    mb_reg_bind(60, rd_pump_hz_x10,       0);
    mb_reg_bind(61, rd_exchanger_raw,     0);
    mb_reg_bind(62, rd_state_seconds,     0);
    mb_reg_bind(63, rd_age_ms,            0);
    mb_reg_bind(64, rd_flags,             0);
    mb_reg_bind(65, rd_valid_frames,      0);
    mb_reg_bind(66, rd_checksum_failures, 0);
    mb_reg_bind(67, rd_transport_errors,  0);
    mb_reg_bind(68, rd_type,              0);
    mb_reg_bind(69, rd_setpoint,          wr_setpoint);
    mb_reg_bind(70, rd_caps,              0);
    mb_reg_bind(71, rd_phase,             0);
    mb_reg_bind(72, rd_vendor_state,      0);
    mb_reg_bind(73, rd_detect,            0);
    mb_reg_bind(74, rd_type_select,       wr_type_select);
}
````

In `App/services/modbus_defs.h` replace:

```c
/* Register model. Registers are 1..70; wire range check is (start+count) <= 71. */
#define MB_REG_MAX     70u
#define MB_REG_LIMIT   71u
```

with:

```c
/* Register model. Registers are 1..74; wire range check is (start+count) <= 75. */
#define MB_REG_MAX     74u
#define MB_REG_LIMIT   75u
```

- [ ] **Step 4: Run tests to verify they pass, then the whole suite**

Run: `cmake --build Tests/build --target test_heater test_mbp_heater && (cd Tests/build && ctest -R '^test_(mbp_)?heater$' --output-on-failure)`
Expected: PASS (`test_heater` 12 tests, `test_mbp_heater` 6 tests).
Run: `(cd Tests/build && cmake --build . && ctest)`
Expected: `100% tests passed` (the Modbus engine/regmodel tests size their tables from `MB_REG_MAX`, so they follow the bump).

- [ ] **Step 5: Commit**

```bash
git add -A App/services Tests
git commit -m "feat(heater): G0B1 heater service becomes the coprocessor I2C client

Relays run/level/setpoint/ambient to the 0x64 coprocessor every 250 ms and
mirrors its status. Regs 53-67 keep their meaning; adds 68-74 (type,
setpoint, caps, phase, vendor state, detect, type select). Retires the
WK2132 bridge and the in-MCU VEVOR protocol (now in heater-common).

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_017uxgfN1Y67gVKWmTWmpxxk"
```

---

### Task 2: End-to-end host test — G0B1 client ↔ real coprocessor register file

**Files:**
- Create: `Tests/test_heater_e2e.c`
- Modify: `Tests/CMakeLists.txt` (append)

**Interfaces:**
- Consumes: `heater_*` (Task 1); `htr_regs_*`, `htr_fsm_*`, `fake_drv`, `fake_gpio_t`, `fake_uart_t` (Plan 1).

- [ ] **Step 1: Write the test**

`Tests/test_heater_e2e.c`:

````c
/* End-to-end on the host: G0B1 heater client -> I2C bridge -> REAL coprocessor
   register file + FSM (fake heater driver). Proves the two sides of the
   frozen htr_i2c_map.h contract agree. */
#include "heater.h"
#include "htr_regs.h"
#include "htr_fsm.h"
#include "htr_i2c_map.h"
#include "fake_htr_driver.h"
#include "fake_htr_gpio.h"
#include "fake_htr_uart.h"
#include "unity.h"

static fake_gpio_t g; static fake_uart_t u3, u4;
static const htr_driver_t *const drvs[] = { &fake_drv[0], &fake_drv[1] };
static uint32_t now;

/* Mem_Write = START addr+W, ptr, data..., STOP. Mem_Read = START addr+W, ptr,
   RESTART addr+R, data..., STOP (no STOP between the two phases). */
static int br_read(void *ctx, uint8_t reg, uint8_t *buf, uint16_t len){
    (void)ctx; htr_regs_i2c_begin(false); htr_regs_i2c_rx(reg);
    htr_regs_i2c_begin(true); for(uint16_t i=0;i<len;i++) buf[i]=htr_regs_i2c_tx(); htr_regs_i2c_end(); return 0;
}
static int br_write(void *ctx, uint8_t reg, const uint8_t *buf, uint16_t len){
    (void)ctx; htr_regs_i2c_begin(false); htr_regs_i2c_rx(reg);
    for(uint16_t i=0;i<len;i++) htr_regs_i2c_rx(buf[i]); htr_regs_i2c_end(); return 0;
}
static const i2c_backend_t bridge = { br_read, br_write, 0 };

static void both_to(uint32_t t){
    for(; now < t; now += 10U){
        htr_regs_poll(now); htr_fsm_process(now); htr_regs_publish(now, 0);
        heater_process(now);
    }
}
static heater_telemetry_t tel(void){ heater_telemetry_t t; heater_get_telemetry(&t); return t; }

void setUp(void){
    fake_drivers_reset(); fake_gpio_init(&g); fake_uart_init(&u3); fake_uart_init(&u4); now = 0;
    g.high_pct[HTR_WIRING_3W] = 100; fake_ctx[0].probe_answer = HTR_PROBE_FOUND;
    htr_regs_cfg_t rc = { 0, HTR_HW_ID_EF_L011, (1u<<1)|(1u<<2), 0, 0 };
    uint8_t type = htr_regs_init(&rc, now);
    htr_fsm_cfg_t fc = { drvs, 2, &g.api, false, &u3.api, &u4.api };
    htr_fsm_init(&fc, type, now);
    heater_init(&bridge, 0, now);
}
void tearDown(void){}

void test_full_chain_detect_start_run_stop_cooldown(void){
    both_to(7000);
    heater_telemetry_t t = tel();
    TEST_ASSERT_TRUE(t.present);
    TEST_ASSERT_EQUAL_UINT8(1, t.type);
    TEST_ASSERT_EQUAL_UINT8(HTR_PHASE_OFF, t.phase);
    TEST_ASSERT_TRUE(t.flags & HEATER_FLAG_SAFE_OFF);
    heater_set_level(8); heater_set_request(1); both_to(7600);
    TEST_ASSERT_EQUAL(1u, fake_ctx[0].starts);
    TEST_ASSERT_EQUAL_UINT8(8, fake_ctx[0].last_rq.level);
    TEST_ASSERT_EQUAL_UINT8(1, tel().state);                  /* legacy starting */
    fake_ctx[0].hint = HTR_HINT_RUNNING; both_to(8500);
    TEST_ASSERT_EQUAL_UINT8(3, tel().state);
    heater_set_request(0); both_to(9000);
    TEST_ASSERT_EQUAL(1u, fake_ctx[0].stops);
    fake_ctx[0].hint = HTR_HINT_COOLDOWN; both_to(10000);
    TEST_ASSERT_EQUAL_UINT8(HTR_PHASE_COOLDOWN, tel().phase);
    TEST_ASSERT_TRUE(tel().flags & HEATER_FLAG_COOLDOWN);
    fake_ctx[0].hint = HTR_HINT_OFF; both_to(11000);
    TEST_ASSERT_EQUAL_UINT8(0, tel().state);
    TEST_ASSERT_TRUE(tel().flags & HEATER_FLAG_SAFE_OFF);
}
void test_g0b1_rewriting_run_keeps_host_timeout_alive(void){
    both_to(7000); heater_set_request(1); fake_ctx[0].hint = HTR_HINT_RUNNING;
    both_to(7000 + 15u * 60000u);                             /* 15 min > default 10 min timeout */
    TEST_ASSERT_EQUAL(0u, fake_ctx[0].stops);
    TEST_ASSERT_EQUAL_UINT8(HTR_PHASE_RUNNING, tel().phase);
}
int main(void){ UNITY_BEGIN();
    RUN_TEST(test_full_chain_detect_start_run_stop_cooldown);
    RUN_TEST(test_g0b1_rewriting_run_keeps_host_timeout_alive);
    return UNITY_END(); }
````

Append to `Tests/CMakeLists.txt`:

```cmake
add_heater_test(test_heater_e2e test_heater_e2e.c ../App/services/heater.c ${HCORE}/htr_regs.c ${HCORE}/htr_fsm.c ${HCORE}/htr_iface.c fakes/fake_htr_driver.c fakes/fake_htr_gpio.c fakes/fake_htr_uart.c)
```

- [ ] **Step 2: Run it**

Run: `cmake -S Tests -B Tests/build >/dev/null && cmake --build Tests/build --target test_heater_e2e && (cd Tests/build && ctest -R '^test_heater_e2e$' --output-on-failure)`
Expected: PASS (2 tests). This test exercises code from Task 1 and Plan 1 together; it has no RED phase of its own. To prove it can fail, temporarily change `HTR_R_RUN_REQUEST` to `0x12` in `heater.c`'s control write — the test must FAIL (`starts` stays 0) — then revert.

- [ ] **Step 3: Commit**

```bash
git add Tests/test_heater_e2e.c Tests/CMakeLists.txt
git commit -m "test(heater): end-to-end G0B1 client <-> coprocessor register file

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_017uxgfN1Y67gVKWmTWmpxxk"
```

---

### Task 3: On-target integration (I2C2 backend, app_main, CubeIDE project)

**Files:**
- Create: `cube/Core/Inc/drv_heater_i2c2.h`, `cube/Core/Src/drv_heater_i2c2.c`
- Delete: `cube/Core/Inc/drv_wk2132_i2c2.h`, `cube/Core/Src/drv_wk2132_i2c2.c`
- Modify: `cube/Core/Src/app_main.c`, `cube/.project`, `cube/.cproject`, `docs/vevor-heater.md`

**Interfaces:**
- Consumes: `heater_init(const i2c_backend_t*, void (*)(void), uint32_t)`, `heater_set_ambient_f(int16_t)` (Task 1); `sensors_get_encl_temp_f()` (`App/services/sensors.h`); `hi2c2` (CubeMX, already enabled).
- Produces: `const i2c_backend_t *drv_heater_i2c2_backend(void)`.

- [ ] **Step 1: Write the I2C2 backend; delete the WK2132 one**

`cube-g0b1/Core/Inc/drv_heater_i2c2.h`:

````c
#ifndef DRV_HEATER_I2C2_H
#define DRV_HEATER_I2C2_H
/* I2C2 (PA11 SCL / PB14 SDA, 100 kHz) backend for the heater service: a
   register-pointer i2c_backend_t bound to the heater coprocessor at
   HTR_I2C_ADDR7 (0x64). Same HAL_I2C_Mem_* pattern as drv_mcp7940n.c. */
#include "i2c_backend.h"

const i2c_backend_t *drv_heater_i2c2_backend(void);

#endif /* DRV_HEATER_I2C2_H */
````

`cube-g0b1/Core/Src/drv_heater_i2c2.c`:

````c
/* drv_heater_i2c2.c -- I2C2 transport to the heater coprocessor (0x64).
 * Pure transport: register semantics live in heater-common/htr_i2c_map.h and
 * App/services/heater.c. Returns 0 on success, -1 on HAL error/timeout. */
#include "drv_heater_i2c2.h"
#include "htr_i2c_map.h"
#include "main.h"

extern I2C_HandleTypeDef hi2c2;

#define HEATER_I2C_TIMEOUT_MS 10u

static int hc_read(void *ctx, uint8_t reg, uint8_t *buf, uint16_t len)
{
    (void)ctx;
    return (HAL_I2C_Mem_Read(&hi2c2, (uint16_t)(HTR_I2C_ADDR7 << 1), reg,
                             I2C_MEMADD_SIZE_8BIT, buf, len,
                             HEATER_I2C_TIMEOUT_MS) == HAL_OK) ? 0 : -1;
}

static int hc_write(void *ctx, uint8_t reg, const uint8_t *buf, uint16_t len)
{
    (void)ctx;
    return (HAL_I2C_Mem_Write(&hi2c2, (uint16_t)(HTR_I2C_ADDR7 << 1), reg,
                              I2C_MEMADD_SIZE_8BIT, (uint8_t *)buf, len,
                              HEATER_I2C_TIMEOUT_MS) == HAL_OK) ? 0 : -1;
}

static const i2c_backend_t s_backend = { hc_read, hc_write, NULL };

const i2c_backend_t *drv_heater_i2c2_backend(void) { return &s_backend; }
````

```bash
git rm cube/Core/Inc/drv_wk2132_i2c2.h cube/Core/Src/drv_wk2132_i2c2.c
```

- [ ] **Step 2: Rewire `app_main.c`**

Replace the include line

```c
#include "drv_wk2132_i2c2.h" /* Task 12: concrete I2C2 WK2132 backend for the VEVOR heater */
```

with

```c
#include "drv_heater_i2c2.h" /* I2C2 backend for the heater coprocessor (0x64) */
```

Replace

```c
#include "heater.h"          /* portable VEVOR diesel heater service (App/services) */
#include "mbp_heater.h"      /* heater Modbus provider (regs 53-67) */
```

with

```c
#include "heater.h"          /* heater coprocessor client (App/services) */
#include "mbp_heater.h"      /* heater Modbus provider (regs 53-74) */
#include "sensors.h"         /* cabin (enclosure) temp -> heater ambient */
```

Replace the whole block from `/* Task 12 — VEVOR diesel heater over WK2132/I2C2.` through `heater_init(drv_wk2132_i2c2_backend(), HAL_GetTick());` with

```c
    /* Heater coprocessor (EF-L011 / C011) on I2C2 at 0x64. Optional hardware:
     * heater_init only stashes the backend; an absent coprocessor is reported
     * as heater_present=0 and never blocks or faults boot. No nRST GPIO is
     * wired yet (spec open item 6), so no reset hook. */
    heater_init(drv_heater_i2c2_backend(), NULL, HAL_GetTick());
```

Replace

```c
    mbp_heater_register();            /* Task 12: VEVOR heater regs 53-67 (command + telemetry) */
```

with

```c
    mbp_heater_register();            /* heater regs 53-74 (command + telemetry) */
```

Replace the superloop lines

```c
        heater_process(now);     /* Task 12: service the VEVOR heater FSM every pass (far
                                   * faster than its 10 ms RX-poll cadence); kept in the
                                   * superloop, not a sched slot, to stay decoupled from
                                   * control_app.c. */
```

with

```c
        heater_set_ambient_f(sensors_get_encl_temp_f()); /* AUTOTERM room temp (enclosure stand-in) */
        heater_process(now);     /* coprocessor client: self-paced at 250 ms */
```

Check: `grep -n "wk2132\|WK2132\|VEVOR" cube/Core/Src/app_main.c` → no output.

- [ ] **Step 3: Update the CubeIDE project files**

In `cube/.project`, delete the two `<link>` blocks whose `<name>` is `Core/Src/wk2132.c` and `Core/Src/vevor_heater.c` (keep `heater.c` and `mbp_heater.c`).

In `cube/.cproject`, after **each** of the two lines (Debug and Release configurations)

```xml
									<listOptionValue builtIn="false" value="&quot;${workspace_loc:/${ProjName}/App/services}&quot;"/>
```

add

```xml
									<listOptionValue builtIn="false" value="../../heater-common"/>
```

(Include paths in `.cproject` are relative to the build directory `cube/Debug`, so `../../heater-common` is the repo's `heater-common/`.)

Check: `grep -c "heater-common" cube/.cproject` → `2`; `grep -c "wk2132\|vevor_heater" cube/.project` → `0`.

- [ ] **Step 4: Cross-compile syntax check of every changed on-target file**

```bash
TC=/Applications/STM32CubeIDE.app/Contents/Eclipse/plugins/com.st.stm32cube.ide.mcu.externaltools.gnu-tools-for-stm32.13.3.rel1.macos64_1.0.0.202411102158/tools/bin
for f in cube/Core/Src/drv_heater_i2c2.c cube/Core/Src/app_main.c App/services/heater.c App/services/mbp_heater.c; do
  $TC/arm-none-eabi-gcc -mcpu=cortex-m0plus -mthumb -DSTM32G0B1xx -DUSE_HAL_DRIVER \
    -Icube/Core/Inc -Icube/Drivers/STM32G0xx_HAL_Driver/Inc -Icube/Drivers/CMSIS/Device/ST/STM32G0xx/Include \
    -Icube/Drivers/CMSIS/Include -IApp/services -IApp/include -Iheater-common \
    -std=gnu11 -Wall -Wextra -fsyntax-only $f && echo "OK $f"
done
```

Expected: four `OK` lines.

- [ ] **Step 5: Mark the old VEVOR doc superseded**

Insert directly under the title of `docs/vevor-heater.md`:

```markdown
> **Superseded (2026-10-06):** the G0B1 no longer talks to the heater directly. The VEVOR protocol now runs on the heater coprocessor (`heater-common/vevor_proto.c`), reached over I²C at 0x64 — see `docs/heater-coprocessor.md`. The WK2132/DFR0627 bridge sections below are historical. The protocol, safety rules and Modbus regs 53–67 described here still hold.
```

- [ ] **Step 6: User CubeIDE build (cannot be done headless here)**

Ask the user to: in STM32CubeIDE, **File ▸ Close Project → Open Project** on `g0b1-apu` (required after `.project` link changes), then **Project ▸ Clean… → Build**. Expected: 0 errors / 0 warnings. Delete any macOS ` 2.*` duplicate files first (`find cube -name "* 2.*"`).

- [ ] **Step 7: Commit**

```bash
git add -A cube/Core cube/.project cube/.cproject docs/vevor-heater.md
git commit -m "feat(g0b1): wire the heater coprocessor client on I2C2

drv_heater_i2c2 replaces the WK2132 backend; app_main feeds enclosure
temp as heater ambient; project drops wk2132/vevor links and adds the
heater-common include path.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_017uxgfN1Y67gVKWmTWmpxxk"
```
