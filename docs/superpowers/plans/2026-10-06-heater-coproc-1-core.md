# Heater Coprocessor — Plan 1 of 3: Portable Core Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the MCU-independent heart of the heater coprocessor — heater protocols (VEVOR, AUTOTERM), driver registry, interface sequencing, generic state machine with auto-detect, and the I²C register file — fully host-tested.

**Architecture:** Two new top-level dirs in `g0b1-firmware`: `heater-common/` (shared with the G0B1: I²C map, types, UART seam, protocols) and `heater-coproc/core/` (driver adapters, FSM, iface, regs). No MCU headers anywhere in either; hardware is reached only through three seams (`htr_uart_t`, `htr_gpio_t`, `htr_nv_t`) that Plan 2 implements for the L011 and the tests implement as fakes.

**Tech Stack:** C11, Unity + CMake host suite in `Tests/` (ASan/UBSan for all new targets).

**Spec:** `docs/superpowers/specs/2026-10-06-heater-coprocessor-design.md`

**Plan set:** Plan 1 (this) → Plan 2 `2026-10-06-heater-coproc-2-l011-port.md` (L011 firmware image) and Plan 3 `2026-10-06-heater-coproc-3-g0b1-client.md` (G0B1 side). Plans 2 and 3 both depend on Plan 1 and are independent of each other.

## Global Constraints

- Portable code (`heater-common/`, `heater-coproc/core/`) includes **no** MCU/HAL/CMSIS header.
- No blocking anywhere: every module is driven by `process(now_ms)`; all time comparisons are `(uint32_t)(now - t) >= X` (wrap-safe).
- AUTOTERM frame = `AA dir LEN 00 CMD payload CRC_HI CRC_LO`, CRC-16/MODBUS, **high byte first**.
- AUTOTERM sends only the verified request set: wake `1C`, version `06`, status `0F`, settings `02` (read + write), temp `11`, start `01`, stop `03`. Never `04`/`0B`.
- VEVOR protocol bytes/timing are Robb's verified ones — moved, never changed.
- A driver's `probe()` may only send requests that cannot start a heater or change its settings.
- Never stop a heater by removing power: every stop goes through `driver->stop()` + cooldown.
- No autonomous restart after FAULT; comms loss is a flag, not a transition.
- 4-wire push-pull TX path is enabled during auto-detect **only** after the 4-wire line signature.
- I²C slave address `0x64`, `HTR_DEVICE_ID 0xEF`, `HTR_MAP_VERSION 1`; register map exactly per spec §6.
- Host timeout default 10 min, range 0–60 (0 = disabled), persisted with HEATER_TYPE; RUN_REQUEST never persisted.
- New test targets use `add_heater_test()` (ASan+UBSan, `-Werror`).
- Commit messages end with:
  ```
  Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
  Claude-Session: https://claude.ai/code/session_017uxgfN1Y67gVKWmTWmpxxk
  ```

## Review Focus

1. **Tick wrap (~49.7 days of uptime):** a heater cycle that straddles the 32-bit ms wrap must behave exactly like any other → `test_full_cycle_across_tick_wrap` (Task 7).
2. **G0B1 keeps writing RUN_REQUEST=1 while the coprocessor is in FAULT:** must never restart ignition → `test_no_autonomous_restart_after_fault` (Task 7).
3. **A read transaction that spans a state change:** the G0B1 must never see a torn status block (e.g. phase from before detection, type from after) → `test_read_is_one_consistent_snapshot` (Task 8).
4. **Half-written 16-bit register (master writes only the low byte of AMBIENT):** must be rejected, never applied as a half value → `test_half_written_ambient_is_rejected` (Task 8).
5. **Ambiguous/marginal line levels at auto-detect:** must never drive the 4-wire push-pull stage onto a possibly-3-wire bus, and must report UNKNOWN rather than guess → `test_push_pull_4w_never_enabled_without_4w_signature` (Task 7).

## File Structure

| File | Responsibility |
|---|---|
| `heater-common/htr_types.h` | type ids, wiring classes, phases, hints, caps/valid bits, `htr_request_t`, `htr_status_t` |
| `heater-common/htr_uart.h` | byte-stream UART seam |
| `heater-common/htr_i2c_map.h` | frozen I²C register contract (both MCUs include it) |
| `heater-common/crc16_modbus.c/.h` | CRC-16/MODBUS |
| `heater-common/autoterm_proto.c/.h` | AUTOTERM framing, builders, parser, status decode, phase hint |
| `heater-common/vevor_proto.c/.h` | VEVOR protocol over `htr_uart_t` (moved from `App/services/vevor_heater.c`) |
| `heater-coproc/core/htr_driver.h` | driver vtable |
| `heater-coproc/core/drv_vevor.c/.h` | VEVOR driver adapter |
| `heater-coproc/core/drv_autoterm.c/.h` | AUTOTERM wake/baud sequencer + poll scheduler |
| `heater-coproc/core/htr_iface.c/.h` | TX routing/power sequencing via `htr_gpio_t` |
| `heater-coproc/core/htr_fsm.c/.h` | generic state machine + auto-detect |
| `heater-coproc/core/htr_nv.h`, `htr_version.h` | persisted-settings seam, firmware version |
| `heater-coproc/core/htr_regs.c/.h` | I²C register file, snapshot, commands, host timeout |
| `Tests/fakes/fake_htr_uart.c/.h`, `fake_htr_gpio.c/.h`, `fake_htr_driver.c/.h` | test fakes |
| `Tests/test_crc16_modbus.c`, `test_autoterm_proto.c`, `test_vevor_proto.c`, `test_drv_vevor.c`, `test_drv_autoterm.c`, `test_htr_iface.c`, `test_htr_fsm.c`, `test_htr_regs.c` | host tests |

`Tests/fixtures/autoterm_cycle.csv` (712 captured replies) is already committed with the spec.

**How to run tests** (from the repo root, every task):

```bash
cmake -S Tests -B Tests/build >/dev/null && cmake --build Tests/build --target <test> && (cd Tests/build && ctest -R '^<test>$' --output-on-failure)
```

---

### Task 1: heater-common foundation (types, UART seam, CRC, fake UART, CMake block)

**Files:**
- Create: `heater-common/htr_types.h`, `heater-common/htr_uart.h`, `heater-common/crc16_modbus.h`, `heater-common/crc16_modbus.c`
- Create: `Tests/fakes/fake_htr_uart.h`, `Tests/fakes/fake_htr_uart.c`, `Tests/test_crc16_modbus.c`
- Modify: `Tests/CMakeLists.txt` (append at end)

**Interfaces:**
- Produces: `uint16_t crc16_modbus(const uint8_t *data, size_t len)`; `htr_uart_t {write, read, flush_rx, set_baud, ctx}`; all `HTR_TYPE_*`, `HTR_WIRING_*`, `htr_phase_t`, `htr_hint_t`, `htr_probe_t`, `HTR_CAP_*`, `HTR_V_*`, `HTR_AMBIENT_UNKNOWN`, `htr_request_t`, `htr_status_t`; `fake_uart_t` + `fake_uart_init/inject/clear_tx/write_cmd`; CMake function `add_heater_test()` and variables `HC`, `HCORE`.

- [ ] **Step 1: Write the failing test**

`Tests/test_crc16_modbus.c`:

````c
#include "crc16_modbus.h"
#include "unity.h"
#include <stdio.h>
void setUp(void){} void tearDown(void){}

/* Every reference frame from the AUTOTERM bench notes: CRC over all but the
   last two bytes, which hold the CRC high byte first. */
static void check(const uint8_t *f, size_t n){
    uint16_t c = crc16_modbus(f, n - 2u);
    TEST_ASSERT_EQUAL_HEX8(c >> 8, f[n - 2u]);
    TEST_ASSERT_EQUAL_HEX8(c & 0xFF, f[n - 1u]);
}
void test_reference_frames(void){
    const uint8_t a[]={0xAA,0x03,0x00,0x00,0x1C,0x95,0x3D};
    const uint8_t b[]={0xAA,0x03,0x00,0x00,0x06,0x5E,0xBC};
    const uint8_t c[]={0xAA,0x04,0x05,0x00,0x06,0x02,0x01,0x03,0x05,0x03,0x51,0x6F};
    const uint8_t d[]={0xAA,0x03,0x00,0x00,0x0F,0x58,0x7C};
    const uint8_t e[]={0xAA,0x03,0x00,0x00,0x02,0x9D,0xBD};
    const uint8_t f[]={0xAA,0x03,0x01,0x00,0x11,0x17,0xB3,0x11};
    const uint8_t g[]={0xAA,0x04,0x01,0x00,0x11,0x17,0x73,0xA4};
    const uint8_t h[]={0xAA,0x03,0x07,0x00,0x01,0xFF,0xFF,0x02,0x1D,0x01,0xFF,0x11,0x19,0x34};
    const uint8_t i[]={0xAA,0x03,0x00,0x00,0x03,0x5D,0x7C};
    check(a,sizeof a); check(b,sizeof b); check(c,sizeof c); check(d,sizeof d);
    check(e,sizeof e); check(f,sizeof f); check(g,sizeof g); check(h,sizeof h); check(i,sizeof i);
}
void test_modbus_check_value(void){
    const uint8_t s[]="123456789";
    TEST_ASSERT_EQUAL_HEX16(0x4B37, crc16_modbus(s, 9u));   /* catalogue check */
}
int main(void){ UNITY_BEGIN(); RUN_TEST(test_reference_frames); RUN_TEST(test_modbus_check_value); return UNITY_END(); }
````

Append to `Tests/CMakeLists.txt`:

```cmake

# ---- heater coprocessor: portable heater-common + coproc core ----
set(HC ${CMAKE_CURRENT_SOURCE_DIR}/../heater-common)
set(HCORE ${CMAKE_CURRENT_SOURCE_DIR}/../heater-coproc/core)
function(add_heater_test name)
  add_unity_test(${name} ${ARGN})
  target_include_directories(${name} PRIVATE ${HC} ${HCORE})
  target_compile_options(${name} PRIVATE -fsanitize=address,undefined -fno-sanitize-recover=all)
  target_link_options(${name} PRIVATE -fsanitize=address,undefined)
endfunction()
add_heater_test(test_crc16_modbus test_crc16_modbus.c ${HC}/crc16_modbus.c)
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake -S Tests -B Tests/build >/dev/null && cmake --build Tests/build --target test_crc16_modbus`
Expected: FAIL — `crc16_modbus.h: No such file or directory` / missing `heater-common/crc16_modbus.c`.

- [ ] **Step 3: Write the implementation**

`heater-common/htr_types.h`:

````c
#ifndef HTR_TYPES_H
#define HTR_TYPES_H
/* Heater-agnostic types shared by every heater driver, the coprocessor core,
   and the G0B1 client. Portable C: no MCU headers. */
#include <stdbool.h>
#include <stdint.h>

/* Heater type ids (register HEATER_TYPE / ACTIVE_TYPE). */
#define HTR_TYPE_NONE      0x00u
#define HTR_TYPE_VEVOR     0x01u
#define HTR_TYPE_AUTOTERM  0x02u
#define HTR_TYPE_AUTO      0xFFu

/* Wiring class: selects UART + TX routing. */
#define HTR_WIRING_NONE    0u
#define HTR_WIRING_3W      1u
#define HTR_WIRING_4W      2u

/* Generic phase (register PHASE). */
typedef enum {
    HTR_PHASE_OFF = 0,
    HTR_PHASE_DETECTING = 1,
    HTR_PHASE_STARTING = 2,
    HTR_PHASE_RUNNING = 3,
    HTR_PHASE_STOP_REQUESTED = 4,
    HTR_PHASE_COOLDOWN = 5,
    HTR_PHASE_FAULT = 6
} htr_phase_t;

/* What a driver believes the heater itself is doing, from its last frame. */
typedef enum {
    HTR_HINT_UNKNOWN = 0,
    HTR_HINT_OFF,
    HTR_HINT_STARTING,
    HTR_HINT_RUNNING,
    HTR_HINT_COOLDOWN,
    HTR_HINT_FAULT
} htr_hint_t;

typedef enum { HTR_PROBE_BUSY = 0, HTR_PROBE_FOUND, HTR_PROBE_ABSENT } htr_probe_t;

/* Driver capabilities (register CAPS). */
#define HTR_CAP_LEVEL       (1u << 0)
#define HTR_CAP_SETPOINT    (1u << 1)
#define HTR_CAP_AMBIENT_IN  (1u << 2)
#define HTR_CAP_FAN_RPM     (1u << 3)
#define HTR_CAP_PUMP        (1u << 4)
#define HTR_CAP_SUPPLY_V    (1u << 5)
#define HTR_CAP_EXCH_TEMP   (1u << 6)
#define HTR_CAP_FAULT_CODE  (1u << 7)

/* Field-valid bits (register VALID): a clear bit means "not decoded". */
#define HTR_V_FAULT_CODE    (1u << 0)
#define HTR_V_ACTIVE_LEVEL  (1u << 1)
#define HTR_V_SUPPLY_MV     (1u << 2)
#define HTR_V_FAN_RPM       (1u << 3)
#define HTR_V_PUMP_HZ       (1u << 4)
#define HTR_V_EXCH_RAW      (1u << 5)
#define HTR_V_EXCH_C        (1u << 6)
#define HTR_V_STATE_SECONDS (1u << 7)

#define HTR_AMBIENT_UNKNOWN ((int16_t)0x7FFF)

typedef struct {
    uint8_t run;            /* 0/1 */
    uint8_t level;          /* 1..10, CAP_LEVEL drivers */
    int8_t  setpoint_c;     /* CAP_SETPOINT drivers */
    int16_t ambient_c_x10;  /* CAP_AMBIENT_IN drivers; HTR_AMBIENT_UNKNOWN if none */
} htr_request_t;

typedef struct {
    uint8_t  hint;          /* htr_hint_t */
    uint8_t  vendor_state;
    uint8_t  vendor_sub;
    uint8_t  fault_code;
    uint8_t  active_level;
    uint16_t supply_mv;
    uint16_t fan_rpm;
    uint16_t pump_hz_x10;
    uint16_t exch_raw;
    int16_t  exch_c_x10;
    uint16_t state_seconds;
    uint16_t valid;         /* HTR_V_* */
    bool     any_rx;        /* at least one valid frame since init */
    uint32_t last_rx_tick;
    uint32_t valid_frames;
    uint32_t crc_fails;
    uint32_t transport_errs;
} htr_status_t;

#endif /* HTR_TYPES_H */
````

`heater-common/htr_uart.h`:

````c
#ifndef HTR_UART_H
#define HTR_UART_H
/* Byte-stream seam between heater protocol code and a UART.
   The port implements it with IRQ-driven rings; tests use a fake.
   On half-duplex (3-wire) links the port strips the local TX echo, so
   read() only ever returns bytes sent by the heater. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct htr_uart {
    size_t (*write)(void *ctx, const uint8_t *buf, size_t len); /* bytes queued */
    size_t (*read)(void *ctx, uint8_t *buf, size_t cap);        /* bytes drained */
    void   (*flush_rx)(void *ctx);
    bool   (*set_baud)(void *ctx, uint32_t baud);               /* false: TX busy, retry */
    void   *ctx;
} htr_uart_t;

#endif /* HTR_UART_H */
````

`heater-common/crc16_modbus.h`:

````c
#ifndef CRC16_MODBUS_H
#define CRC16_MODBUS_H
#include <stddef.h>
#include <stdint.h>
/* CRC-16/MODBUS: poly 0xA001 (reflected 0x8005), init 0xFFFF, no xorout. */
uint16_t crc16_modbus(const uint8_t *data, size_t len);
#endif
````

`heater-common/crc16_modbus.c`:

````c
#include "crc16_modbus.h"

uint16_t crc16_modbus(const uint8_t *data, size_t len)
{
    uint16_t crc = 0xFFFFu;
    size_t i;
    uint8_t b;
    for (i = 0u; i < len; ++i) {
        crc ^= data[i];
        for (b = 0u; b < 8u; ++b) {
            crc = (crc & 1u) ? (uint16_t)((crc >> 1) ^ 0xA001u) : (uint16_t)(crc >> 1);
        }
    }
    return crc;
}
````

Also create the fake UART used by Tasks 3–8:

`Tests/fakes/fake_htr_uart.h`:

````c
#ifndef FAKE_HTR_UART_H
#define FAKE_HTR_UART_H
/* In-memory htr_uart_t: records everything written (with the baud it was
   written at), serves injected RX bytes, and can refuse writes/baud changes. */
#include "htr_uart.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define FAKE_UART_TX_CAP 4096u
#define FAKE_UART_RX_CAP 4096u
#define FAKE_UART_MAX_WRITES 512u

typedef struct {
    htr_uart_t api;
    uint8_t  tx[FAKE_UART_TX_CAP];
    size_t   tx_len;
    size_t   write_off[FAKE_UART_MAX_WRITES];  /* start offset of each write() */
    size_t   write_len[FAKE_UART_MAX_WRITES];
    uint32_t write_baud[FAKE_UART_MAX_WRITES];
    size_t   writes;
    uint8_t  rx[FAKE_UART_RX_CAP];
    size_t   rx_head, rx_tail;
    uint32_t baud;
    bool     refuse_write;
    bool     refuse_baud;
    unsigned flushes;
} fake_uart_t;

void fake_uart_init(fake_uart_t *f);
void fake_uart_inject(fake_uart_t *f, const uint8_t *buf, size_t len);
void fake_uart_clear_tx(fake_uart_t *f);
/* Command byte (offset 4) of write #i, for AUTOTERM frames. */
uint8_t fake_uart_write_cmd(const fake_uart_t *f, size_t i);
#endif
````

`Tests/fakes/fake_htr_uart.c`:

````c
#include "fake_htr_uart.h"
#include <string.h>

static size_t fu_write(void *ctx, const uint8_t *buf, size_t len)
{
    fake_uart_t *f = (fake_uart_t *)ctx;
    if (f->refuse_write || (f->tx_len + len > FAKE_UART_TX_CAP) ||
        (f->writes >= FAKE_UART_MAX_WRITES)) {
        return 0u;
    }
    f->write_off[f->writes] = f->tx_len;
    f->write_len[f->writes] = len;
    f->write_baud[f->writes] = f->baud;
    f->writes++;
    memcpy(&f->tx[f->tx_len], buf, len);
    f->tx_len += len;
    return len;
}

static size_t fu_read(void *ctx, uint8_t *buf, size_t cap)
{
    fake_uart_t *f = (fake_uart_t *)ctx;
    size_t n = 0u;
    while ((n < cap) && (f->rx_tail != f->rx_head)) {
        buf[n++] = f->rx[f->rx_tail];
        f->rx_tail = (f->rx_tail + 1u) % FAKE_UART_RX_CAP;
    }
    return n;
}

static void fu_flush(void *ctx)
{
    fake_uart_t *f = (fake_uart_t *)ctx;
    f->rx_tail = f->rx_head;
    f->flushes++;
}

static bool fu_set_baud(void *ctx, uint32_t baud)
{
    fake_uart_t *f = (fake_uart_t *)ctx;
    if (f->refuse_baud) return false;
    f->baud = baud;
    return true;
}

void fake_uart_init(fake_uart_t *f)
{
    memset(f, 0, sizeof(*f));
    f->api.write = fu_write;
    f->api.read = fu_read;
    f->api.flush_rx = fu_flush;
    f->api.set_baud = fu_set_baud;
    f->api.ctx = f;
}

void fake_uart_inject(fake_uart_t *f, const uint8_t *buf, size_t len)
{
    size_t i;
    for (i = 0u; i < len; ++i) {
        f->rx[f->rx_head] = buf[i];
        f->rx_head = (f->rx_head + 1u) % FAKE_UART_RX_CAP;
    }
}

void fake_uart_clear_tx(fake_uart_t *f)
{
    f->tx_len = 0u;
    f->writes = 0u;
}

uint8_t fake_uart_write_cmd(const fake_uart_t *f, size_t i)
{
    return (i < f->writes && f->write_len[i] > 4u) ? f->tx[f->write_off[i] + 4u] : 0xFFu;
}
````

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build Tests/build --target test_crc16_modbus && (cd Tests/build && ctest -R '^test_crc16_modbus$' --output-on-failure)`
Expected: PASS (2 tests). Then the whole suite: `(cd Tests/build && cmake --build . && ctest)` — all pre-existing tests still pass.

- [ ] **Step 5: Commit**

```bash
git add heater-common/htr_types.h heater-common/htr_uart.h heater-common/crc16_modbus.[ch] Tests/fakes/fake_htr_uart.[ch] Tests/test_crc16_modbus.c Tests/CMakeLists.txt
git commit -m "feat(heater-common): types, UART seam, CRC-16/MODBUS

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_017uxgfN1Y67gVKWmTWmpxxk"
```

---

### Task 2: AUTOTERM protocol (framing, builders, parser, status decode)

**Files:**
- Create: `heater-common/autoterm_proto.h`, `heater-common/autoterm_proto.c`, `Tests/test_autoterm_proto.c`
- Modify: `Tests/CMakeLists.txt` (append)

**Interfaces:**
- Consumes: `crc16_modbus()`, `HTR_HINT_*` (Task 1).
- Produces: `AT_CMD_*`, `AT_DIR_*`, `AT_MAX_PAYLOAD`, `AT_OVERHEAD`, `AT_MAX_FRAME`, `AT_STATUS_LEN`; `size_t AT_BuildFrame(uint8_t dir, uint8_t cmd, const uint8_t *payload, uint8_t len, uint8_t *out)`, `AT_BuildSimple(cmd,out)`, `AT_BuildTemp(int8_t,out)`, `AT_BuildStart(int8_t,out)`, `AT_BuildSettingsWrite(int8_t,out)`; `at_frame_t`, `at_parser_t`, `void AT_ParserReset(at_parser_t*)`, `bool AT_ParseByte(at_parser_t*, uint8_t, at_frame_t*)`; `at_status_t`, `bool AT_DecodeStatus(const at_frame_t*, at_status_t*)`; `uint8_t AT_PhaseHint(uint8_t major, uint8_t minor)`.

- [ ] **Step 1: Write the failing test**

`Tests/test_autoterm_proto.c`:

````c
#include "autoterm_proto.h"
#include "htr_types.h"
#include "unity.h"
#include <stdio.h>
#include <string.h>
void setUp(void){} void tearDown(void){}

void test_builders_byte_exact_vs_captures(void){
    uint8_t f[AT_MAX_FRAME];
    const uint8_t wake[]={0xAA,0x03,0x00,0x00,0x1C,0x95,0x3D};
    const uint8_t ver[] ={0xAA,0x03,0x00,0x00,0x06,0x5E,0xBC};
    const uint8_t stat[]={0xAA,0x03,0x00,0x00,0x0F,0x58,0x7C};
    const uint8_t sets[]={0xAA,0x03,0x00,0x00,0x02,0x9D,0xBD};
    const uint8_t stop[]={0xAA,0x03,0x00,0x00,0x03,0x5D,0x7C};
    const uint8_t t23[] ={0xAA,0x03,0x01,0x00,0x11,0x17,0xB3,0x11};
    const uint8_t st29[]={0xAA,0x03,0x07,0x00,0x01,0xFF,0xFF,0x02,0x1D,0x01,0xFF,0x11,0x19,0x34};
    TEST_ASSERT_EQUAL_size_t(7, AT_BuildSimple(AT_CMD_WAKE,f));     TEST_ASSERT_EQUAL_HEX8_ARRAY(wake,f,7);
    TEST_ASSERT_EQUAL_size_t(7, AT_BuildSimple(AT_CMD_VERSION,f));  TEST_ASSERT_EQUAL_HEX8_ARRAY(ver,f,7);
    TEST_ASSERT_EQUAL_size_t(7, AT_BuildSimple(AT_CMD_STATUS,f));   TEST_ASSERT_EQUAL_HEX8_ARRAY(stat,f,7);
    TEST_ASSERT_EQUAL_size_t(7, AT_BuildSimple(AT_CMD_SETTINGS,f)); TEST_ASSERT_EQUAL_HEX8_ARRAY(sets,f,7);
    TEST_ASSERT_EQUAL_size_t(7, AT_BuildSimple(AT_CMD_STOP,f));     TEST_ASSERT_EQUAL_HEX8_ARRAY(stop,f,7);
    TEST_ASSERT_EQUAL_size_t(8, AT_BuildTemp(23,f));                TEST_ASSERT_EQUAL_HEX8_ARRAY(t23,f,8);
    TEST_ASSERT_EQUAL_size_t(14, AT_BuildStart(29,f));              TEST_ASSERT_EQUAL_HEX8_ARRAY(st29,f,14);
}
void test_settings_write_shape(void){
    uint8_t f[AT_MAX_FRAME];
    TEST_ASSERT_EQUAL_size_t(13, AT_BuildSettingsWrite(23,f));
    const uint8_t head[]={0xAA,0x03,0x06,0x00,0x02,0xFF,0xFF,0x02,0x17,0x01,0xFF};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(head,f,11);
}

static int feed(at_parser_t *p, const uint8_t *b, size_t n, at_frame_t *last){
    int got=0; for(size_t i=0;i<n;i++) if(AT_ParseByte(p,b[i],last)) got++; return got;
}
void test_parser_version_reply_with_garbage_and_resync(void){
    at_parser_t p; memset(&p,0,sizeof p); at_frame_t fr;
    const uint8_t s[]={0x00,0x55,0xAA,0xAA,0x04,0x05,0x00,0x06,0x02,0x01,0x03,0x05,0x03,0x51,0x6F};
    TEST_ASSERT_EQUAL_INT(1, feed(&p,s,sizeof s,&fr));
    TEST_ASSERT_EQUAL_HEX8(AT_CMD_VERSION, fr.cmd);
    TEST_ASSERT_EQUAL_UINT8(5, fr.len);
    TEST_ASSERT_EQUAL_HEX8(0x02, fr.payload[0]);
}
void test_parser_rejects_bad_crc_then_recovers(void){
    at_parser_t p; memset(&p,0,sizeof p); at_frame_t fr;
    const uint8_t bad[]={0xAA,0x04,0x01,0x00,0x11,0x17,0x73,0xA5};
    const uint8_t good[]={0xAA,0x04,0x01,0x00,0x11,0x17,0x73,0xA4};
    TEST_ASSERT_EQUAL_INT(0, feed(&p,bad,sizeof bad,&fr));
    TEST_ASSERT_EQUAL_UINT32(1, p.crc_fails);
    TEST_ASSERT_EQUAL_INT(1, feed(&p,good,sizeof good,&fr));
    TEST_ASSERT_EQUAL_HEX8(0x17, fr.payload[0]);
}
void test_parser_ignores_requests_and_malformed_transition(void){
    at_parser_t p; memset(&p,0,sizeof p); at_frame_t fr;
    const uint8_t s[]={0x03,0xAA,0x00,0x00,0x06,0x5E,0xBC,          /* captured transition */
                       0xAA,0x03,0x00,0x00,0x06,0x5E,0xBC};         /* a request (dir 03) */
    TEST_ASSERT_EQUAL_INT(0, feed(&p,s,sizeof s,&fr));
    TEST_ASSERT_EQUAL_UINT32(0, p.crc_fails);
}
void test_parser_oversize_length_resyncs(void){
    at_parser_t p; memset(&p,0,sizeof p); at_frame_t fr;
    const uint8_t s[]={0xAA,0x04,0x40,0xAA,0x04,0x01,0x00,0x11,0x17,0x73,0xA4};
    TEST_ASSERT_EQUAL_INT(1, feed(&p,s,sizeof s,&fr));
}
void test_phase_hint_table(void){
    TEST_ASSERT_EQUAL_UINT8(HTR_HINT_OFF,      AT_PhaseHint(0,1));
    TEST_ASSERT_EQUAL_UINT8(HTR_HINT_STARTING, AT_PhaseHint(1,0));
    TEST_ASSERT_EQUAL_UINT8(HTR_HINT_STARTING, AT_PhaseHint(2,2));
    TEST_ASSERT_EQUAL_UINT8(HTR_HINT_RUNNING,  AT_PhaseHint(2,4));
    TEST_ASSERT_EQUAL_UINT8(HTR_HINT_RUNNING,  AT_PhaseHint(3,0));
    TEST_ASSERT_EQUAL_UINT8(HTR_HINT_COOLDOWN, AT_PhaseHint(4,0));
    TEST_ASSERT_EQUAL_UINT8(HTR_HINT_FAULT,    AT_PhaseHint(9,0));
}

/* Replay the 712-reply bench capture: every frame parses, decodes, and the
   phase-hint timeline is exactly OFF -> STARTING -> RUNNING -> COOLDOWN -> OFF. */
static int hexval(int c){ if(c>='0'&&c<='9')return c-'0'; if(c>='A'&&c<='F')return c-'A'+10; if(c>='a'&&c<='f')return c-'a'+10; return -1; }
void test_capture_replay_timeline(void){
    FILE *fp=fopen("fixtures/autoterm_cycle.csv","r"); TEST_ASSERT_NOT_NULL(fp);
    char line[512]; at_parser_t p; memset(&p,0,sizeof p); at_frame_t fr; at_status_t st;
    uint8_t timeline[16]; int tl=0, frames=0; uint16_t first_mv=0;
    TEST_ASSERT_NOT_NULL(fgets(line,sizeof line,fp));            /* header */
    while(fgets(line,sizeof line,fp)){
        char *hex=strrchr(line,','); TEST_ASSERT_NOT_NULL(hex);
        int hi=-1;
        for(char *c=hex+1; *c; c++){
            int v=hexval(*c); if(v<0) continue;
            if(hi<0){ hi=v; continue; }
            if(AT_ParseByte(&p,(uint8_t)((hi<<4)|v),&fr)){
                TEST_ASSERT_TRUE(AT_DecodeStatus(&fr,&st));
                if(frames==0) first_mv=st.supply_mv;
                frames++;
                uint8_t h=AT_PhaseHint(st.major,st.minor);
                if(tl==0 || timeline[tl-1]!=h){ TEST_ASSERT_LESS_THAN(16,tl); timeline[tl++]=h; }
            }
            hi=-1;
        }
    }
    fclose(fp);
    TEST_ASSERT_EQUAL_INT(712, frames);
    TEST_ASSERT_EQUAL_UINT32(0, p.crc_fails);
    TEST_ASSERT_EQUAL_UINT16(12600, first_mv);
    const uint8_t want[]={HTR_HINT_OFF,HTR_HINT_STARTING,HTR_HINT_RUNNING,HTR_HINT_COOLDOWN,HTR_HINT_OFF};
    TEST_ASSERT_EQUAL_INT(5, tl);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(want, timeline, 5);
}
int main(void){ UNITY_BEGIN();
    RUN_TEST(test_builders_byte_exact_vs_captures);
    RUN_TEST(test_settings_write_shape);
    RUN_TEST(test_parser_version_reply_with_garbage_and_resync);
    RUN_TEST(test_parser_rejects_bad_crc_then_recovers);
    RUN_TEST(test_parser_ignores_requests_and_malformed_transition);
    RUN_TEST(test_parser_oversize_length_resyncs);
    RUN_TEST(test_phase_hint_table);
    RUN_TEST(test_capture_replay_timeline);
    return UNITY_END(); }
````

Append to `Tests/CMakeLists.txt`:

```cmake
add_heater_test(test_autoterm_proto test_autoterm_proto.c ${HC}/autoterm_proto.c ${HC}/crc16_modbus.c)
set_tests_properties(test_autoterm_proto PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
```

(The working directory pin is needed because the test opens `fixtures/autoterm_cycle.csv` relative to `Tests/`.)

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake -S Tests -B Tests/build >/dev/null && cmake --build Tests/build --target test_autoterm_proto`
Expected: FAIL — `autoterm_proto.h: No such file or directory`.

- [ ] **Step 3: Write the implementation**

`heater-common/autoterm_proto.h`:

````c
#ifndef AUTOTERM_PROTO_H
#define AUTOTERM_PROTO_H
/* AUTOTERM 4-wire serial protocol: framing, CRC, request builders, reply
   parser and status decode. Pure functions, no timing (see drv_autoterm).

   Frame: AA dir LEN 00 CMD payload[LEN] CRC_HI CRC_LO
   dir 0x03 = controller->heater, 0x04 = heater->controller.
   CRC-16/MODBUS over AA..payload, transmitted HIGH byte first. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define AT_PREAMBLE      0xAAu
#define AT_DIR_REQ       0x03u
#define AT_DIR_RESP      0x04u

#define AT_CMD_START     0x01u
#define AT_CMD_SETTINGS  0x02u
#define AT_CMD_STOP      0x03u
#define AT_CMD_VERSION   0x06u
#define AT_CMD_STATUS    0x0Fu
#define AT_CMD_TEMP      0x11u
#define AT_CMD_WAKE      0x1Cu

#define AT_MAX_PAYLOAD   32u
#define AT_OVERHEAD      7u      /* AA dir len 00 cmd crc_hi crc_lo */
#define AT_MAX_FRAME     (AT_OVERHEAD + AT_MAX_PAYLOAD)

#define AT_STATUS_LEN    19u

/* Builders return the frame length written to out (out must hold it). */
size_t AT_BuildFrame(uint8_t dir, uint8_t cmd, const uint8_t *payload,
                     uint8_t len, uint8_t *out);
size_t AT_BuildSimple(uint8_t cmd, uint8_t out[AT_OVERHEAD]);   /* empty payload */
size_t AT_BuildTemp(int8_t temp_c, uint8_t out[8]);
size_t AT_BuildStart(int8_t setpoint_c, uint8_t out[14]);
size_t AT_BuildSettingsWrite(int8_t setpoint_c, uint8_t out[13]);

typedef struct {
    uint8_t cmd;
    uint8_t len;
    uint8_t payload[AT_MAX_PAYLOAD];
} at_frame_t;

typedef struct {
    uint8_t  buf[AT_MAX_FRAME];
    uint8_t  idx;
    uint8_t  total;
    uint32_t crc_fails;
} at_parser_t;

void AT_ParserReset(at_parser_t *p);
/* Feed one byte. Returns true when a CRC-valid heater reply (dir 0x04)
   completes; it is copied to *out. Resyncs on AA after any error. */
bool AT_ParseByte(at_parser_t *p, uint8_t byte, at_frame_t *out);

typedef struct {
    uint8_t  major;
    uint8_t  minor;
    int8_t   internal_c;        /* provisional */
    uint8_t  external_c;        /* 0x7F = no sensor */
    uint16_t supply_mv;         /* verified: byte6 x 100 mV */
    uint16_t exch_raw;          /* raw u16, big-endian bytes 7-8 */
    uint8_t  fan_rps_req;       /* provisional */
    uint8_t  fan_rps;           /* provisional */
    uint8_t  pump_hz_x100_req;  /* provisional */
    uint8_t  pump_hz_x100;      /* provisional */
} at_status_t;

/* Decode a 0x0F status reply. False if cmd/len don't match. */
bool AT_DecodeStatus(const at_frame_t *f, at_status_t *out);

/* htr_hint_t for a reported state (evidence: autoterm_cycle.csv).
   0.x off; 1.x and 2.0-2.3 starting; 2.4+ and 3.x running; 4.x cooldown. */
uint8_t AT_PhaseHint(uint8_t major, uint8_t minor);

#endif /* AUTOTERM_PROTO_H */
````

`heater-common/autoterm_proto.c`:

````c
#include "autoterm_proto.h"
#include "crc16_modbus.h"
#include "htr_types.h"
#include <string.h>

size_t AT_BuildFrame(uint8_t dir, uint8_t cmd, const uint8_t *payload,
                     uint8_t len, uint8_t *out)
{
    uint16_t crc;
    size_t n;
    if (len > AT_MAX_PAYLOAD) return 0u;
    out[0] = AT_PREAMBLE;
    out[1] = dir;
    out[2] = len;
    out[3] = 0x00u;
    out[4] = cmd;
    if (len > 0u) memcpy(&out[5], payload, len);
    n = 5u + len;
    crc = crc16_modbus(out, n);
    out[n] = (uint8_t)(crc >> 8);
    out[n + 1u] = (uint8_t)(crc & 0xFFu);
    return n + 2u;
}

size_t AT_BuildSimple(uint8_t cmd, uint8_t out[AT_OVERHEAD])
{
    return AT_BuildFrame(AT_DIR_REQ, cmd, NULL, 0u, out);
}

size_t AT_BuildTemp(int8_t temp_c, uint8_t out[8])
{
    uint8_t p[1];
    p[0] = (uint8_t)temp_c;
    return AT_BuildFrame(AT_DIR_REQ, AT_CMD_TEMP, p, 1u, out);
}

/* Captured start: payload FF FF 02 1D 01 FF 11 (29 C). Only byte 3 (setpoint)
   is parameterised; the rest is copied verbatim, including the trailing 0x11
   whose meaning is not yet known. */
size_t AT_BuildStart(int8_t setpoint_c, uint8_t out[14])
{
    uint8_t p[7] = { 0xFFu, 0xFFu, 0x02u, 0x00u, 0x01u, 0xFFu, 0x11u };
    p[3] = (uint8_t)setpoint_c;
    return AT_BuildFrame(AT_DIR_REQ, AT_CMD_START, p, 7u, out);
}

size_t AT_BuildSettingsWrite(int8_t setpoint_c, uint8_t out[13])
{
    uint8_t p[6] = { 0xFFu, 0xFFu, 0x02u, 0x00u, 0x01u, 0xFFu };
    p[3] = (uint8_t)setpoint_c;
    return AT_BuildFrame(AT_DIR_REQ, AT_CMD_SETTINGS, p, 6u, out);
}

void AT_ParserReset(at_parser_t *p)
{
    p->idx = 0u;
    p->total = 0u;
}

bool AT_ParseByte(at_parser_t *p, uint8_t byte, at_frame_t *out)
{
    uint16_t crc;
    uint8_t n;
    switch (p->idx) {
    case 0u:
        if (byte == AT_PREAMBLE) p->buf[p->idx++] = byte;
        return false;
    case 1u:
        if (byte == AT_DIR_RESP) {
            p->buf[p->idx++] = byte;
        } else if (byte != AT_PREAMBLE) {
            p->idx = 0u;               /* AA AA: stay at idx 1 */
        }
        return false;
    case 2u:
        if (byte > AT_MAX_PAYLOAD) {
            p->idx = (byte == AT_PREAMBLE) ? 1u : 0u;
            return false;
        }
        p->buf[p->idx++] = byte;
        p->total = (uint8_t)(AT_OVERHEAD + byte);
        return false;
    case 3u:
        if (byte != 0x00u) {
            p->idx = (byte == AT_PREAMBLE) ? 1u : 0u;
            return false;
        }
        p->buf[p->idx++] = byte;
        return false;
    default:
        p->buf[p->idx++] = byte;
        if (p->idx < p->total) return false;
        n = (uint8_t)(p->total - 2u);
        crc = crc16_modbus(p->buf, n);
        p->idx = 0u;
        if ((p->buf[n] != (uint8_t)(crc >> 8)) ||
            (p->buf[n + 1u] != (uint8_t)(crc & 0xFFu))) {
            p->crc_fails++;
            return false;
        }
        out->cmd = p->buf[4];
        out->len = p->buf[2];
        memcpy(out->payload, &p->buf[5], out->len);
        return true;
    }
}

bool AT_DecodeStatus(const at_frame_t *f, at_status_t *out)
{
    const uint8_t *p = f->payload;
    if ((f->cmd != AT_CMD_STATUS) || (f->len < AT_STATUS_LEN)) return false;
    out->major = p[0];
    out->minor = p[1];
    out->internal_c = (int8_t)p[3];
    out->external_c = p[4];
    out->supply_mv = (uint16_t)((uint16_t)p[6] * 100u);
    out->exch_raw = (uint16_t)(((uint16_t)p[7] << 8) | p[8]);
    out->fan_rps_req = p[11];
    out->fan_rps = p[12];
    out->pump_hz_x100_req = p[14];
    out->pump_hz_x100 = p[16];
    return true;
}

uint8_t AT_PhaseHint(uint8_t major, uint8_t minor)
{
    switch (major) {
    case 0u: return HTR_HINT_OFF;
    case 1u: return HTR_HINT_STARTING;
    case 2u: return (minor < 4u) ? HTR_HINT_STARTING : HTR_HINT_RUNNING;
    case 3u: return HTR_HINT_RUNNING;
    case 4u: return HTR_HINT_COOLDOWN;
    default: return HTR_HINT_FAULT;
    }
}
````

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build Tests/build --target test_autoterm_proto && (cd Tests/build && ctest -R '^test_autoterm_proto$' --output-on-failure)`
Expected: PASS (8 tests) — including `test_capture_replay_timeline` reporting 712 frames, 0 CRC failures, timeline OFF→STARTING→RUNNING→COOLDOWN→OFF.

- [ ] **Step 5: Commit**

```bash
git add heater-common/autoterm_proto.[ch] Tests/test_autoterm_proto.c Tests/CMakeLists.txt
git commit -m "feat(heater-common): AUTOTERM framing, parser, status decode

Byte-exact against every captured request; replays the 712-reply bench
capture to the expected phase timeline.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_017uxgfN1Y67gVKWmTWmpxxk"
```

---

### Task 3: VEVOR protocol on the UART seam

The verified VEVOR code moves from `App/services/vevor_heater.c` (WK2132 transport) to `heater-common/vevor_proto.c` (`htr_uart_t` transport). **Only the transport calls change** — frame builders, checksum, parser and timing are byte-identical. The G0B1 copy stays in place until Plan 3 removes it.

**Files:**
- Create: `heater-common/vevor_proto.h`, `heater-common/vevor_proto.c`, `Tests/test_vevor_proto.c`
- Modify: `Tests/CMakeLists.txt` (append)

**Interfaces:**
- Consumes: `htr_uart_t`, `fake_uart_t` (Task 1).
- Produces: same API as `vevor_heater.h` except `bool VEVOR_Init(VEVOR_t *heater, const htr_uart_t *uart, uint32_t now)` and `VEVOR_t.uart` is `const htr_uart_t *`. `VEVOR_Process`, `VEVOR_Start(VEVOR_t*, uint8_t level)`, `VEVOR_SetLevel`, `VEVOR_Stop`, `VEVOR_IsResponseFresh`, `VEVOR_Checksum`, `VEVOR_Build{Start,Run,Stop}Frame`, `VEVOR_REPORTED_*` unchanged.

- [ ] **Step 1: Write the failing test**

This is the existing `Tests/test_vevor.c` (byte-exact frames + 1221-frame capture checksum regression) retargeted to `vevor_proto.h`, plus one new seam test:

`Tests/test_vevor_proto.c`:

````c
#include "vevor_proto.h"
#include "fake_htr_uart.h"
#include "unity.h"
#include <stdio.h>
#include <string.h>
void setUp(void){} void tearDown(void){}

void test_build_frames_byte_exact(void){
    uint8_t f[16];
    const uint8_t start1[16]={0xAA,0x66,0x06,0x0B,0,0,0,0,1,0x06,0,0,0,0,0,0x18};
    const uint8_t run10[16] ={0xAA,0x66,0x02,0x0B,0,0,0,0,10,0x06,0x02,0,0,0,0,0x1F};
    const uint8_t stop1[16] ={0xAA,0x66,0x06,0x0B,0,0,0,0,1,0x05,0,0,0,0,0,0x17};
    VEVOR_BuildStartFrame(1U,f);  TEST_ASSERT_EQUAL_HEX8_ARRAY(start1,f,16);
    VEVOR_BuildRunFrame(10U,f);   TEST_ASSERT_EQUAL_HEX8_ARRAY(run10,f,16);
    VEVOR_BuildStopFrame(1U,f);   TEST_ASSERT_EQUAL_HEX8_ARRAY(stop1,f,16);
}

/* Read a .hex fixture, strip non-hex, decode to bytes. */
static size_t load_hex(const char *path, uint8_t *out, size_t cap){
    FILE *fp=fopen(path,"rb"); TEST_ASSERT_NOT_NULL(fp);
    size_t n=0; int hi=-1, c;
    while((c=fgetc(fp))!=EOF){
        int v; if(c>='0'&&c<='9')v=c-'0'; else if(c>='A'&&c<='F')v=c-'A'+10;
        else if(c>='a'&&c<='f')v=c-'a'+10; else continue;
        if(hi<0)hi=v; else { TEST_ASSERT_LESS_THAN(cap,n); out[n++]=(uint8_t)((hi<<4)|v); hi=-1; }
    }
    fclose(fp); return n;
}
/* Resync on AA 77, verify every 56-byte heater frame's checksum. */
static void check_capture(const char *path, int *frames, int *bad){
    static uint8_t buf[200000]; size_t n=load_hex(path,buf,sizeof buf);
    *frames=0; *bad=0;
    for(size_t i=0;i+56<=n;){
        if(buf[i]==0xAA && buf[i+1]==0x77){
            if(VEVOR_Checksum(&buf[i],55U)!=buf[i+55]) (*bad)++;
            (*frames)++; i+=56;
        } else i++;
    }
}
void test_capture_checksums_all_pass(void){
    int f1,b1,f2,b2;
    check_capture("fixtures/capture1.hex",&f1,&b1);
    check_capture("fixtures/capture2.hex",&f2,&b2);
    TEST_ASSERT_GREATER_THAN(0,f1+f2);
    TEST_ASSERT_EQUAL_INT(0,b1+b2);      /* handoff: all ~1752 responses valid */
}

/* Through the htr_uart seam: first process() sends a STOP poll; a valid
   56-byte reply decodes; a refused write counts a transport error. */
void test_process_over_uart_seam(void){
    fake_uart_t u; fake_uart_init(&u); VEVOR_t v;
    TEST_ASSERT_TRUE(VEVOR_Init(&v,&u.api,0U));
    VEVOR_Process(&v,0U);
    TEST_ASSERT_EQUAL_size_t(1,u.writes); TEST_ASSERT_EQUAL_size_t(16,u.write_len[0]);
    TEST_ASSERT_EQUAL_HEX8(0x05,u.tx[9]);                     /* STOP state byte */
    uint8_t r[56]={0}; r[0]=0xAA; r[1]=0x77; r[5]=3; r[6]=4; r[11]=126; r[55]=VEVOR_Checksum(r,55U);
    fake_uart_inject(&u,r,56); VEVOR_Process(&v,20U);
    TEST_ASSERT_EQUAL_UINT32(1,v.valid_frames);
    TEST_ASSERT_EQUAL_UINT8(3,v.status.state); TEST_ASSERT_EQUAL_UINT16(12600,v.status.supply_mv);
    u.refuse_write=true; VEVOR_Process(&v,1000U);
    TEST_ASSERT_EQUAL_UINT32(1,v.transport_errors);
}
int main(void){ UNITY_BEGIN();
    RUN_TEST(test_build_frames_byte_exact);
    RUN_TEST(test_capture_checksums_all_pass);
    RUN_TEST(test_process_over_uart_seam); return UNITY_END(); }
````

Append to `Tests/CMakeLists.txt`:

```cmake
add_heater_test(test_vevor_proto test_vevor_proto.c ${HC}/vevor_proto.c fakes/fake_htr_uart.c)
set_tests_properties(test_vevor_proto PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake -S Tests -B Tests/build >/dev/null && cmake --build Tests/build --target test_vevor_proto`
Expected: FAIL — `vevor_proto.h: No such file or directory`.

- [ ] **Step 3: Write the implementation**

`heater-common/vevor_proto.h`:

````c
#ifndef VEVOR_PROTO_H
#define VEVOR_PROTO_H

#include "htr_uart.h"
#include <stdbool.h>
#include <stdint.h>

#define VEVOR_CONTROLLER_FRAME_SIZE 16U
#define VEVOR_HEATER_FRAME_SIZE     56U

typedef enum {
    VEVOR_REPORTED_OFF = 0,
    VEVOR_REPORTED_PREHEAT = 1,
    VEVOR_REPORTED_IGNITION = 2,
    VEVOR_REPORTED_RUNNING = 3,
    VEVOR_REPORTED_COOLDOWN = 4
} VEVOR_ReportedState_t;

typedef enum {
    VEVOR_REQUEST_STOP = 0,
    VEVOR_REQUEST_START,
    VEVOR_REQUEST_RUN
} VEVOR_RequestMode_t;

typedef struct {
    bool valid;
    uint8_t state;
    uint8_t level;
    uint8_t error;
    uint16_t supply_mv;
    bool cooldown_flag;
    uint16_t exchanger_raw;
    uint16_t state_seconds;
    uint16_t pump_hz_x10;
    uint16_t fan_rpm;
    uint32_t received_tick;
} VEVOR_Status_t;

typedef struct {
    const htr_uart_t *uart;
    VEVOR_RequestMode_t request_mode;
    uint8_t requested_level;
    uint8_t start_frames_remaining;
    uint8_t rx_frame[VEVOR_HEATER_FRAME_SIZE];
    uint8_t rx_index;
    uint32_t last_tx_tick;
    uint32_t last_rx_poll_tick;
    uint32_t valid_frames;
    uint32_t checksum_failures;
    uint32_t transport_errors;
    VEVOR_Status_t status;
} VEVOR_t;

bool VEVOR_Init(VEVOR_t *heater, const htr_uart_t *uart, uint32_t now);
void VEVOR_Process(VEVOR_t *heater, uint32_t now);
bool VEVOR_Start(VEVOR_t *heater, uint8_t level);
bool VEVOR_SetLevel(VEVOR_t *heater, uint8_t level);
void VEVOR_Stop(VEVOR_t *heater);
bool VEVOR_IsResponseFresh(const VEVOR_t *heater, uint32_t now);
bool VEVOR_IsSafeToPowerDown(const VEVOR_t *heater, uint32_t now);

/* Exposed for host tests and comparison with captured frames. */
void VEVOR_BuildStartFrame(uint8_t level, uint8_t out[16]);
void VEVOR_BuildRunFrame(uint8_t level, uint8_t out[16]);
void VEVOR_BuildStopFrame(uint8_t level, uint8_t out[16]);
uint8_t VEVOR_Checksum(const uint8_t *frame, uint8_t checksum_index);

#endif
````

`heater-common/vevor_proto.c`:

````c
#include "vevor_proto.h"
#include <string.h>

#define VEVOR_TX_INTERVAL_MS       1000UL
#define VEVOR_RX_POLL_INTERVAL_MS    10UL
#define VEVOR_RESPONSE_FRESH_MS    3000UL
#define VEVOR_START_FRAME_COUNT       4U

uint8_t VEVOR_Checksum(const uint8_t *frame, uint8_t checksum_index)
{
    uint16_t sum = 0U;
    uint8_t i;
    for (i = 2U; i < checksum_index; ++i) sum += frame[i];
    return (uint8_t)sum;
}

static void build_frame(uint8_t command, uint8_t level, uint8_t state,
                        uint8_t qualifier, uint8_t out[16])
{
    static const uint8_t blank[16] = {
        0xAA, 0x66, 0, 0x0B, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
    };
    memcpy(out, blank, sizeof(blank));
    out[2] = command;
    out[8] = level;
    out[9] = state;
    out[10] = qualifier;
    out[15] = VEVOR_Checksum(out, 15U);
}

void VEVOR_BuildStartFrame(uint8_t level, uint8_t out[16])
{
    build_frame(0x06U, level, 0x06U, 0x00U, out);
}

void VEVOR_BuildRunFrame(uint8_t level, uint8_t out[16])
{
    build_frame(0x02U, level, 0x06U, 0x02U, out);
}

void VEVOR_BuildStopFrame(uint8_t level, uint8_t out[16])
{
    build_frame(0x06U, level, 0x05U, 0x00U, out);
}

static bool decode_response(VEVOR_t *heater, const uint8_t *frame,
                            uint32_t now)
{
    if ((frame[0] != 0xAAU) || (frame[1] != 0x77U) ||
        (VEVOR_Checksum(frame, 55U) != frame[55])) {
        heater->checksum_failures++;
        return false;
    }
    heater->status.state = frame[5];
    heater->status.level = frame[6];
    heater->status.error = frame[7];
    heater->status.supply_mv = (uint16_t)frame[11] * 100U;
    heater->status.cooldown_flag = (frame[14] != 0U);
    heater->status.exchanger_raw = (uint16_t)(((uint16_t)frame[16] << 8) |
                                              frame[17]);
    heater->status.state_seconds = (uint16_t)(((uint16_t)frame[20] << 8) |
                                              frame[21]);
    heater->status.pump_hz_x10 = frame[23];
    heater->status.fan_rpm = (uint16_t)(((uint16_t)frame[28] << 8) |
                                       frame[29]);
    heater->status.received_tick = now;
    heater->status.valid = true;
    heater->valid_frames++;
    return true;
}

static void parse_byte(VEVOR_t *heater, uint8_t byte, uint32_t now)
{
    if (heater->rx_index == 0U) {
        if (byte == 0xAAU) heater->rx_frame[heater->rx_index++] = byte;
        return;
    }
    if (heater->rx_index == 1U) {
        if (byte == 0x77U) {
            heater->rx_frame[heater->rx_index++] = byte;
        } else if (byte == 0xAAU) {
            heater->rx_frame[0] = byte;
        } else {
            heater->rx_index = 0U;
        }
        return;
    }
    heater->rx_frame[heater->rx_index++] = byte;
    if (heater->rx_index == VEVOR_HEATER_FRAME_SIZE) {
        (void)decode_response(heater, heater->rx_frame, now);
        heater->rx_index = 0U;
    }
}

bool VEVOR_Init(VEVOR_t *heater, const htr_uart_t *uart, uint32_t now)
{
    if ((heater == NULL) || (uart == NULL)) return false;
    memset(heater, 0, sizeof(*heater));
    heater->uart = uart;
    heater->requested_level = 1U;
    heater->request_mode = VEVOR_REQUEST_STOP;
    /* Arrange for an immediate conservative STOP poll. */
    heater->last_tx_tick = now - VEVOR_TX_INTERVAL_MS;
    heater->last_rx_poll_tick = now;
    return true;
}

bool VEVOR_Start(VEVOR_t *heater, uint8_t level)
{
    if ((heater == NULL) || (level < 1U) || (level > 10U)) return false;
    heater->requested_level = level;
    heater->request_mode = VEVOR_REQUEST_START;
    heater->start_frames_remaining = VEVOR_START_FRAME_COUNT;
    return true;
}

bool VEVOR_SetLevel(VEVOR_t *heater, uint8_t level)
{
    if ((heater == NULL) || (level < 1U) || (level > 10U)) return false;
    heater->requested_level = level;
    return true;
}

void VEVOR_Stop(VEVOR_t *heater)
{
    if (heater == NULL) return;
    heater->request_mode = VEVOR_REQUEST_STOP;
    heater->start_frames_remaining = 0U;
}

bool VEVOR_IsResponseFresh(const VEVOR_t *heater, uint32_t now)
{
    return (heater != NULL) && heater->status.valid &&
           ((uint32_t)(now - heater->status.received_tick) <=
            VEVOR_RESPONSE_FRESH_MS);
}

bool VEVOR_IsSafeToPowerDown(const VEVOR_t *heater, uint32_t now)
{
    return VEVOR_IsResponseFresh(heater, now) &&
           (heater->status.state == VEVOR_REPORTED_OFF);
}

void VEVOR_Process(VEVOR_t *heater, uint32_t now)
{
    uint8_t data[64];
    uint8_t tx[VEVOR_CONTROLLER_FRAME_SIZE];
    size_t count;
    size_t i;
    if ((heater == NULL) || (heater->uart == NULL)) return;

    if ((uint32_t)(now - heater->last_rx_poll_tick) >=
        VEVOR_RX_POLL_INTERVAL_MS) {
        heater->last_rx_poll_tick = now;
        count = heater->uart->read(heater->uart->ctx, data, sizeof(data));
        for (i = 0U; i < count; ++i) parse_byte(heater, data[i], now);
    }

    if ((uint32_t)(now - heater->last_tx_tick) < VEVOR_TX_INTERVAL_MS) return;
    heater->last_tx_tick = now;

    if (heater->request_mode == VEVOR_REQUEST_START) {
        VEVOR_BuildStartFrame(heater->requested_level, tx);
    } else if (heater->request_mode == VEVOR_REQUEST_RUN) {
        VEVOR_BuildRunFrame(heater->requested_level, tx);
    } else {
        VEVOR_BuildStopFrame(heater->requested_level, tx);
    }

    if (heater->uart->write(heater->uart->ctx, tx, sizeof(tx)) != sizeof(tx)) {
        heater->transport_errors++;
        return;
    }

    if (heater->request_mode == VEVOR_REQUEST_START) {
        if (heater->start_frames_remaining > 0U) {
            heater->start_frames_remaining--;
        }
        if (heater->start_frames_remaining == 0U) {
            heater->request_mode = VEVOR_REQUEST_RUN;
        }
    }
}
````

Verify the move is transport-only: `diff <(sed -n '/^uint8_t VEVOR_Checksum/,/^bool VEVOR_Init/p' App/services/vevor_heater.c) <(sed -n '/^uint8_t VEVOR_Checksum/,/^bool VEVOR_Init/p' heater-common/vevor_proto.c)` prints only the `VEVOR_Init` signature line.

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build Tests/build --target test_vevor_proto && (cd Tests/build && ctest -R '^test_vevor_proto$' --output-on-failure)`
Expected: PASS (3 tests).

- [ ] **Step 5: Commit**

```bash
git add heater-common/vevor_proto.[ch] Tests/test_vevor_proto.c Tests/CMakeLists.txt
git commit -m "feat(heater-common): VEVOR protocol on the htr_uart seam

Verified frames/timing unchanged; only the WK2132 transport calls are
replaced by htr_uart_t write/read.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_017uxgfN1Y67gVKWmTWmpxxk"
```

---

### Task 4: Driver interface + VEVOR driver

**Files:**
- Create: `heater-coproc/core/htr_driver.h`, `heater-coproc/core/drv_vevor.h`, `heater-coproc/core/drv_vevor.c`, `Tests/test_drv_vevor.c`
- Modify: `Tests/CMakeLists.txt` (append)

**Interfaces:**
- Consumes: `VEVOR_*` (Task 3), `htr_types.h`, `htr_uart_t` (Task 1).
- Produces: `htr_driver_t {type_id, wiring, caps, ctx, init, process, start, stop, apply, probe, status}` with signatures exactly as in `htr_driver.h` below; `extern const htr_driver_t drv_vevor;`.

- [ ] **Step 1: Write the failing test**

`Tests/test_drv_vevor.c`:

````c
#include "drv_vevor.h"
#include "vevor_proto.h"
#include "fake_htr_uart.h"
#include "unity.h"
#include <string.h>

static fake_uart_t u;
void setUp(void){ fake_uart_init(&u); drv_vevor.init(drv_vevor.ctx,&u.api,0U); }
void tearDown(void){}

static void reply(uint8_t state, uint8_t level, uint8_t err){
    uint8_t r[56]={0}; r[0]=0xAA; r[1]=0x77; r[5]=state; r[6]=level; r[7]=err; r[11]=125;
    r[55]=VEVOR_Checksum(r,55U); fake_uart_inject(&u,r,56);
}

void test_identity(void){
    TEST_ASSERT_EQUAL_UINT8(HTR_TYPE_VEVOR, drv_vevor.type_id);
    TEST_ASSERT_EQUAL_UINT8(HTR_WIRING_3W, drv_vevor.wiring);
    TEST_ASSERT_TRUE(drv_vevor.caps & HTR_CAP_LEVEL);
}
void test_probe_found_on_valid_reply_and_sends_only_stop_polls(void){
    TEST_ASSERT_EQUAL_INT(HTR_PROBE_BUSY, drv_vevor.probe(drv_vevor.ctx,0U));
    reply(0,1,0);
    TEST_ASSERT_EQUAL_INT(HTR_PROBE_FOUND, drv_vevor.probe(drv_vevor.ctx,20U));
    for(size_t i=0;i<u.writes;i++){
        TEST_ASSERT_EQUAL_HEX8(0x06,u.tx[u.write_off[i]+2]);    /* control cmd */
        TEST_ASSERT_EQUAL_HEX8(0x05,u.tx[u.write_off[i]+9]);    /* STOP state */
    }
}
void test_probe_absent_after_timeout_with_only_stop_polls(void){
    htr_probe_t r=HTR_PROBE_BUSY; uint32_t t=0;
    while(r==HTR_PROBE_BUSY && t<10000U){ r=drv_vevor.probe(drv_vevor.ctx,t); t+=10U; }
    TEST_ASSERT_EQUAL_INT(HTR_PROBE_ABSENT,r);
    TEST_ASSERT_GREATER_OR_EQUAL_UINT32(3500U,t-10U);
    TEST_ASSERT_GREATER_THAN(0,(int)u.writes);
    for(size_t i=0;i<u.writes;i++) TEST_ASSERT_EQUAL_HEX8(0x05,u.tx[u.write_off[i]+9]);
}
void test_start_uses_request_level(void){
    htr_request_t rq={1,7,20,HTR_AMBIENT_UNKNOWN};
    drv_vevor.start(drv_vevor.ctx,&rq); fake_uart_clear_tx(&u);
    drv_vevor.process(drv_vevor.ctx,1000U);
    TEST_ASSERT_EQUAL_HEX8(0x06,u.tx[2]); TEST_ASSERT_EQUAL_HEX8(0x06,u.tx[9]);   /* START */
    TEST_ASSERT_EQUAL_UINT8(7,u.tx[8]);
}
void test_status_maps_states_and_valid_bits(void){
    htr_status_t s;
    drv_vevor.status(drv_vevor.ctx,0U,&s);
    TEST_ASSERT_FALSE(s.any_rx); TEST_ASSERT_EQUAL_HEX16(0,s.valid);
    const uint8_t st[5]={0,1,2,3,4};
    const uint8_t hint[5]={HTR_HINT_OFF,HTR_HINT_STARTING,HTR_HINT_STARTING,HTR_HINT_RUNNING,HTR_HINT_COOLDOWN};
    for(int i=0;i<5;i++){
        reply(st[i],5,0); drv_vevor.process(drv_vevor.ctx,(uint32_t)(100+i*20));
        drv_vevor.status(drv_vevor.ctx,0U,&s);
        TEST_ASSERT_EQUAL_UINT8(hint[i],s.hint);
    }
    TEST_ASSERT_TRUE(s.any_rx);
    TEST_ASSERT_EQUAL_UINT16(12500,s.supply_mv);
    TEST_ASSERT_TRUE(s.valid & HTR_V_SUPPLY_MV);
    TEST_ASSERT_FALSE(s.valid & HTR_V_EXCH_C);              /* never characterised */
}
int main(void){ UNITY_BEGIN();
    RUN_TEST(test_identity);
    RUN_TEST(test_probe_found_on_valid_reply_and_sends_only_stop_polls);
    RUN_TEST(test_probe_absent_after_timeout_with_only_stop_polls);
    RUN_TEST(test_start_uses_request_level);
    RUN_TEST(test_status_maps_states_and_valid_bits);
    return UNITY_END(); }
````

Append to `Tests/CMakeLists.txt`:

```cmake
add_heater_test(test_drv_vevor test_drv_vevor.c ${HCORE}/drv_vevor.c ${HC}/vevor_proto.c fakes/fake_htr_uart.c)
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake -S Tests -B Tests/build >/dev/null && cmake --build Tests/build --target test_drv_vevor`
Expected: FAIL — `drv_vevor.h: No such file or directory`.

- [ ] **Step 3: Write the implementation**

`heater-coproc/core/htr_driver.h`:

````c
#ifndef HTR_DRIVER_H
#define HTR_DRIVER_H
/* One heater protocol = one htr_driver_t. Adding a heater means adding a
   driver and one line in the registry passed to htr_fsm_init(); the FSM,
   register map and G0B1 side never change. */
#include "htr_types.h"
#include "htr_uart.h"

typedef struct htr_driver {
    uint8_t     type_id;   /* HTR_TYPE_* */
    uint8_t     wiring;    /* HTR_WIRING_3W / HTR_WIRING_4W */
    uint16_t    caps;      /* HTR_CAP_* */
    void       *ctx;
    /* Reset all protocol state and bind the UART. Sends nothing. */
    void (*init)(void *ctx, const htr_uart_t *u, uint32_t now);
    /* Non-blocking service: RX parse + scheduled TX. */
    void (*process)(void *ctx, uint32_t now);
    void (*start)(void *ctx, const htr_request_t *rq);
    /* Request a stop; the protocol keeps talking through cooldown. Never cuts power. */
    void (*stop)(void *ctx);
    /* Live level / setpoint / ambient update. */
    void (*apply)(void *ctx, const htr_request_t *rq);
    /* One auto-detect step (call repeatedly after init). May only transmit
       requests that cannot start a heater or change its settings. */
    htr_probe_t (*probe)(void *ctx, uint32_t now);
    void (*status)(const void *ctx, uint32_t now, htr_status_t *out);
} htr_driver_t;

#endif /* HTR_DRIVER_H */
````

`heater-coproc/core/drv_vevor.h`:

````c
#ifndef DRV_VEVOR_H
#define DRV_VEVOR_H
#include "htr_driver.h"
extern const htr_driver_t drv_vevor;
#endif
````

`heater-coproc/core/drv_vevor.c`:

````c
/* htr_driver_t adapter over the verified VEVOR protocol (vevor_proto.c). */
#include "drv_vevor.h"
#include "vevor_proto.h"
#include <string.h>

#define VEVOR_PROBE_TIMEOUT_MS 3500u   /* ~3 stop polls without a valid reply */

typedef struct {
    VEVOR_t  v;
    uint32_t init_tick;
} vevor_ctx_t;

static vevor_ctx_t s_ctx;

static void v_init(void *ctx, const htr_uart_t *u, uint32_t now)
{
    vevor_ctx_t *c = (vevor_ctx_t *)ctx;
    (void)VEVOR_Init(&c->v, u, now);
    c->init_tick = now;
}

static void v_process(void *ctx, uint32_t now)
{
    VEVOR_Process(&((vevor_ctx_t *)ctx)->v, now);
}

static void v_start(void *ctx, const htr_request_t *rq)
{
    (void)VEVOR_Start(&((vevor_ctx_t *)ctx)->v, rq->level);
}

static void v_stop(void *ctx)
{
    VEVOR_Stop(&((vevor_ctx_t *)ctx)->v);
}

static void v_apply(void *ctx, const htr_request_t *rq)
{
    (void)VEVOR_SetLevel(&((vevor_ctx_t *)ctx)->v, rq->level);
}

/* The VEVOR answers only control frames; its standby poll is a STOP frame,
   which is harmless to an idle heater (and starts a normal cooldown on a
   running one -- see spec 4.1). */
static htr_probe_t v_probe(void *ctx, uint32_t now)
{
    vevor_ctx_t *c = (vevor_ctx_t *)ctx;
    VEVOR_Process(&c->v, now);
    if (c->v.valid_frames > 0u) return HTR_PROBE_FOUND;
    if ((uint32_t)(now - c->init_tick) >= VEVOR_PROBE_TIMEOUT_MS) return HTR_PROBE_ABSENT;
    return HTR_PROBE_BUSY;
}

static uint8_t hint_for(uint8_t state)
{
    switch (state) {
    case VEVOR_REPORTED_OFF:      return HTR_HINT_OFF;
    case VEVOR_REPORTED_PREHEAT:
    case VEVOR_REPORTED_IGNITION: return HTR_HINT_STARTING;
    case VEVOR_REPORTED_RUNNING:  return HTR_HINT_RUNNING;
    case VEVOR_REPORTED_COOLDOWN: return HTR_HINT_COOLDOWN;
    default:                      return HTR_HINT_UNKNOWN;
    }
}

static void v_status(const void *ctx, uint32_t now, htr_status_t *out)
{
    const VEVOR_t *v = &((const vevor_ctx_t *)ctx)->v;
    (void)now;
    memset(out, 0, sizeof(*out));
    out->valid_frames = v->valid_frames;
    out->crc_fails = v->checksum_failures;
    out->transport_errs = v->transport_errors;
    if (!v->status.valid) return;
    out->any_rx = true;
    out->last_rx_tick = v->status.received_tick;
    out->hint = hint_for(v->status.state);
    out->vendor_state = v->status.state;
    out->fault_code = v->status.error;
    out->active_level = v->status.level;
    out->supply_mv = v->status.supply_mv;
    out->fan_rpm = v->status.fan_rpm;
    out->pump_hz_x10 = v->status.pump_hz_x10;
    out->exch_raw = v->status.exchanger_raw;
    out->state_seconds = v->status.state_seconds;
    out->valid = HTR_V_FAULT_CODE | HTR_V_ACTIVE_LEVEL | HTR_V_SUPPLY_MV |
                 HTR_V_FAN_RPM | HTR_V_PUMP_HZ | HTR_V_EXCH_RAW |
                 HTR_V_STATE_SECONDS;
}

const htr_driver_t drv_vevor = {
    HTR_TYPE_VEVOR, HTR_WIRING_3W,
    HTR_CAP_LEVEL | HTR_CAP_FAN_RPM | HTR_CAP_PUMP | HTR_CAP_SUPPLY_V |
        HTR_CAP_EXCH_TEMP | HTR_CAP_FAULT_CODE,
    &s_ctx,
    v_init, v_process, v_start, v_stop, v_apply, v_probe, v_status
};
````

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build Tests/build --target test_drv_vevor && (cd Tests/build && ctest -R '^test_drv_vevor$' --output-on-failure)`
Expected: PASS (5 tests).

- [ ] **Step 5: Commit**

```bash
git add heater-coproc/core/htr_driver.h heater-coproc/core/drv_vevor.[ch] Tests/test_drv_vevor.c Tests/CMakeLists.txt
git commit -m "feat(heater-coproc): driver vtable + VEVOR driver

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_017uxgfN1Y67gVKWmTWmpxxk"
```

---

### Task 5: AUTOTERM driver (wake/baud sequencer + poll scheduler)

**Files:**
- Create: `heater-coproc/core/drv_autoterm.h`, `heater-coproc/core/drv_autoterm.c`, `Tests/test_drv_autoterm.c`
- Modify: `Tests/CMakeLists.txt` (append)

**Interfaces:**
- Consumes: `AT_*` (Task 2), `htr_driver_t` (Task 4), `fake_uart_t` (Task 1).
- Produces: `extern const htr_driver_t drv_autoterm;` and the timing macros `AT_WAKE_COUNT 35`, `AT_WAKE_GAP_MS 200`, `AT_VER4800_COUNT 5`, `AT_VER_GAP_MS 200`, `AT_VER9600_TIMEOUT_MS 3000`, `AT_WAKE_ATTEMPTS 3`, `AT_TX_SLOT_MS 250`, `AT_STATUS_EVERY_MS 1000`, `AT_SETTINGS_EVERY_MS 5000`, `AT_TEMP_EVERY_MS 10000`, `AT_CMD_RETRY_MS 1000`, `AT_START_TRIES 10`, `AT_LINK_LOST_MS 10000`.

Behaviour summary (spec §5.2): wake `1C` ×35 @200 ms at 2400 → version `06` ×5 @200 ms at 4800 → version every 200 ms at 9600 until a valid version reply (3 s timeout → re-wake; 3 failed wakes → ABSENT). Online: one TX per 250 ms slot, priority stop > start > settings-write > status (1 s) > settings-read (5 s) > temp (10 s, only if ambient known). Start re-sent each 1 s until status major ≠ 0 (max 10 tries); stop re-sent each 1 s until major is 0 or 4. No valid reply for 10 s → re-wake. Only supply voltage and exchanger raw are marked valid.

- [ ] **Step 1: Write the failing test**

`Tests/test_drv_autoterm.c`:

````c
#include "drv_autoterm.h"
#include "autoterm_proto.h"
#include "fake_htr_uart.h"
#include "unity.h"
#include <string.h>

static fake_uart_t u;
static const htr_driver_t *d = &drv_autoterm;
static uint8_t hs_major = 0, hs_minor = 1;   /* simulated heater state */
static bool heater_alive = true;
static size_t answered;                       /* writes already answered */

static void heater_reply(uint8_t cmd, const uint8_t *p, uint8_t len){
    uint8_t f[AT_MAX_FRAME]; size_t n = AT_BuildFrame(AT_DIR_RESP, cmd, p, len, f);
    fake_uart_inject(&u, f, n);
}
/* Simulated heater: answers version only at 9600, status at 9600. */
static void respond(void){
    for (; answered < u.writes; answered++) {
        if (!heater_alive || u.write_baud[answered] != 9600u) continue;
        uint8_t cmd = fake_uart_write_cmd(&u, answered);
        if (cmd == AT_CMD_VERSION) { const uint8_t v[5]={2,1,3,5,3}; heater_reply(cmd, v, 5); }
        if (cmd == AT_CMD_STATUS) {
            uint8_t s[19]={0}; s[0]=hs_major; s[1]=hs_minor; s[3]=29; s[4]=0x7F; s[6]=126; s[7]=1; s[8]=0x2E;
            heater_reply(cmd, s, 19);
        }
    }
}
static uint32_t run(uint32_t from, uint32_t to, bool probing, htr_probe_t *last){
    htr_probe_t r = HTR_PROBE_BUSY;
    for (uint32_t t = from; t < to; t += 10u) {
        if (probing) r = d->probe(d->ctx, t); else d->process(d->ctx, t);
        respond();
        if (probing && r != HTR_PROBE_BUSY) { if (last) *last = r; return t; }
    }
    if (last) *last = r;
    return to;
}
static size_t count_cmd(uint8_t cmd, size_t from){
    size_t n=0; for(size_t i=from;i<u.writes;i++) if(fake_uart_write_cmd(&u,i)==cmd) n++; return n;
}
static size_t last_index_of(uint8_t cmd){
    size_t r=(size_t)-1; for(size_t i=0;i<u.writes;i++) if(fake_uart_write_cmd(&u,i)==cmd) r=i; return r;
}
void setUp(void){ fake_uart_init(&u); hs_major=0; hs_minor=1; heater_alive=true; answered=0; d->init(d->ctx,&u.api,0U); }
void tearDown(void){}

void test_identity(void){
    TEST_ASSERT_EQUAL_UINT8(HTR_TYPE_AUTOTERM, d->type_id);
    TEST_ASSERT_EQUAL_UINT8(HTR_WIRING_4W, d->wiring);
    TEST_ASSERT_TRUE(d->caps & HTR_CAP_SETPOINT);
    TEST_ASSERT_FALSE(d->caps & HTR_CAP_LEVEL);
}
void test_wake_sequence_bauds_counts_and_gaps(void){
    heater_alive = false;
    run(0, 9000, true, NULL);
    TEST_ASSERT_GREATER_OR_EQUAL(41, (int)u.writes);
    for (size_t i = 0; i < 35; i++) {
        TEST_ASSERT_EQUAL_HEX8(AT_CMD_WAKE, fake_uart_write_cmd(&u, i));
        TEST_ASSERT_EQUAL_UINT32(2400u, u.write_baud[i]);
    }
    for (size_t i = 35; i < 40; i++) {
        TEST_ASSERT_EQUAL_HEX8(AT_CMD_VERSION, fake_uart_write_cmd(&u, i));
        TEST_ASSERT_EQUAL_UINT32(4800u, u.write_baud[i]);
    }
    TEST_ASSERT_EQUAL_HEX8(AT_CMD_VERSION, fake_uart_write_cmd(&u, 40));
    TEST_ASSERT_EQUAL_UINT32(9600u, u.write_baud[40]);
}
void test_probe_found_when_version_answers_at_9600(void){
    htr_probe_t r; uint32_t t = run(0, 20000, true, &r);
    TEST_ASSERT_EQUAL_INT(HTR_PROBE_FOUND, r);
    TEST_ASSERT_LESS_THAN_UINT32(9000u, t);
}
void test_probe_absent_after_three_wakes_and_sends_only_wake_or_version(void){
    heater_alive = false;
    htr_probe_t r; run(0, 60000, true, &r);
    TEST_ASSERT_EQUAL_INT(HTR_PROBE_ABSENT, r);
    TEST_ASSERT_EQUAL(3u*AT_WAKE_COUNT, count_cmd(AT_CMD_WAKE, 0));
    for (size_t i = 0; i < u.writes; i++) {
        uint8_t c = fake_uart_write_cmd(&u, i);
        TEST_ASSERT_TRUE(c == AT_CMD_WAKE || c == AT_CMD_VERSION);
    }
}
static uint32_t go_online(void){
    htr_probe_t r; uint32_t t = run(0, 20000, true, &r);
    TEST_ASSERT_EQUAL_INT(HTR_PROBE_FOUND, r); return t + 10u;
}
void test_online_polls_status_settings_and_temp_only_when_known(void){
    uint32_t t = go_online(); size_t from = u.writes;
    run(t, t + 20000, false, NULL);
    size_t st = count_cmd(AT_CMD_STATUS, from);
    TEST_ASSERT_TRUE(st >= 19 && st <= 21);
    size_t se = count_cmd(AT_CMD_SETTINGS, from);
    TEST_ASSERT_TRUE(se >= 3 && se <= 5);
    TEST_ASSERT_EQUAL(0, count_cmd(AT_CMD_TEMP, from));      /* ambient unknown */
    htr_request_t rq = {0, 1, 20, 231};                     /* 23.1 C */
    d->apply(d->ctx, &rq); from = u.writes;
    run(t + 20000, t + 31000, false, NULL);
    size_t i = last_index_of(AT_CMD_TEMP);
    TEST_ASSERT_TRUE(i != (size_t)-1 && i >= from);
    TEST_ASSERT_EQUAL_UINT8(23, u.tx[u.write_off[i] + 5]);
}
void test_start_sends_captured_frame_until_heater_leaves_standby(void){
    uint32_t t = go_online();
    htr_request_t rq = {1, 1, 29, HTR_AMBIENT_UNKNOWN};
    d->start(d->ctx, &rq); size_t from = u.writes;
    run(t, t + 3000, false, NULL);                          /* heater still in 0.1 */
    size_t n1 = count_cmd(AT_CMD_START, from);
    TEST_ASSERT_TRUE(n1 >= 2 && n1 <= 4);                   /* retried ~1/s */
    size_t i = last_index_of(AT_CMD_START);
    const uint8_t st29[]={0xAA,0x03,0x07,0x00,0x01,0xFF,0xFF,0x02,0x1D,0x01,0xFF,0x11,0x19,0x34};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(st29, &u.tx[u.write_off[i]], 14);
    hs_major = 1; hs_minor = 0; from = u.writes;
    run(t + 3000, t + 8000, false, NULL);
    TEST_ASSERT_LESS_OR_EQUAL(1, (int)count_cmd(AT_CMD_START, from)); /* at most one in flight */
    from = u.writes; run(t + 8000, t + 12000, false, NULL);
    TEST_ASSERT_EQUAL(0, count_cmd(AT_CMD_START, from));
}
void test_stop_repeats_until_cooldown_then_keeps_polling(void){
    uint32_t t = go_online(); hs_major = 2; hs_minor = 4;
    d->stop(d->ctx); size_t from = u.writes;
    run(t, t + 3000, false, NULL);
    TEST_ASSERT_GREATER_OR_EQUAL(2, (int)count_cmd(AT_CMD_STOP, from));
    hs_major = 4; hs_minor = 0; run(t + 3000, t + 5000, false, NULL);
    from = u.writes; run(t + 5000, t + 10000, false, NULL);
    TEST_ASSERT_EQUAL(0, count_cmd(AT_CMD_STOP, from));
    TEST_ASSERT_GREATER_OR_EQUAL(4, (int)count_cmd(AT_CMD_STATUS, from));
    htr_status_t s; d->status(d->ctx, 0, &s);
    TEST_ASSERT_EQUAL_UINT8(HTR_HINT_COOLDOWN, s.hint);
}
void test_setpoint_change_while_running_sends_settings_write(void){
    uint32_t t = go_online();
    htr_request_t rq = {1, 1, 22, HTR_AMBIENT_UNKNOWN};
    d->start(d->ctx, &rq); hs_major = 2; hs_minor = 4;
    run(t, t + 3000, false, NULL);
    rq.setpoint_c = 25; d->apply(d->ctx, &rq); size_t from = u.writes;
    run(t + 3000, t + 4000, false, NULL);
    size_t found = 0;
    for (size_t i = from; i < u.writes; i++)
        if (fake_uart_write_cmd(&u, i) == AT_CMD_SETTINGS && u.write_len[i] == 13) {
            TEST_ASSERT_EQUAL_UINT8(25, u.tx[u.write_off[i] + 8]); found++;
        }
    TEST_ASSERT_EQUAL(1, found);
}
void test_status_valid_bits_only_verified_fields(void){
    uint32_t t = go_online(); run(t, t + 2000, false, NULL);
    htr_status_t s; d->status(d->ctx, 0, &s);
    TEST_ASSERT_TRUE(s.any_rx);
    TEST_ASSERT_EQUAL_UINT8(HTR_HINT_OFF, s.hint);
    TEST_ASSERT_EQUAL_UINT8(0, s.vendor_state); TEST_ASSERT_EQUAL_UINT8(1, s.vendor_sub);
    TEST_ASSERT_EQUAL_UINT16(12600, s.supply_mv);
    TEST_ASSERT_EQUAL_HEX16(0x012E, s.exch_raw);
    TEST_ASSERT_EQUAL_HEX16(HTR_V_SUPPLY_MV | HTR_V_EXCH_RAW, s.valid);
}
void test_link_lost_rewakes_at_2400(void){
    uint32_t t = go_online(); heater_alive = false; size_t from = u.writes;
    run(t, t + 12000, false, NULL);
    size_t i = last_index_of(AT_CMD_WAKE);
    TEST_ASSERT_TRUE(i != (size_t)-1 && i >= from);
    TEST_ASSERT_EQUAL_UINT32(2400u, u.write_baud[i]);
}
int main(void){ UNITY_BEGIN();
    RUN_TEST(test_identity);
    RUN_TEST(test_wake_sequence_bauds_counts_and_gaps);
    RUN_TEST(test_probe_found_when_version_answers_at_9600);
    RUN_TEST(test_probe_absent_after_three_wakes_and_sends_only_wake_or_version);
    RUN_TEST(test_online_polls_status_settings_and_temp_only_when_known);
    RUN_TEST(test_start_sends_captured_frame_until_heater_leaves_standby);
    RUN_TEST(test_stop_repeats_until_cooldown_then_keeps_polling);
    RUN_TEST(test_setpoint_change_while_running_sends_settings_write);
    RUN_TEST(test_status_valid_bits_only_verified_fields);
    RUN_TEST(test_link_lost_rewakes_at_2400);
    return UNITY_END(); }
````

Append to `Tests/CMakeLists.txt`:

```cmake
add_heater_test(test_drv_autoterm test_drv_autoterm.c ${HCORE}/drv_autoterm.c ${HC}/autoterm_proto.c ${HC}/crc16_modbus.c fakes/fake_htr_uart.c)
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake -S Tests -B Tests/build >/dev/null && cmake --build Tests/build --target test_drv_autoterm`
Expected: FAIL — `drv_autoterm.h: No such file or directory`.

- [ ] **Step 3: Write the implementation**

`heater-coproc/core/drv_autoterm.h`:

````c
#ifndef DRV_AUTOTERM_H
#define DRV_AUTOTERM_H
#include "htr_driver.h"
extern const htr_driver_t drv_autoterm;

/* Link timing (ms), exposed for tests. */
#define AT_WAKE_COUNT        35u
#define AT_WAKE_GAP_MS      200u
#define AT_VER4800_COUNT      5u
#define AT_VER_GAP_MS       200u
#define AT_VER9600_TIMEOUT_MS 3000u
#define AT_WAKE_ATTEMPTS      3u
#define AT_TX_SLOT_MS       250u
#define AT_STATUS_EVERY_MS 1000u
#define AT_SETTINGS_EVERY_MS 5000u
#define AT_TEMP_EVERY_MS  10000u
#define AT_CMD_RETRY_MS    1000u
#define AT_START_TRIES       10u
#define AT_LINK_LOST_MS   10000u
#endif
````

`heater-coproc/core/drv_autoterm.c`:

````c
/* AUTOTERM 4-wire driver: wake/baud sequencer + online poll scheduler.
   Only the verified request set is ever sent (spec 5.2). */
#include "drv_autoterm.h"
#include "autoterm_proto.h"
#include <string.h>

typedef enum {
    AT_S_WAKE = 0,     /* 2400: wake frames            */
    AT_S_VER4800,      /* 4800: version requests        */
    AT_S_VER9600,      /* 9600: version until a reply   */
    AT_S_ONLINE,       /* 9600: polling + control       */
    AT_S_ABSENT        /* wake attempts exhausted       */
} at_link_t;

typedef struct {
    const htr_uart_t *u;
    at_parser_t parser;
    at_link_t link;
    uint8_t   sent;            /* frames sent in the current link step */
    uint8_t   attempts;        /* wake attempts */
    uint32_t  step_tick;       /* last TX / step start */
    uint32_t  last_tx;
    uint32_t  last_status_tx, last_settings_tx, last_temp_tx;
    uint32_t  last_rx_tick;
    bool      any_rx;
    /* control */
    bool      want_run;        /* start requested and not yet stopped */
    bool      start_pending;
    uint8_t   start_tries;
    bool      stop_pending;
    bool      settings_dirty;
    uint32_t  last_cmd_tx;
    bool      cmd_due;         /* send start/stop at the next TX slot */
    int8_t    setpoint_c;
    int16_t   ambient_c_x10;
    /* last decoded status */
    bool      have_status;
    at_status_t st;
    uint32_t  valid_frames;
    uint32_t  transport_errs;
} at_ctx_t;

static at_ctx_t s_ctx;

static void tx(at_ctx_t *c, const uint8_t *f, size_t n, uint32_t now)
{
    if (c->u->write(c->u->ctx, f, n) != n) c->transport_errs++;
    c->last_tx = now;
}

static void tx_simple(at_ctx_t *c, uint8_t cmd, uint32_t now)
{
    uint8_t f[AT_OVERHEAD];
    tx(c, f, AT_BuildSimple(cmd, f), now);
}

static void enter(at_ctx_t *c, at_link_t link, uint32_t baud, uint32_t now)
{
    if ((baud != 0u) && !c->u->set_baud(c->u->ctx, baud)) return; /* TX busy: retry next pass */
    c->link = link;
    c->sent = 0u;
    c->step_tick = now;
    if (baud != 0u) {
        AT_ParserReset(&c->parser);
        c->u->flush_rx(c->u->ctx);
    }
}

static void a_init(void *ctx, const htr_uart_t *u, uint32_t now)
{
    at_ctx_t *c = (at_ctx_t *)ctx;
    memset(c, 0, sizeof(*c));
    c->u = u;
    c->ambient_c_x10 = HTR_AMBIENT_UNKNOWN;
    c->setpoint_c = 20;
    c->link = AT_S_WAKE;
    c->step_tick = now - AT_WAKE_GAP_MS;
    c->last_tx = now - AT_WAKE_GAP_MS;
    (void)c->u->set_baud(c->u->ctx, 2400u);
}

static void rx(at_ctx_t *c, uint32_t now)
{
    uint8_t buf[32];
    at_frame_t fr;
    size_t n, i;
    while ((n = c->u->read(c->u->ctx, buf, sizeof(buf))) > 0u) {
        for (i = 0u; i < n; ++i) {
            if (!AT_ParseByte(&c->parser, buf[i], &fr)) continue;
            c->valid_frames++;
            c->any_rx = true;
            c->last_rx_tick = now;
            if ((c->link == AT_S_VER9600) && (fr.cmd == AT_CMD_VERSION)) {
                c->link = AT_S_ONLINE;
                c->attempts = 0u;
                c->last_status_tx = now - AT_STATUS_EVERY_MS;
                c->last_settings_tx = now;
                c->last_temp_tx = now - AT_TEMP_EVERY_MS;
            }
            if (AT_DecodeStatus(&fr, &c->st)) {
                c->have_status = true;
                /* Start acknowledged once the heater leaves standby; stop once it
                   leaves starting/running (cooldown or standby). */
                if (c->start_pending && (c->st.major != 0u)) c->start_pending = false;
                if (c->stop_pending && ((c->st.major == 0u) || (c->st.major == 4u))) {
                    c->stop_pending = false;
                }
            }
        }
    }
}

static void link_step(at_ctx_t *c, uint32_t now)
{
    switch (c->link) {
    case AT_S_WAKE:
        if ((uint32_t)(now - c->last_tx) < AT_WAKE_GAP_MS) return;
        if (c->sent >= AT_WAKE_COUNT) { enter(c, AT_S_VER4800, 4800u, now); return; }
        tx_simple(c, AT_CMD_WAKE, now);
        c->sent++;
        return;
    case AT_S_VER4800:
        if ((uint32_t)(now - c->last_tx) < AT_VER_GAP_MS) return;
        if (c->sent >= AT_VER4800_COUNT) { enter(c, AT_S_VER9600, 9600u, now); return; }
        tx_simple(c, AT_CMD_VERSION, now);
        c->sent++;
        return;
    case AT_S_VER9600:
        if ((uint32_t)(now - c->step_tick) >= AT_VER9600_TIMEOUT_MS) {
            c->attempts++;
            if (c->attempts >= AT_WAKE_ATTEMPTS) { c->link = AT_S_ABSENT; return; }
            enter(c, AT_S_WAKE, 2400u, now);
            return;
        }
        if ((uint32_t)(now - c->last_tx) < AT_VER_GAP_MS) return;
        tx_simple(c, AT_CMD_VERSION, now);
        return;
    default:
        return;
    }
}

static void online_step(at_ctx_t *c, uint32_t now)
{
    uint8_t f[AT_MAX_FRAME];
    if ((uint32_t)(now - c->last_rx_tick) >= AT_LINK_LOST_MS) {
        enter(c, AT_S_WAKE, 2400u, now);      /* re-wake; harmless to a running heater */
        c->attempts = 0u;
        return;
    }
    if ((uint32_t)(now - c->last_tx) < AT_TX_SLOT_MS) return;

    if (c->stop_pending &&
        (c->cmd_due || ((uint32_t)(now - c->last_cmd_tx) >= AT_CMD_RETRY_MS))) {
        tx_simple(c, AT_CMD_STOP, now);
        c->last_cmd_tx = now;
        c->cmd_due = false;
        return;
    }
    if (c->start_pending &&
        (c->cmd_due || ((uint32_t)(now - c->last_cmd_tx) >= AT_CMD_RETRY_MS))) {
        if (c->start_tries >= AT_START_TRIES) {
            c->start_pending = false;          /* FSM start timeout will raise FAULT */
        } else {
            tx(c, f, AT_BuildStart(c->setpoint_c, f), now);
            c->start_tries++;
            c->last_cmd_tx = now;
            c->cmd_due = false;
            return;
        }
    }
    if (c->settings_dirty && c->want_run && !c->start_pending) {
        tx(c, f, AT_BuildSettingsWrite(c->setpoint_c, f), now);
        c->settings_dirty = false;
        return;
    }
    if ((uint32_t)(now - c->last_status_tx) >= AT_STATUS_EVERY_MS) {
        tx_simple(c, AT_CMD_STATUS, now);
        c->last_status_tx = now;
        return;
    }
    if ((uint32_t)(now - c->last_settings_tx) >= AT_SETTINGS_EVERY_MS) {
        tx_simple(c, AT_CMD_SETTINGS, now);
        c->last_settings_tx = now;
        return;
    }
    if ((c->ambient_c_x10 != HTR_AMBIENT_UNKNOWN) &&
        ((uint32_t)(now - c->last_temp_tx) >= AT_TEMP_EVERY_MS)) {
        int16_t a = c->ambient_c_x10;
        int8_t whole = (int8_t)((a >= 0) ? ((a + 5) / 10) : ((a - 5) / 10));
        tx(c, f, AT_BuildTemp(whole, f), now);
        c->last_temp_tx = now;
    }
}

static void a_process(void *ctx, uint32_t now)
{
    at_ctx_t *c = (at_ctx_t *)ctx;
    rx(c, now);
    if (c->link == AT_S_ONLINE) online_step(c, now);
    else link_step(c, now);
}

static void a_apply(void *ctx, const htr_request_t *rq)
{
    at_ctx_t *c = (at_ctx_t *)ctx;
    if (rq->setpoint_c != c->setpoint_c) {
        c->setpoint_c = rq->setpoint_c;
        c->settings_dirty = true;
    }
    c->ambient_c_x10 = rq->ambient_c_x10;
}

static void a_start(void *ctx, const htr_request_t *rq)
{
    at_ctx_t *c = (at_ctx_t *)ctx;
    a_apply(ctx, rq);
    c->settings_dirty = false;       /* setpoint travels in the start frame */
    c->want_run = true;
    c->stop_pending = false;
    c->start_pending = true;
    c->start_tries = 0u;
    c->cmd_due = true;
}

static void a_stop(void *ctx)
{
    at_ctx_t *c = (at_ctx_t *)ctx;
    c->want_run = false;
    c->start_pending = false;
    c->stop_pending = true;
    c->cmd_due = true;
}

/* Probe = the link sequence only: wake + version requests. */
static htr_probe_t a_probe(void *ctx, uint32_t now)
{
    at_ctx_t *c = (at_ctx_t *)ctx;
    rx(c, now);
    if (c->link == AT_S_ONLINE) return HTR_PROBE_FOUND;
    if (c->link == AT_S_ABSENT) return HTR_PROBE_ABSENT;
    link_step(c, now);
    return HTR_PROBE_BUSY;
}

static void a_status(const void *ctx, uint32_t now, htr_status_t *out)
{
    const at_ctx_t *c = (const at_ctx_t *)ctx;
    (void)now;
    memset(out, 0, sizeof(*out));
    out->valid_frames = c->valid_frames;
    out->crc_fails = c->parser.crc_fails;
    out->transport_errs = c->transport_errs;
    out->any_rx = c->any_rx;
    out->last_rx_tick = c->last_rx_tick;
    if (!c->have_status) return;
    out->hint = AT_PhaseHint(c->st.major, c->st.minor);
    out->vendor_state = c->st.major;
    out->vendor_sub = c->st.minor;
    out->supply_mv = c->st.supply_mv;
    out->exch_raw = c->st.exch_raw;
    /* Provisional fields are carried but NOT marked valid (spec 5.2). */
    out->fan_rpm = (uint16_t)((uint16_t)c->st.fan_rps * 60u);
    out->pump_hz_x10 = (uint16_t)(c->st.pump_hz_x100 / 10u);
    out->valid = HTR_V_SUPPLY_MV | HTR_V_EXCH_RAW;
}

const htr_driver_t drv_autoterm = {
    HTR_TYPE_AUTOTERM, HTR_WIRING_4W,
    HTR_CAP_SETPOINT | HTR_CAP_AMBIENT_IN | HTR_CAP_SUPPLY_V | HTR_CAP_EXCH_TEMP,
    &s_ctx,
    a_init, a_process, a_start, a_stop, a_apply, a_probe, a_status
};
````

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build Tests/build --target test_drv_autoterm && (cd Tests/build && ctest -R '^test_drv_autoterm$' --output-on-failure)`
Expected: PASS (10 tests).

- [ ] **Step 5: Commit**

```bash
git add heater-coproc/core/drv_autoterm.[ch] Tests/test_drv_autoterm.c Tests/CMakeLists.txt
git commit -m "feat(heater-coproc): AUTOTERM driver (wake/baud sequencer, poll scheduler)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_017uxgfN1Y67gVKWmTWmpxxk"
```

---

### Task 6: Interface sequencing (`htr_iface`)

**Files:**
- Create: `heater-coproc/core/htr_iface.h`, `heater-coproc/core/htr_iface.c`, `Tests/fakes/fake_htr_gpio.h`, `Tests/fakes/fake_htr_gpio.c`, `Tests/test_htr_iface.c`
- Modify: `Tests/CMakeLists.txt` (append)

**Interfaces:**
- Consumes: `HTR_WIRING_*` (Task 1).
- Produces: `htr_line_t {HTR_LINE_TX_MODE, HTR_LINE_TX_EN, HTR_LINE_TX_PWR, HTR_LINE_COUNT}` (semantic "active": MODE active = 4-wire path, EN active = TX connected, PWR active = TX supply on); `htr_gpio_t {set_line, attach_uart, rx_level, ctx}`; `HTR_IFACE_SETTLE_MS 5`; `htr_iface_init(const htr_gpio_t*, bool pwr_for_3w)`, `htr_iface_enable(uint8_t wiring, uint32_t now)`, `htr_iface_disable(void)`, `htr_iface_process(uint32_t now)`, `bool htr_iface_ready(void)`, `uint8_t htr_iface_wiring(void)`, `bool htr_iface_rx_level(uint8_t wiring)`; `fake_gpio_t` + `fake_gpio_init/clear_log`, `EV_LINE`, `EV_ATTACH`.

- [ ] **Step 1: Write the failing test**

`Tests/test_htr_iface.c`:

````c
#include "htr_iface.h"
#include "htr_types.h"
#include "fake_htr_gpio.h"
#include "unity.h"

static fake_gpio_t g;
void setUp(void){ fake_gpio_init(&g); }
void tearDown(void){}

#define LINE(l,v)  {EV_LINE,(uint8_t)(l),(uint8_t)(v)}
#define ATT(w,v)   {EV_ATTACH,(uint8_t)(w),(uint8_t)(v)}
static void expect(const fake_ev_t *want, size_t n){
    TEST_ASSERT_EQUAL_size_t(n, g.n);
    for(size_t i=0;i<n;i++){
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(want[i].kind,g.ev[i].kind,"kind");
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(want[i].a,g.ev[i].a,"a");
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(want[i].b,g.ev[i].b,"b");
    }
}
void test_init_drives_everything_safe_disconnect_first(void){
    htr_iface_init(&g.api,false);
    const fake_ev_t w[]={LINE(HTR_LINE_TX_EN,0),ATT(HTR_WIRING_3W,0),ATT(HTR_WIRING_4W,0),
                         LINE(HTR_LINE_TX_PWR,0),LINE(HTR_LINE_TX_MODE,0)};
    expect(w,5);
    TEST_ASSERT_FALSE(htr_iface_ready());
    TEST_ASSERT_EQUAL_UINT8(HTR_WIRING_NONE, htr_iface_wiring());
}
void test_enable_4w_powers_settles_then_attaches_and_connects(void){
    htr_iface_init(&g.api,false); fake_gpio_clear_log(&g);
    htr_iface_enable(HTR_WIRING_4W,100);
    const fake_ev_t w1[]={LINE(HTR_LINE_TX_EN,0),LINE(HTR_LINE_TX_MODE,1),LINE(HTR_LINE_TX_PWR,1)};
    expect(w1,3);
    htr_iface_process(104); TEST_ASSERT_FALSE(htr_iface_ready()); TEST_ASSERT_EQUAL_size_t(3,g.n);
    htr_iface_process(105); TEST_ASSERT_TRUE(htr_iface_ready());
    const fake_ev_t w2[]={LINE(HTR_LINE_TX_EN,0),LINE(HTR_LINE_TX_MODE,1),LINE(HTR_LINE_TX_PWR,1),
                          ATT(HTR_WIRING_4W,1),LINE(HTR_LINE_TX_EN,1)};
    expect(w2,5);
}
void test_enable_3w_leaves_tx_power_off_unless_board_says(void){
    htr_iface_init(&g.api,false); htr_iface_enable(HTR_WIRING_3W,0); htr_iface_process(10);
    TEST_ASSERT_FALSE(g.line[HTR_LINE_TX_PWR]); TEST_ASSERT_FALSE(g.line[HTR_LINE_TX_MODE]);
    TEST_ASSERT_TRUE(g.attached[HTR_WIRING_3W]); TEST_ASSERT_TRUE(g.line[HTR_LINE_TX_EN]);
    htr_iface_init(&g.api,true); htr_iface_enable(HTR_WIRING_3W,0);
    TEST_ASSERT_TRUE(g.line[HTR_LINE_TX_PWR]);
}
void test_class_change_goes_through_safe_state(void){
    htr_iface_init(&g.api,false); htr_iface_enable(HTR_WIRING_3W,0); htr_iface_process(10);
    fake_gpio_clear_log(&g);
    htr_iface_enable(HTR_WIRING_4W,20);
    /* first event must disconnect, and 3W must be detached before 4W power-up */
    TEST_ASSERT_EQUAL_UINT8(EV_LINE,g.ev[0].kind); TEST_ASSERT_EQUAL_UINT8(HTR_LINE_TX_EN,g.ev[0].a); TEST_ASSERT_EQUAL_UINT8(0,g.ev[0].b);
    TEST_ASSERT_FALSE(g.attached[HTR_WIRING_3W]);
    TEST_ASSERT_FALSE(htr_iface_ready());
}
void test_disable_disconnects_first(void){
    htr_iface_init(&g.api,false); htr_iface_enable(HTR_WIRING_4W,0); htr_iface_process(10);
    fake_gpio_clear_log(&g); htr_iface_disable();
    TEST_ASSERT_EQUAL_UINT8(HTR_LINE_TX_EN,g.ev[0].a); TEST_ASSERT_EQUAL_UINT8(0,g.ev[0].b);
    TEST_ASSERT_FALSE(g.line[HTR_LINE_TX_PWR]); TEST_ASSERT_FALSE(g.attached[HTR_WIRING_4W]);
    TEST_ASSERT_EQUAL_UINT8(HTR_WIRING_NONE, htr_iface_wiring());
}
int main(void){ UNITY_BEGIN();
    RUN_TEST(test_init_drives_everything_safe_disconnect_first);
    RUN_TEST(test_enable_4w_powers_settles_then_attaches_and_connects);
    RUN_TEST(test_enable_3w_leaves_tx_power_off_unless_board_says);
    RUN_TEST(test_class_change_goes_through_safe_state);
    RUN_TEST(test_disable_disconnects_first);
    return UNITY_END(); }
````

Append to `Tests/CMakeLists.txt`:

```cmake
add_heater_test(test_htr_iface test_htr_iface.c ${HCORE}/htr_iface.c fakes/fake_htr_gpio.c)
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake -S Tests -B Tests/build >/dev/null && cmake --build Tests/build --target test_htr_iface`
Expected: FAIL — `htr_iface.h: No such file or directory`.

- [ ] **Step 3: Write the implementation**

`heater-coproc/core/htr_iface.h`:

````c
#ifndef HTR_IFACE_H
#define HTR_IFACE_H
/* Heater TX routing / power sequencing. All pin levels live in the board
   layer (port/<mcu>/htr_board.h): this module only speaks in semantic
   "active" terms, so a schematic change never touches core code. */
#include <stdbool.h>
#include <stdint.h>

typedef enum {
    HTR_LINE_TX_MODE = 0,   /* active = 4-wire TX path selected            */
    HTR_LINE_TX_EN,         /* active = TX path connected to the heater wire */
    HTR_LINE_TX_PWR,        /* active = TX stage supply on                  */
    HTR_LINE_COUNT
} htr_line_t;

typedef struct htr_gpio {
    void (*set_line)(void *ctx, htr_line_t line, bool active);
    /* attach=true: UART pins of that wiring to their peripheral function;
       false: plain inputs (TX released, RX readable as GPIO). */
    void (*attach_uart)(void *ctx, uint8_t wiring, bool attach);
    /* Current level of the RX comparator output for that wiring (true = high). */
    bool (*rx_level)(void *ctx, uint8_t wiring);
    void *ctx;
} htr_gpio_t;

#define HTR_IFACE_SETTLE_MS 5u

void    htr_iface_init(const htr_gpio_t *g, bool pwr_for_3w); /* safe state */
void    htr_iface_enable(uint8_t wiring, uint32_t now);       /* starts the sequence */
void    htr_iface_disable(void);
void    htr_iface_process(uint32_t now);
bool    htr_iface_ready(void);
uint8_t htr_iface_wiring(void);                               /* HTR_WIRING_NONE if off */
bool    htr_iface_rx_level(uint8_t wiring);
#endif
````

`heater-coproc/core/htr_iface.c`:

````c
#include "htr_iface.h"
#include "htr_types.h"
#include <stddef.h>

typedef enum { IF_OFF = 0, IF_SETTLING, IF_ON } if_state_t;

static const htr_gpio_t *s_g;
static bool s_pwr_for_3w;
static if_state_t s_state;
static uint8_t s_wiring;
static uint32_t s_t0;

static void line(htr_line_t l, bool active) { s_g->set_line(s_g->ctx, l, active); }

static void all_safe(void)
{
    line(HTR_LINE_TX_EN, false);       /* disconnect first */
    s_g->attach_uart(s_g->ctx, HTR_WIRING_3W, false);
    s_g->attach_uart(s_g->ctx, HTR_WIRING_4W, false);
    line(HTR_LINE_TX_PWR, false);
    line(HTR_LINE_TX_MODE, false);
}

void htr_iface_init(const htr_gpio_t *g, bool pwr_for_3w)
{
    s_g = g;
    s_pwr_for_3w = pwr_for_3w;
    s_state = IF_OFF;
    s_wiring = HTR_WIRING_NONE;
    all_safe();
}

void htr_iface_enable(uint8_t wiring, uint32_t now)
{
    if ((wiring != HTR_WIRING_3W) && (wiring != HTR_WIRING_4W)) return;
    if ((s_state != IF_OFF) && (s_wiring == wiring)) return;
    if (s_state != IF_OFF) htr_iface_disable();   /* class change goes via off */
    line(HTR_LINE_TX_EN, false);
    line(HTR_LINE_TX_MODE, wiring == HTR_WIRING_4W);
    line(HTR_LINE_TX_PWR, (wiring == HTR_WIRING_4W) || s_pwr_for_3w);
    s_wiring = wiring;
    s_t0 = now;
    s_state = IF_SETTLING;
}

void htr_iface_process(uint32_t now)
{
    if (s_state != IF_SETTLING) return;
    if ((uint32_t)(now - s_t0) < HTR_IFACE_SETTLE_MS) return;
    s_g->attach_uart(s_g->ctx, s_wiring, true);
    line(HTR_LINE_TX_EN, true);
    s_state = IF_ON;
}

void htr_iface_disable(void)
{
    if (s_g == NULL) return;
    all_safe();
    s_state = IF_OFF;
    s_wiring = HTR_WIRING_NONE;
}

bool    htr_iface_ready(void)  { return s_state == IF_ON; }
uint8_t htr_iface_wiring(void) { return s_wiring; }
bool    htr_iface_rx_level(uint8_t wiring) { return s_g->rx_level(s_g->ctx, wiring); }
````

`Tests/fakes/fake_htr_gpio.h`:

````c
#ifndef FAKE_HTR_GPIO_H
#define FAKE_HTR_GPIO_H
/* Records every htr_gpio_t call as a compact event log; RX levels are
   programmable per wiring (constant or a duty pattern). */
#include "htr_iface.h"
#include <stddef.h>

typedef enum { EV_LINE = 1, EV_ATTACH = 2 } fake_ev_kind_t;
typedef struct { uint8_t kind, a, b; } fake_ev_t;   /* LINE: a=line b=active; ATTACH: a=wiring b=attach */

#define FAKE_GPIO_MAX_EV 256u
typedef struct {
    htr_gpio_t api;
    fake_ev_t ev[FAKE_GPIO_MAX_EV];
    size_t n;
    bool line[HTR_LINE_COUNT];
    bool attached[3];
    uint8_t high_pct[3];          /* per wiring: % of samples that read high */
    uint32_t reads[3];
} fake_gpio_t;

void fake_gpio_init(fake_gpio_t *g);
void fake_gpio_clear_log(fake_gpio_t *g);
#endif
````

`Tests/fakes/fake_htr_gpio.c`:

````c
#include "fake_htr_gpio.h"
#include <string.h>

static void log_ev(fake_gpio_t *g, uint8_t k, uint8_t a, uint8_t b)
{
    if (g->n < FAKE_GPIO_MAX_EV) { g->ev[g->n].kind = k; g->ev[g->n].a = a; g->ev[g->n].b = b; g->n++; }
}
static void fg_set_line(void *ctx, htr_line_t l, bool active)
{
    fake_gpio_t *g = (fake_gpio_t *)ctx;
    g->line[l] = active;
    log_ev(g, EV_LINE, (uint8_t)l, active);
}
static void fg_attach(void *ctx, uint8_t wiring, bool attach)
{
    fake_gpio_t *g = (fake_gpio_t *)ctx;
    g->attached[wiring] = attach;
    log_ev(g, EV_ATTACH, wiring, attach);
}
/* Deterministic duty: sample k reads high iff (k*pct) crosses a 100 boundary. */
static bool fg_rx(void *ctx, uint8_t wiring)
{
    fake_gpio_t *g = (fake_gpio_t *)ctx;
    uint32_t k = g->reads[wiring]++;
    return ((k * g->high_pct[wiring]) % 100u) + g->high_pct[wiring] >= 100u;
}
void fake_gpio_init(fake_gpio_t *g)
{
    memset(g, 0, sizeof(*g));
    g->api.set_line = fg_set_line;
    g->api.attach_uart = fg_attach;
    g->api.rx_level = fg_rx;
    g->api.ctx = g;
}
void fake_gpio_clear_log(fake_gpio_t *g) { g->n = 0u; }
````

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build Tests/build --target test_htr_iface && (cd Tests/build && ctest -R '^test_htr_iface$' --output-on-failure)`
Expected: PASS (5 tests).

- [ ] **Step 5: Commit**

```bash
git add heater-coproc/core/htr_iface.[ch] Tests/fakes/fake_htr_gpio.[ch] Tests/test_htr_iface.c Tests/CMakeLists.txt
git commit -m "feat(heater-coproc): TX routing/power sequencing (htr_iface)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_017uxgfN1Y67gVKWmTWmpxxk"
```

---

### Task 7: I²C map + generic state machine with auto-detect (`htr_fsm`)

**Files:**
- Create: `heater-common/htr_i2c_map.h`, `heater-coproc/core/htr_fsm.h`, `heater-coproc/core/htr_fsm.c`, `Tests/fakes/fake_htr_driver.h`, `Tests/fakes/fake_htr_driver.c`, `Tests/test_htr_fsm.c`
- Modify: `Tests/CMakeLists.txt` (append)

**Interfaces:**
- Consumes: `htr_driver_t` (Task 4), `htr_iface_*` + `htr_gpio_t` (Task 6), `fake_gpio_t`, `fake_uart_t`.
- Produces: everything in `htr_i2c_map.h` (register addresses `HTR_R_*`, `HTR_REG_COUNT 0x46`, `HTR_STATUS_FIRST`, `HTR_STATUS_LEN`, `HTR_F_*`, `HTR_LS_*`, `HTR_DET_*`, `HTR_CMD_*`, `HTR_RES_*`, `HTR_I2C_ADDR7`, `HTR_DEVICE_ID`, `HTR_MAP_VERSION`, `HTR_HW_ID_*`); `htr_fsm_cfg_t`, `htr_fsm_view_t`; `htr_fsm_init(const htr_fsm_cfg_t*, uint8_t type_setting, uint32_t now)`, `htr_fsm_process(uint32_t)`, `htr_fsm_set_request(const htr_request_t*)`, `uint8_t htr_fsm_set_type(uint8_t, uint32_t)`, `uint8_t htr_fsm_clear_fault(uint32_t)`, `uint8_t htr_fsm_redetect(uint32_t)`, `uint8_t htr_fsm_reinit_iface(uint32_t)`, `htr_fsm_view(uint32_t, htr_fsm_view_t*)`; timing macros `HTR_BOOT_QUIET_MS 3000`, `HTR_SENSE_MS 1000`, `HTR_SENSE_SAMPLE_MS 10`, `HTR_SENSE_HIGH_PCT 90`, `HTR_SENSE_LOW_PCT 10`, `HTR_DETECT_RETRY_MS 10000`, `HTR_FRESH_MS 3000`, `HTR_HOTSWAP_MS 30000`, `HTR_START_TIMEOUT_MS 600000`, `HTR_COOLDOWN_TIMEOUT_MS 900000`; `fake_drv[3]`, `fake_ctx[3]`, `fake_drivers_reset()`.

Rules implemented (spec §4): boot TX-quiet 3 s; passive sense 1 s (≥90 % high = idle-high, ≤10 % = idle-low; B high → 4W; A high & B low → 3W; both low → none; else ambiguous → 3W-safe probes only); probes drivers of the class in registry order; NO_HEATER/UNKNOWN retry every 10 s; forced type skips sense/probe; hot-swap only in OFF with AUTO after 30 s stale; FAULT on vendor fault, start timeout, cooldown timeout, or the heater stopping on its own while RUNNING; FAULT clears run, requires CLEAR_FAULT (heater fresh in standby) + new request.

- [ ] **Step 1: Write the failing test**

`Tests/test_htr_fsm.c`:

````c
#include "htr_fsm.h"
#include "htr_i2c_map.h"
#include "fake_htr_driver.h"
#include "fake_htr_gpio.h"
#include "fake_htr_uart.h"
#include "unity.h"
#include <string.h>

static fake_gpio_t g; static fake_uart_t u3, u4;
static const htr_driver_t *const regs3[] = { &fake_drv[0], &fake_drv[1], &fake_drv[2] };
static htr_fsm_cfg_t cfg;
static uint32_t now;

static void tick_to(uint32_t t){ while((int32_t)(t - now) > 0){ htr_fsm_process(now); now += 10u; } htr_fsm_process(now); }
static htr_fsm_view_t view(void){ htr_fsm_view_t v; htr_fsm_view(now,&v); return v; }
static void req(uint8_t run){ htr_request_t r={run,5,22,HTR_AMBIENT_UNKNOWN}; htr_fsm_set_request(&r); }

void setUp(void){
    fake_drivers_reset(); fake_gpio_init(&g); fake_uart_init(&u3); fake_uart_init(&u4);
    cfg.drivers = regs3; cfg.n_drivers = 3; cfg.gpio = &g.api; cfg.pwr_for_3w = false;
    cfg.uart_3w = &u3.api; cfg.uart_4w = &u4.api; now = 0;
}
void tearDown(void){}

static void boot_auto(uint8_t pct_a, uint8_t pct_b){
    g.high_pct[HTR_WIRING_3W]=pct_a; g.high_pct[HTR_WIRING_4W]=pct_b;
    htr_fsm_init(&cfg, HTR_TYPE_AUTO, now);
}

void test_boot_is_tx_quiet_and_iface_off_for_3s(void){
    boot_auto(100,0); fake_ctx[0].probe_answer=HTR_PROBE_FOUND;
    tick_to(2990);
    TEST_ASSERT_EQUAL_UINT8(HTR_PHASE_DETECTING, view().phase);
    TEST_ASSERT_EQUAL(0u, fake_ctx[0].inits + fake_ctx[1].inits);
    TEST_ASSERT_FALSE(g.line[HTR_LINE_TX_EN]); TEST_ASSERT_FALSE(g.line[HTR_LINE_TX_PWR]);
    TEST_ASSERT_EQUAL(0u, g.reads[HTR_WIRING_3W]);              /* not even sensing */
}
void test_3w_signature_probes_only_3w_drivers_in_order(void){
    boot_auto(100,0); fake_ctx[0].probe_answer=HTR_PROBE_ABSENT; fake_ctx[2].probe_answer=HTR_PROBE_FOUND;
    tick_to(6000);
    htr_fsm_view_t v=view();
    TEST_ASSERT_EQUAL_UINT8(HTR_PHASE_OFF, v.phase);
    TEST_ASSERT_EQUAL_UINT8(3u, v.active_type);
    TEST_ASSERT_EQUAL_UINT8(HTR_DET_FOUND, v.detect_result);
    TEST_ASSERT_EQUAL_HEX8(HTR_LS_A_IDLE_HIGH | (HTR_LS_CLASS_3W<<HTR_LS_CLASS_SHIFT), v.line_sense);
    TEST_ASSERT_EQUAL(0u, fake_ctx[1].inits);                 /* 4W driver never touched */
    TEST_ASSERT_TRUE(fake_ctx[0].probe_calls > 0);
    TEST_ASSERT_TRUE(fake_ctx[2].uart == &u3.api);
    TEST_ASSERT_TRUE(v.flags & HTR_F_DETECT_DONE); TEST_ASSERT_TRUE(v.flags & HTR_F_PRESENT);
}
void test_4w_signature_selects_4w_driver_and_powers_4w_path(void){
    boot_auto(100,100); fake_ctx[1].probe_answer=HTR_PROBE_FOUND;
    tick_to(6000);
    TEST_ASSERT_EQUAL_UINT8(2u, view().active_type);
    TEST_ASSERT_EQUAL(0u, fake_ctx[0].inits + fake_ctx[2].inits);
    TEST_ASSERT_TRUE(g.line[HTR_LINE_TX_MODE]); TEST_ASSERT_TRUE(g.line[HTR_LINE_TX_PWR]);
    TEST_ASSERT_TRUE(fake_ctx[1].uart == &u4.api);
}
void test_push_pull_4w_never_enabled_without_4w_signature(void){
    boot_auto(50,40);      /* ambiguous lines */
    fake_ctx[1].probe_answer=HTR_PROBE_FOUND;     /* a 4W driver would answer if asked */
    tick_to(30000);
    for(size_t i=0;i<g.n;i++)
        if(g.ev[i].kind==EV_LINE && g.ev[i].a==HTR_LINE_TX_MODE) TEST_ASSERT_EQUAL_UINT8(0,g.ev[i].b);
    TEST_ASSERT_EQUAL(0u, fake_ctx[1].inits);
    htr_fsm_view_t v=view();
    TEST_ASSERT_EQUAL_UINT8(HTR_LS_CLASS_AMBIG, v.line_sense >> HTR_LS_CLASS_SHIFT);
    TEST_ASSERT_EQUAL_UINT8(HTR_DET_UNKNOWN, v.detect_result);
    TEST_ASSERT_EQUAL_UINT8(HTR_PHASE_DETECTING, v.phase);
}
void test_no_heater_retries_every_10s(void){
    boot_auto(0,0); tick_to(5000);
    TEST_ASSERT_EQUAL_UINT8(HTR_DET_NO_HEATER, view().detect_result);
    uint32_t r1=g.reads[HTR_WIRING_3W]; tick_to(14000);
    TEST_ASSERT_EQUAL(r1, g.reads[HTR_WIRING_3W]);          /* backing off */
    tick_to(16000);
    TEST_ASSERT_TRUE(g.reads[HTR_WIRING_3W] > r1);          /* sensing again */
    g.high_pct[HTR_WIRING_3W]=100; fake_ctx[0].probe_answer=HTR_PROBE_FOUND; tick_to(30000);
    TEST_ASSERT_EQUAL_UINT8(1u, view().active_type);        /* hot-plugged */
}
static void boot_found_3w(void){
    boot_auto(100,0); fake_ctx[0].probe_answer=HTR_PROBE_FOUND; tick_to(6000);
    TEST_ASSERT_EQUAL_UINT8(HTR_PHASE_OFF, view().phase);
}
void test_full_cycle_start_run_stop_cooldown_off(void){
    boot_found_3w();
    req(1); tick_to(6100);
    TEST_ASSERT_EQUAL_UINT8(HTR_PHASE_STARTING, view().phase);
    TEST_ASSERT_EQUAL(1u, fake_ctx[0].starts); TEST_ASSERT_EQUAL_UINT8(5, fake_ctx[0].last_rq.level);
    fake_ctx[0].hint=HTR_HINT_STARTING; tick_to(7000);
    TEST_ASSERT_EQUAL_UINT8(HTR_PHASE_STARTING, view().phase);
    fake_ctx[0].hint=HTR_HINT_RUNNING; tick_to(8000);
    TEST_ASSERT_EQUAL_UINT8(HTR_PHASE_RUNNING, view().phase);
    req(0); tick_to(8100);
    TEST_ASSERT_EQUAL_UINT8(HTR_PHASE_STOP_REQUESTED, view().phase);
    TEST_ASSERT_EQUAL(1u, fake_ctx[0].stops);
    fake_ctx[0].hint=HTR_HINT_COOLDOWN; tick_to(9000);
    TEST_ASSERT_EQUAL_UINT8(HTR_PHASE_COOLDOWN, view().phase);
    TEST_ASSERT_FALSE(view().flags & HTR_F_SAFE_OFF);
    fake_ctx[0].hint=HTR_HINT_OFF; tick_to(10000);
    htr_fsm_view_t v=view();
    TEST_ASSERT_EQUAL_UINT8(HTR_PHASE_OFF, v.phase);
    TEST_ASSERT_TRUE(v.flags & HTR_F_SAFE_OFF);
    TEST_ASSERT_TRUE(g.line[HTR_LINE_TX_EN]);               /* interface never dropped */
}
void test_start_timeout_faults_and_stops_without_cutting_power(void){
    boot_found_3w(); req(1); fake_ctx[0].hint=HTR_HINT_STARTING;
    tick_to(6000 + HTR_START_TIMEOUT_MS + 100);
    htr_fsm_view_t v=view();
    TEST_ASSERT_EQUAL_UINT8(HTR_PHASE_FAULT, v.phase);
    TEST_ASSERT_TRUE(v.flags & HTR_F_FAULT_LATCHED);
    TEST_ASSERT_EQUAL(1u, fake_ctx[0].stops);
    TEST_ASSERT_EQUAL_UINT8(0, v.rq.run);
    TEST_ASSERT_TRUE(g.line[HTR_LINE_TX_EN]);
}
void test_no_autonomous_restart_after_fault(void){
    boot_found_3w(); req(1); fake_ctx[0].hint=HTR_HINT_FAULT; tick_to(7000);
    TEST_ASSERT_EQUAL_UINT8(HTR_PHASE_FAULT, view().phase);
    req(1); tick_to(9000);                                  /* G0B1 keeps asserting run */
    TEST_ASSERT_EQUAL_UINT8(HTR_PHASE_FAULT, view().phase);
    TEST_ASSERT_EQUAL(1u, fake_ctx[0].starts);
    TEST_ASSERT_EQUAL_UINT8(HTR_RES_BUSY, htr_fsm_clear_fault(now)); /* heater not in standby */
    fake_ctx[0].hint=HTR_HINT_OFF; tick_to(9500);
    TEST_ASSERT_EQUAL_UINT8(HTR_RES_OK, htr_fsm_clear_fault(now));
    TEST_ASSERT_EQUAL_UINT8(HTR_PHASE_OFF, view().phase);
    TEST_ASSERT_EQUAL(1u, fake_ctx[0].starts);              /* still needs a new request */
    req(1); tick_to(9600);
    TEST_ASSERT_EQUAL(2u, fake_ctx[0].starts);
}
void test_unexpected_heater_stop_while_running_is_a_fault(void){
    boot_found_3w(); req(1); fake_ctx[0].hint=HTR_HINT_RUNNING; tick_to(7000);
    fake_ctx[0].hint=HTR_HINT_COOLDOWN; tick_to(8000);
    TEST_ASSERT_EQUAL_UINT8(HTR_PHASE_FAULT, view().phase);
}
void test_comms_loss_is_flag_only(void){
    boot_found_3w(); req(1); fake_ctx[0].hint=HTR_HINT_RUNNING; tick_to(7000);
    fake_ctx[0].alive=false; tick_to(7000 + 60000);
    htr_fsm_view_t v=view();
    TEST_ASSERT_EQUAL_UINT8(HTR_PHASE_RUNNING, v.phase);
    TEST_ASSERT_TRUE(v.flags & HTR_F_COMMS_FAULT);
    TEST_ASSERT_FALSE(v.flags & HTR_F_FRESH);
    TEST_ASSERT_EQUAL(0u, fake_ctx[0].stops);
}
void test_cooldown_timeout_faults_but_keeps_polling(void){
    boot_found_3w(); req(1); fake_ctx[0].hint=HTR_HINT_RUNNING; tick_to(7000);
    req(0); fake_ctx[0].hint=HTR_HINT_COOLDOWN; tick_to(8000);
    unsigned p=fake_ctx[0].processes;
    tick_to(8000 + HTR_COOLDOWN_TIMEOUT_MS + 100);
    TEST_ASSERT_EQUAL_UINT8(HTR_PHASE_FAULT, view().phase);
    tick_to(now + 1000);
    TEST_ASSERT_TRUE(fake_ctx[0].processes > p + 100);
}
void test_live_level_change_applies_to_driver(void){
    boot_found_3w(); req(1); tick_to(6100);
    unsigned a0 = fake_ctx[0].applies;
    htr_request_t r={1,9,22,HTR_AMBIENT_UNKNOWN}; htr_fsm_set_request(&r);
    TEST_ASSERT_EQUAL(a0 + 1u, fake_ctx[0].applies); TEST_ASSERT_EQUAL_UINT8(9, fake_ctx[0].last_rq.level);
    htr_fsm_set_request(&r);                                /* idempotent rewrite */
    TEST_ASSERT_EQUAL(a0 + 1u, fake_ctx[0].applies);
}
void test_hotswap_redetects_only_while_off(void){
    boot_found_3w(); req(1); fake_ctx[0].hint=HTR_HINT_RUNNING; tick_to(7000);
    fake_ctx[0].alive=false; tick_to(7000 + HTR_HOTSWAP_MS + 5000);
    TEST_ASSERT_EQUAL_UINT8(HTR_PHASE_RUNNING, view().phase);  /* never re-probe a maybe-running heater */
    TEST_ASSERT_EQUAL(1u, fake_ctx[0].inits);
    setUp(); boot_found_3w(); fake_ctx[0].alive=false;
    tick_to(6000 + HTR_FRESH_MS + HTR_HOTSWAP_MS + 100);   /* stale = past the fresh window */
    TEST_ASSERT_EQUAL_UINT8(HTR_PHASE_DETECTING, view().phase);
}
void test_forced_type_skips_sense_and_probe(void){
    g.high_pct[HTR_WIRING_3W]=0; g.high_pct[HTR_WIRING_4W]=0;
    htr_fsm_init(&cfg, 2u, now); tick_to(3500);
    htr_fsm_view_t v=view();
    TEST_ASSERT_EQUAL_UINT8(HTR_PHASE_OFF, v.phase);
    TEST_ASSERT_EQUAL_UINT8(2u, v.active_type);
    TEST_ASSERT_EQUAL(0u, fake_ctx[1].probe_calls);
    TEST_ASSERT_EQUAL(0u, g.reads[HTR_WIRING_3W] + g.reads[HTR_WIRING_4W]);
}
void test_type_change_rejected_while_running(void){
    boot_found_3w(); req(1); tick_to(6100);
    TEST_ASSERT_EQUAL_UINT8(HTR_RES_BUSY, htr_fsm_set_type(2u, now));
    TEST_ASSERT_EQUAL_UINT8(HTR_RES_BUSY, htr_fsm_redetect(now));
    TEST_ASSERT_EQUAL_UINT8(HTR_RES_NOT_SUPPORTED, htr_fsm_set_type(9u, now));
}
/* HAL tick wraps after ~49.7 days: every timeout must use unsigned deltas. */
void test_full_cycle_across_tick_wrap(void){
    now = 0xFFFFF000u;                                /* ~4 s before wrap */
    boot_auto(100,0); fake_ctx[0].probe_answer=HTR_PROBE_FOUND;
    tick_to(0xFFFFF000u + 6000u);                    /* crosses 0 during detection */
    TEST_ASSERT_TRUE(now < 0x10000u);
    TEST_ASSERT_EQUAL_UINT8(HTR_PHASE_OFF, view().phase);
    req(1); fake_ctx[0].hint=HTR_HINT_RUNNING; tick_to(now + 1000u);
    TEST_ASSERT_EQUAL_UINT8(HTR_PHASE_RUNNING, view().phase);
    TEST_ASSERT_TRUE(view().flags & HTR_F_FRESH);
}
int main(void){ UNITY_BEGIN();
    RUN_TEST(test_full_cycle_across_tick_wrap);
    RUN_TEST(test_boot_is_tx_quiet_and_iface_off_for_3s);
    RUN_TEST(test_3w_signature_probes_only_3w_drivers_in_order);
    RUN_TEST(test_4w_signature_selects_4w_driver_and_powers_4w_path);
    RUN_TEST(test_push_pull_4w_never_enabled_without_4w_signature);
    RUN_TEST(test_no_heater_retries_every_10s);
    RUN_TEST(test_full_cycle_start_run_stop_cooldown_off);
    RUN_TEST(test_start_timeout_faults_and_stops_without_cutting_power);
    RUN_TEST(test_no_autonomous_restart_after_fault);
    RUN_TEST(test_unexpected_heater_stop_while_running_is_a_fault);
    RUN_TEST(test_comms_loss_is_flag_only);
    RUN_TEST(test_cooldown_timeout_faults_but_keeps_polling);
    RUN_TEST(test_live_level_change_applies_to_driver);
    RUN_TEST(test_hotswap_redetects_only_while_off);
    RUN_TEST(test_forced_type_skips_sense_and_probe);
    RUN_TEST(test_type_change_rejected_while_running);
    return UNITY_END(); }
````

Append to `Tests/CMakeLists.txt`:

```cmake
add_heater_test(test_htr_fsm test_htr_fsm.c ${HCORE}/htr_fsm.c ${HCORE}/htr_iface.c fakes/fake_htr_driver.c fakes/fake_htr_gpio.c fakes/fake_htr_uart.c)
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake -S Tests -B Tests/build >/dev/null && cmake --build Tests/build --target test_htr_fsm`
Expected: FAIL — `htr_fsm.h: No such file or directory`.

- [ ] **Step 3: Write the implementation**

`heater-common/htr_i2c_map.h`:

````c
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
````

`heater-coproc/core/htr_fsm.h`:

````c
#ifndef HTR_FSM_H
#define HTR_FSM_H
/* Generic heater state machine + auto-detection (spec section 4). Owns the
   active driver and the interface; knows nothing about I2C or registers. */
#include "htr_driver.h"
#include "htr_iface.h"
#include "htr_types.h"

#define HTR_BOOT_QUIET_MS      3000u
#define HTR_SENSE_MS           1000u
#define HTR_SENSE_SAMPLE_MS      10u
#define HTR_SENSE_HIGH_PCT       90u   /* >= this % high = idle-high      */
#define HTR_SENSE_LOW_PCT        10u   /* <= this % high = idle-low       */
#define HTR_DETECT_RETRY_MS   10000u
#define HTR_FRESH_MS           3000u
#define HTR_HOTSWAP_MS        30000u
#define HTR_START_TIMEOUT_MS  (10u * 60u * 1000u)
#define HTR_COOLDOWN_TIMEOUT_MS (15u * 60u * 1000u)

typedef struct {
    const htr_driver_t *const *drivers;
    uint8_t n_drivers;
    const htr_gpio_t *gpio;
    bool pwr_for_3w;
    const htr_uart_t *uart_3w;
    const htr_uart_t *uart_4w;
} htr_fsm_cfg_t;

typedef struct {
    uint8_t  phase;          /* htr_phase_t */
    uint8_t  active_type;    /* HTR_TYPE_* or NONE */
    uint8_t  line_sense;     /* HTR_LS_* */
    uint8_t  detect_result;  /* HTR_DET_* */
    uint16_t flags;          /* HTR_F_* owned by the FSM (not HOST_TIMEOUT/ECHO) */
    uint16_t caps;
    htr_request_t rq;        /* effective request (run is forced 0 in FAULT) */
    htr_status_t st;
} htr_fsm_view_t;

/* type_setting: HTR_TYPE_AUTO, HTR_TYPE_NONE, or a forced type id. */
void htr_fsm_init(const htr_fsm_cfg_t *cfg, uint8_t type_setting, uint32_t now);
void htr_fsm_process(uint32_t now);
/* Level-triggered: call with the full request whenever any field changes. */
void htr_fsm_set_request(const htr_request_t *rq);
/* Return HTR_RES_* codes (htr_i2c_map.h). */
uint8_t htr_fsm_set_type(uint8_t type_setting, uint32_t now);
uint8_t htr_fsm_clear_fault(uint32_t now);
uint8_t htr_fsm_redetect(uint32_t now);
uint8_t htr_fsm_reinit_iface(uint32_t now);
void htr_fsm_view(uint32_t now, htr_fsm_view_t *out);
#endif
````

`heater-coproc/core/htr_fsm.c`:

````c
#include "htr_fsm.h"
#include "htr_i2c_map.h"
#include <string.h>

typedef enum {
    DET_QUIET = 0,   /* boot TX-quiet                          */
    DET_SENSE,       /* passive line sense, iface off          */
    DET_ENABLE,      /* iface powering for the probe class     */
    DET_PROBE,       /* probing drivers of that class          */
    DET_WAIT,        /* back-off before the next attempt       */
    DET_DONE
} det_t;

static htr_fsm_cfg_t s_cfg;
static uint8_t  s_type_setting;
static uint8_t  s_phase;
static const htr_driver_t *s_drv;      /* active driver (NULL until found) */
static htr_request_t s_rq;
static uint32_t s_phase_tick;
static bool     s_fault_latched;
/* detection */
static det_t    s_det;
static uint32_t s_det_tick;
static uint32_t s_last_sample;
static uint16_t s_samples, s_high_a, s_high_b;
static uint8_t  s_line_sense;
static uint8_t  s_det_result;
static uint8_t  s_probe_wiring;
static uint8_t  s_probe_idx;
static bool     s_probe_inited;
static uint32_t s_stale_since;

static const htr_uart_t *uart_for(uint8_t wiring)
{
    return (wiring == HTR_WIRING_4W) ? s_cfg.uart_4w : s_cfg.uart_3w;
}

static const htr_driver_t *driver_for_type(uint8_t type)
{
    uint8_t i;
    for (i = 0u; i < s_cfg.n_drivers; ++i) {
        if (s_cfg.drivers[i]->type_id == type) return s_cfg.drivers[i];
    }
    return NULL;
}

static void set_phase(uint8_t p, uint32_t now)
{
    s_phase = p;
    s_phase_tick = now;
}

static void begin_sense(uint32_t now)
{
    htr_iface_disable();
    s_drv = NULL;
    s_det = DET_SENSE;
    s_det_tick = now;
    s_last_sample = now - HTR_SENSE_SAMPLE_MS;
    s_samples = s_high_a = s_high_b = 0u;
    s_det_result = HTR_DET_RUNNING;
    s_stale_since = now;
    set_phase(HTR_PHASE_DETECTING, now);
}

static void start_detection(uint32_t now, bool boot)
{
    if (s_type_setting == HTR_TYPE_NONE) {
        htr_iface_disable();
        s_drv = NULL;
        s_det = DET_DONE;
        s_det_result = HTR_DET_IDLE;
        set_phase(HTR_PHASE_OFF, now);
        return;
    }
    if (s_type_setting != HTR_TYPE_AUTO) {
        /* Forced type (bench override): no sensing, no probing. */
        s_drv = driver_for_type(s_type_setting);
        htr_iface_disable();
        if (s_drv == NULL) {
            s_det = DET_DONE;
            s_det_result = HTR_DET_UNKNOWN;
            set_phase(HTR_PHASE_OFF, now);
            return;
        }
        s_probe_wiring = s_drv->wiring;
        s_det = boot ? DET_QUIET : DET_ENABLE;
        s_det_tick = now;
        if (!boot) htr_iface_enable(s_probe_wiring, now);
        set_phase(HTR_PHASE_DETECTING, now);
        return;
    }
    if (boot) {
        htr_iface_disable();
        s_drv = NULL;
        s_det = DET_QUIET;
        s_det_tick = now;
        s_det_result = HTR_DET_RUNNING;
        set_phase(HTR_PHASE_DETECTING, now);
    } else {
        begin_sense(now);
    }
}

/* Next driver index >= from with the probe wiring; n_drivers if none. */
static uint8_t next_probe(uint8_t from)
{
    while ((from < s_cfg.n_drivers) && (s_cfg.drivers[from]->wiring != s_probe_wiring)) from++;
    return from;
}

static void det_fail(uint8_t result, uint32_t now)
{
    htr_iface_disable();
    s_drv = NULL;
    s_det_result = result;
    s_det = DET_WAIT;
    s_det_tick = now;
}

static void found(const htr_driver_t *d, uint32_t now)
{
    s_drv = d;
    s_det = DET_DONE;
    s_det_result = HTR_DET_FOUND;
    s_stale_since = now;
    set_phase(HTR_PHASE_OFF, now);
}

static void detect_step(uint32_t now)
{
    uint8_t cls;
    switch (s_det) {
    case DET_QUIET:
        if ((uint32_t)(now - s_det_tick) < HTR_BOOT_QUIET_MS) return;
        if (s_type_setting == HTR_TYPE_AUTO) {
            begin_sense(now);
        } else {
            s_det = DET_ENABLE;
            s_det_tick = now;
            htr_iface_enable(s_probe_wiring, now);
        }
        return;
    case DET_SENSE:
        if ((uint32_t)(now - s_last_sample) >= HTR_SENSE_SAMPLE_MS) {
            s_last_sample = now;
            s_samples++;
            if (htr_iface_rx_level(HTR_WIRING_3W)) s_high_a++;
            if (htr_iface_rx_level(HTR_WIRING_4W)) s_high_b++;
        }
        if ((uint32_t)(now - s_det_tick) < HTR_SENSE_MS) return;
        {
            uint32_t pa = (100u * s_high_a) / s_samples;
            uint32_t pb = (100u * s_high_b) / s_samples;
            bool a_hi = pa >= HTR_SENSE_HIGH_PCT, b_hi = pb >= HTR_SENSE_HIGH_PCT;
            bool a_lo = pa <= HTR_SENSE_LOW_PCT,  b_lo = pb <= HTR_SENSE_LOW_PCT;
            if (b_hi) cls = HTR_LS_CLASS_4W;
            else if (a_hi && b_lo) cls = HTR_LS_CLASS_3W;
            else if (a_lo && b_lo) cls = HTR_LS_CLASS_NONE;
            else cls = HTR_LS_CLASS_AMBIG;
            s_line_sense = (uint8_t)((a_hi ? HTR_LS_A_IDLE_HIGH : 0u) |
                                     (b_hi ? HTR_LS_B_IDLE_HIGH : 0u) |
                                     (cls << HTR_LS_CLASS_SHIFT));
        }
        if (cls == HTR_LS_CLASS_NONE) { det_fail(HTR_DET_NO_HEATER, now); return; }
        /* Safety: the 4-wire push-pull driver only with the 4-wire signature;
           ambiguous lines get only the open-drain 3-wire probes. */
        s_probe_wiring = (cls == HTR_LS_CLASS_4W) ? HTR_WIRING_4W : HTR_WIRING_3W;
        s_probe_idx = next_probe(0u);
        if (s_probe_idx >= s_cfg.n_drivers) { det_fail(HTR_DET_UNKNOWN, now); return; }
        htr_iface_enable(s_probe_wiring, now);
        s_det = DET_ENABLE;
        s_det_tick = now;
        return;
    case DET_ENABLE:
        if (!htr_iface_ready()) return;
        if (s_type_setting != HTR_TYPE_AUTO) {
            s_drv->init(s_drv->ctx, uart_for(s_drv->wiring), now);
            found(s_drv, now);
            return;
        }
        s_det = DET_PROBE;
        s_probe_inited = false;
        return;
    case DET_PROBE: {
        const htr_driver_t *d = s_cfg.drivers[s_probe_idx];
        htr_probe_t r;
        if (!s_probe_inited) {
            d->init(d->ctx, uart_for(d->wiring), now);
            s_probe_inited = true;
        }
        r = d->probe(d->ctx, now);
        if (r == HTR_PROBE_FOUND) { found(d, now); return; }
        if (r == HTR_PROBE_ABSENT) {
            s_probe_idx = next_probe((uint8_t)(s_probe_idx + 1u));
            s_probe_inited = false;
            if (s_probe_idx >= s_cfg.n_drivers) det_fail(HTR_DET_UNKNOWN, now);
        }
        return;
    }
    case DET_WAIT:
        if ((uint32_t)(now - s_det_tick) >= HTR_DETECT_RETRY_MS) begin_sense(now);
        return;
    default:
        return;
    }
}

static bool fresh_now(const htr_status_t *st, uint32_t now)
{
    return st->any_rx && ((uint32_t)(now - st->last_rx_tick) <= HTR_FRESH_MS);
}

static void enter_fault(uint32_t now)
{
    s_fault_latched = true;
    s_rq.run = 0u;
    s_drv->stop(s_drv->ctx);
    set_phase(HTR_PHASE_FAULT, now);
}

static void run_step(uint32_t now)
{
    htr_status_t st;
    bool fresh;
    uint8_t h;
    s_drv->process(s_drv->ctx, now);
    s_drv->status(s_drv->ctx, now, &st);
    fresh = fresh_now(&st, now);
    h = fresh ? st.hint : (uint8_t)HTR_HINT_UNKNOWN;
    if (fresh) s_stale_since = now;

    switch (s_phase) {
    case HTR_PHASE_OFF:
        if (s_rq.run) {
            s_drv->start(s_drv->ctx, &s_rq);
            set_phase(HTR_PHASE_STARTING, now);
        } else if ((s_type_setting == HTR_TYPE_AUTO) &&
                   ((uint32_t)(now - s_stale_since) >= HTR_HOTSWAP_MS)) {
            begin_sense(now);                     /* hot-swap: only while OFF */
        }
        return;
    case HTR_PHASE_STARTING:
        if (!s_rq.run) { s_drv->stop(s_drv->ctx); set_phase(HTR_PHASE_STOP_REQUESTED, now); return; }
        if (h == HTR_HINT_FAULT) { enter_fault(now); return; }
        if (h == HTR_HINT_RUNNING) { set_phase(HTR_PHASE_RUNNING, now); return; }
        if ((uint32_t)(now - s_phase_tick) >= HTR_START_TIMEOUT_MS) enter_fault(now);
        return;
    case HTR_PHASE_RUNNING:
        if (!s_rq.run) { s_drv->stop(s_drv->ctx); set_phase(HTR_PHASE_STOP_REQUESTED, now); return; }
        /* Fault code, or the heater stopping on its own while we want heat. */
        if ((h == HTR_HINT_FAULT) || (h == HTR_HINT_COOLDOWN) || (h == HTR_HINT_OFF)) enter_fault(now);
        return;
    case HTR_PHASE_STOP_REQUESTED:
        if (h == HTR_HINT_COOLDOWN) { set_phase(HTR_PHASE_COOLDOWN, now); return; }
        if (h == HTR_HINT_OFF) { set_phase(HTR_PHASE_OFF, now); return; }
        if ((uint32_t)(now - s_phase_tick) >= HTR_COOLDOWN_TIMEOUT_MS) enter_fault(now);
        return;
    case HTR_PHASE_COOLDOWN:
        if (h == HTR_HINT_OFF) { set_phase(HTR_PHASE_OFF, now); return; }
        if ((uint32_t)(now - s_phase_tick) >= HTR_COOLDOWN_TIMEOUT_MS) enter_fault(now);
        return;
    default:  /* FAULT: keep talking (stop already requested), wait for CLEAR_FAULT */
        return;
    }
}

void htr_fsm_init(const htr_fsm_cfg_t *cfg, uint8_t type_setting, uint32_t now)
{
    s_cfg = *cfg;
    s_type_setting = type_setting;
    memset(&s_rq, 0, sizeof(s_rq));
    s_rq.level = 1u;
    s_rq.setpoint_c = 20;
    s_rq.ambient_c_x10 = HTR_AMBIENT_UNKNOWN;
    s_fault_latched = false;
    s_line_sense = 0u;
    s_drv = NULL;
    htr_iface_init(cfg->gpio, cfg->pwr_for_3w);
    start_detection(now, true);
}

void htr_fsm_process(uint32_t now)
{
    htr_iface_process(now);
    if (s_phase == HTR_PHASE_DETECTING) { detect_step(now); return; }
    if (s_drv != NULL) run_step(now);
}

void htr_fsm_set_request(const htr_request_t *rq)
{
    bool live = (s_rq.level != rq->level) || (s_rq.setpoint_c != rq->setpoint_c) ||
                (s_rq.ambient_c_x10 != rq->ambient_c_x10);
    s_rq.level = rq->level;
    s_rq.setpoint_c = rq->setpoint_c;
    s_rq.ambient_c_x10 = rq->ambient_c_x10;
    /* FAULT holds run at 0 until CLEAR_FAULT (no autonomous restart). */
    s_rq.run = (s_phase == HTR_PHASE_FAULT) ? 0u : (rq->run ? 1u : 0u);
    if (live && (s_drv != NULL) && (s_phase != HTR_PHASE_DETECTING)) s_drv->apply(s_drv->ctx, &s_rq);
}

static bool idle_phase(void)
{
    return (s_phase == HTR_PHASE_OFF) || (s_phase == HTR_PHASE_DETECTING) ||
           (s_phase == HTR_PHASE_FAULT);
}

uint8_t htr_fsm_set_type(uint8_t type_setting, uint32_t now)
{
    if ((type_setting != HTR_TYPE_AUTO) && (type_setting != HTR_TYPE_NONE) &&
        (driver_for_type(type_setting) == NULL)) return HTR_RES_NOT_SUPPORTED;
    if (!idle_phase() || s_rq.run) return HTR_RES_BUSY;
    s_type_setting = type_setting;
    s_fault_latched = false;
    start_detection(now, false);
    return HTR_RES_OK;
}

uint8_t htr_fsm_clear_fault(uint32_t now)
{
    htr_status_t st;
    if (s_phase != HTR_PHASE_FAULT) return HTR_RES_OK;
    s_drv->status(s_drv->ctx, now, &st);
    if (!fresh_now(&st, now) || (st.hint != HTR_HINT_OFF)) return HTR_RES_BUSY;
    s_fault_latched = false;
    set_phase(HTR_PHASE_OFF, now);
    return HTR_RES_OK;
}

uint8_t htr_fsm_redetect(uint32_t now)
{
    if (!idle_phase() || s_rq.run || (s_phase == HTR_PHASE_FAULT)) return HTR_RES_BUSY;
    if (s_type_setting == HTR_TYPE_NONE) return HTR_RES_NO_TYPE;
    start_detection(now, false);
    return HTR_RES_OK;
}

uint8_t htr_fsm_reinit_iface(uint32_t now)
{
    return htr_fsm_redetect(now);   /* re-runs the interface bring-up + driver init */
}

void htr_fsm_view(uint32_t now, htr_fsm_view_t *out)
{
    bool fresh;
    memset(out, 0, sizeof(*out));
    out->phase = s_phase;
    out->line_sense = s_line_sense;
    out->detect_result = s_det_result;
    out->rq = s_rq;
    if ((s_drv != NULL) && (s_phase != HTR_PHASE_DETECTING)) {
        out->active_type = s_drv->type_id;
        out->caps = s_drv->caps;
        s_drv->status(s_drv->ctx, now, &out->st);
        fresh = fresh_now(&out->st, now);
        if (out->st.any_rx) out->flags |= HTR_F_PRESENT;
        if (fresh) out->flags |= HTR_F_FRESH;
        if (fresh && (out->st.hint == HTR_HINT_OFF)) out->flags |= HTR_F_SAFE_OFF;
        if (s_rq.run && !fresh) out->flags |= HTR_F_COMMS_FAULT;
        out->flags |= HTR_F_DETECT_DONE;
    }
    if (htr_iface_ready()) out->flags |= HTR_F_IFACE_POWERED;
    if (s_fault_latched) out->flags |= HTR_F_FAULT_LATCHED;
}
````

`Tests/fakes/fake_htr_driver.h`:

````c
#ifndef FAKE_HTR_DRIVER_H
#define FAKE_HTR_DRIVER_H
/* Scriptable heater drivers for FSM tests. fake_drv[0]: type 1, 3-wire;
   fake_drv[1]: type 2, 4-wire; fake_drv[2]: type 3, 3-wire. */
#include "htr_driver.h"

typedef struct {
    const htr_uart_t *uart;
    htr_probe_t probe_answer;    /* returned once probe_calls >= probe_after */
    unsigned probe_after;
    unsigned probe_calls, inits, starts, stops, applies, processes;
    bool alive;                  /* process()/probe() refresh last_rx when true */
    uint8_t hint;
    uint32_t last_rx;
    bool any_rx;
    htr_request_t last_rq;
} fake_drv_ctx_t;

extern fake_drv_ctx_t fake_ctx[3];
extern const htr_driver_t fake_drv[3];
void fake_drivers_reset(void);
#endif
````

`Tests/fakes/fake_htr_driver.c`:

````c
#include "fake_htr_driver.h"
#include <string.h>

fake_drv_ctx_t fake_ctx[3];

static void touch(fake_drv_ctx_t *c, uint32_t now) { if (c->alive) { c->last_rx = now; c->any_rx = true; } }
static void f_init(void *ctx, const htr_uart_t *u, uint32_t now)
{
    fake_drv_ctx_t *c = (fake_drv_ctx_t *)ctx;
    (void)now; c->uart = u; c->inits++; c->probe_calls = 0u; c->any_rx = false;
}
static void f_process(void *ctx, uint32_t now) { fake_drv_ctx_t *c = (fake_drv_ctx_t *)ctx; c->processes++; touch(c, now); }
static void f_start(void *ctx, const htr_request_t *rq) { fake_drv_ctx_t *c = (fake_drv_ctx_t *)ctx; c->starts++; c->last_rq = *rq; }
static void f_stop(void *ctx) { ((fake_drv_ctx_t *)ctx)->stops++; }
static void f_apply(void *ctx, const htr_request_t *rq) { fake_drv_ctx_t *c = (fake_drv_ctx_t *)ctx; c->applies++; c->last_rq = *rq; }
static htr_probe_t f_probe(void *ctx, uint32_t now)
{
    fake_drv_ctx_t *c = (fake_drv_ctx_t *)ctx;
    c->probe_calls++;
    if (c->probe_calls < c->probe_after) return HTR_PROBE_BUSY;
    if (c->probe_answer == HTR_PROBE_FOUND) touch(c, now);
    return c->probe_answer;
}
static void f_status(const void *ctx, uint32_t now, htr_status_t *out)
{
    const fake_drv_ctx_t *c = (const fake_drv_ctx_t *)ctx;
    (void)now; memset(out, 0, sizeof(*out));
    out->any_rx = c->any_rx; out->last_rx_tick = c->last_rx; out->hint = c->hint;
}
#define FAKE(i, type, wiring) \
    { type, wiring, (uint16_t)(1u << i), &fake_ctx[i], f_init, f_process, f_start, f_stop, f_apply, f_probe, f_status }
const htr_driver_t fake_drv[3] = { FAKE(0, 1u, HTR_WIRING_3W), FAKE(1, 2u, HTR_WIRING_4W), FAKE(2, 3u, HTR_WIRING_3W) };

void fake_drivers_reset(void)
{
    unsigned i;
    memset(fake_ctx, 0, sizeof(fake_ctx));
    for (i = 0u; i < 3u; ++i) { fake_ctx[i].probe_answer = HTR_PROBE_ABSENT; fake_ctx[i].probe_after = 3u; fake_ctx[i].alive = true; fake_ctx[i].hint = HTR_HINT_OFF; }
}
````

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build Tests/build --target test_htr_fsm && (cd Tests/build && ctest -R '^test_htr_fsm$' --output-on-failure)`
Expected: PASS (16 tests).

- [ ] **Step 5: Commit**

```bash
git add heater-common/htr_i2c_map.h heater-coproc/core/htr_fsm.[ch] Tests/fakes/fake_htr_driver.[ch] Tests/test_htr_fsm.c Tests/CMakeLists.txt
git commit -m "feat(heater-coproc): generic heater FSM + auto-detect, frozen I2C map

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_017uxgfN1Y67gVKWmTWmpxxk"
```

---

### Task 8: I²C register file (`htr_regs`) + persisted settings seam

**Files:**
- Create: `heater-coproc/core/htr_nv.h`, `heater-coproc/core/htr_version.h`, `heater-coproc/core/htr_regs.h`, `heater-coproc/core/htr_regs.c`, `Tests/test_htr_regs.c`
- Modify: `Tests/CMakeLists.txt` (append)

**Interfaces:**
- Consumes: `htr_fsm_*` + `htr_i2c_map.h` (Task 7).
- Produces: `htr_nv_t {load, save, ctx}`, `HTR_NV_DEFAULT_TYPE 0xFF`, `HTR_NV_DEFAULT_TIMEOUT 10`; `HTR_FW_MAJOR/MINOR/BUILD`; `htr_regs_cfg_t {nv, hw_id, supported_types, lock, unlock}`; `uint8_t htr_regs_init(const htr_regs_cfg_t*, uint32_t now)` (returns HEATER_TYPE setting for `htr_fsm_init`); ISR side `htr_regs_i2c_begin(bool is_read)`, `htr_regs_i2c_rx(uint8_t)`, `uint8_t htr_regs_i2c_tx(void)`, `htr_regs_i2c_end(void)`; loop side `htr_regs_poll(uint32_t now)`, `htr_regs_publish(uint32_t now, uint32_t echo_errs)`.

Superloop order (used by Plan 2's `main.c`): `htr_regs_poll(now); htr_fsm_process(now); htr_regs_publish(now, echo_errs);`.

- [ ] **Step 1: Write the failing test**

`Tests/test_htr_regs.c`:

````c
#include "htr_regs.h"
#include "htr_fsm.h"
#include "htr_i2c_map.h"
#include "htr_version.h"
#include "fake_htr_driver.h"
#include "fake_htr_gpio.h"
#include "fake_htr_uart.h"
#include "unity.h"
#include <string.h>

static fake_gpio_t g; static fake_uart_t u3, u4;
static const htr_driver_t *const drvs[] = { &fake_drv[0], &fake_drv[1] };
static uint32_t now;
/* fake nv */
static bool nv_valid; static uint8_t nv_type, nv_to; static unsigned nv_saves;
static bool nv_load(void *c, uint8_t *t, uint8_t *to){ (void)c; if(!nv_valid) return false; *t=nv_type; *to=nv_to; return true; }
static void nv_save(void *c, uint8_t t, uint8_t to){ (void)c; nv_valid=true; nv_type=t; nv_to=to; nv_saves++; }
static const htr_nv_t nv = { nv_load, nv_save, 0 };

/* I2C master helpers (what the G0B1's Mem_Write / Mem_Read look like). */
static void mwrite(uint8_t reg, const uint8_t *d, size_t n){
    htr_regs_i2c_begin(false); htr_regs_i2c_rx(reg); for(size_t i=0;i<n;i++) htr_regs_i2c_rx(d[i]); htr_regs_i2c_end();
}
static void mwrite1(uint8_t reg, uint8_t v){ mwrite(reg,&v,1); }
static void mread(uint8_t reg, uint8_t *d, size_t n){
    htr_regs_i2c_begin(false); htr_regs_i2c_rx(reg); htr_regs_i2c_end();     /* set pointer */
    htr_regs_i2c_begin(true); for(size_t i=0;i<n;i++) d[i]=htr_regs_i2c_tx(); htr_regs_i2c_end();
}
static uint8_t rd8(uint8_t reg){ uint8_t v; mread(reg,&v,1); return v; }
static uint16_t rd16(uint8_t reg){ uint8_t v[2]; mread(reg,v,2); return (uint16_t)(v[0]|(v[1]<<8)); }
static void loop_to(uint32_t t){ for(; now<t; now+=10u){ htr_regs_poll(now); htr_fsm_process(now); htr_regs_publish(now,0); } }

static void boot(void){
    htr_regs_cfg_t rc = { &nv, HTR_HW_ID_EF_L011, (1u<<1)|(1u<<2), 0, 0 };
    uint8_t type = htr_regs_init(&rc, now);
    htr_fsm_cfg_t fc = { drvs, 2, &g.api, false, &u3.api, &u4.api };
    htr_fsm_init(&fc, type, now);
}
void setUp(void){
    fake_drivers_reset(); fake_gpio_init(&g); fake_uart_init(&u3); fake_uart_init(&u4);
    now=0; nv_valid=false; nv_saves=0;
    g.high_pct[HTR_WIRING_3W]=100; fake_ctx[0].probe_answer=HTR_PROBE_FOUND;
}
void tearDown(void){}

void test_identity_block_and_defaults(void){
    boot(); loop_to(100);
    TEST_ASSERT_EQUAL_HEX8(HTR_DEVICE_ID, rd8(HTR_R_DEVICE_ID));
    TEST_ASSERT_EQUAL_UINT8(HTR_MAP_VERSION, rd8(HTR_R_MAP_VERSION));
    TEST_ASSERT_EQUAL_UINT8(HTR_FW_MINOR, rd8(HTR_R_FW_MINOR));
    TEST_ASSERT_EQUAL_HEX16(0x0006, rd16(HTR_R_SUPPORTED_TYPES));
    TEST_ASSERT_EQUAL_UINT8(HTR_HW_ID_EF_L011, rd8(HTR_R_HW_ID));
    TEST_ASSERT_EQUAL_HEX8(HTR_TYPE_AUTO, rd8(HTR_R_HEATER_TYPE));      /* blank nv -> AUTO */
    TEST_ASSERT_EQUAL_UINT8(10, rd8(HTR_R_HOST_TIMEOUT));
    TEST_ASSERT_EQUAL_HEX8(0xFF, rd8(0x7F));                            /* beyond the map */
}
void test_read_is_one_consistent_snapshot(void){
    boot(); loop_to(100);
    htr_regs_i2c_begin(false); htr_regs_i2c_rx(HTR_R_PHASE); htr_regs_i2c_end();
    htr_regs_i2c_begin(true);
    uint8_t p0 = htr_regs_i2c_tx();
    loop_to(7000);                    /* detection completes mid-transaction */
    uint8_t t0 = htr_regs_i2c_tx();   /* ACTIVE_TYPE from the same snapshot */
    htr_regs_i2c_end();
    TEST_ASSERT_EQUAL_UINT8(HTR_PHASE_DETECTING, p0);
    TEST_ASSERT_EQUAL_UINT8(HTR_TYPE_NONE, t0);
    TEST_ASSERT_EQUAL_UINT8(1u, rd8(HTR_R_ACTIVE_TYPE));               /* next read sees it */
}
void test_control_block_write_reaches_driver(void){
    boot(); loop_to(7000);
    const uint8_t ctl[] = { 1, 7, 21, 0xE6, 0x00 };   /* run, level 7, 21 C, ambient 23.0 C */
    mwrite(HTR_R_RUN_REQUEST, ctl, sizeof ctl); loop_to(7100);
    TEST_ASSERT_EQUAL(1u, fake_ctx[0].starts);
    TEST_ASSERT_EQUAL_UINT8(7, fake_ctx[0].last_rq.level);
    TEST_ASSERT_EQUAL_INT8(21, fake_ctx[0].last_rq.setpoint_c);
    TEST_ASSERT_EQUAL_INT16(230, fake_ctx[0].last_rq.ambient_c_x10);
    TEST_ASSERT_EQUAL_UINT8(1, rd8(HTR_R_RUN_REQUEST));
    TEST_ASSERT_EQUAL_UINT8(HTR_PHASE_STARTING, rd8(HTR_R_PHASE));
}
void test_ro_writes_ignored_and_bad_values_rejected(void){
    boot(); loop_to(7000);
    mwrite1(HTR_R_PHASE, 3); mwrite1(HTR_R_DEVICE_ID, 0); loop_to(7100);
    TEST_ASSERT_EQUAL_UINT8(HTR_PHASE_OFF, rd8(HTR_R_PHASE));
    TEST_ASSERT_EQUAL_HEX8(HTR_DEVICE_ID, rd8(HTR_R_DEVICE_ID));
    mwrite1(HTR_R_LEVEL, 11); loop_to(7200);
    TEST_ASSERT_EQUAL_UINT8(HTR_RES_BAD_ARG, rd8(HTR_R_CMD_RESULT));
    TEST_ASSERT_EQUAL_UINT8(1, rd8(HTR_R_LEVEL));
    mwrite1(HTR_R_SETPOINT_C, 40); mwrite1(HTR_R_RUN_REQUEST, 2); loop_to(7300);
    TEST_ASSERT_EQUAL_INT8(20, (int8_t)rd8(HTR_R_SETPOINT_C));
    TEST_ASSERT_EQUAL_UINT8(0, rd8(HTR_R_RUN_REQUEST));
}
void test_half_written_ambient_is_rejected(void){
    boot(); loop_to(7000);
    mwrite1(HTR_R_AMBIENT_C_X10, 0x10); loop_to(7100);
    TEST_ASSERT_EQUAL_UINT8(HTR_RES_BAD_ARG, rd8(HTR_R_CMD_RESULT));
    TEST_ASSERT_EQUAL_HEX16(0x7FFF, rd16(HTR_R_AMBIENT_C_X10));
}
void test_heater_type_write_persists_and_validates(void){
    boot(); loop_to(7000);
    mwrite1(HTR_R_HEATER_TYPE, 9); loop_to(7100);
    TEST_ASSERT_EQUAL_UINT8(HTR_RES_NOT_SUPPORTED, rd8(HTR_R_CMD_RESULT));
    TEST_ASSERT_EQUAL(0u, nv_saves);
    mwrite1(HTR_R_HEATER_TYPE, 2); loop_to(7200);
    TEST_ASSERT_EQUAL_UINT8(HTR_RES_OK, rd8(HTR_R_CMD_RESULT));
    TEST_ASSERT_EQUAL(1u, nv_saves); TEST_ASSERT_EQUAL_UINT8(2, nv_type);
    TEST_ASSERT_EQUAL_UINT8(2, rd8(HTR_R_HEATER_TYPE));
    mwrite1(HTR_R_HOST_TIMEOUT, 61); loop_to(7300);
    TEST_ASSERT_EQUAL_UINT8(10, rd8(HTR_R_HOST_TIMEOUT));
    mwrite1(HTR_R_HOST_TIMEOUT, 5); loop_to(7400);
    TEST_ASSERT_EQUAL_UINT8(5, nv_to);
}
void test_persisted_settings_load_at_boot(void){
    nv_valid=true; nv_type=1; nv_to=3; boot(); loop_to(100);
    TEST_ASSERT_EQUAL_UINT8(1, rd8(HTR_R_HEATER_TYPE));
    TEST_ASSERT_EQUAL_UINT8(3, rd8(HTR_R_HOST_TIMEOUT));
}
void test_host_timeout_stops_via_cooldown_path_and_flags(void){
    boot(); loop_to(7000);
    mwrite1(HTR_R_HOST_TIMEOUT, 1); mwrite1(HTR_R_RUN_REQUEST, 1); loop_to(7100);
    fake_ctx[0].hint = HTR_HINT_RUNNING; loop_to(8000);
    TEST_ASSERT_EQUAL_UINT8(HTR_PHASE_RUNNING, rd8(HTR_R_PHASE));   /* reads count as contact */
    loop_to(8000 + 59000);
    TEST_ASSERT_EQUAL(0u, fake_ctx[0].stops);                        /* not yet: 59 s idle */
    loop_to(8000 + 61000);
    TEST_ASSERT_EQUAL(1u, fake_ctx[0].stops);
    TEST_ASSERT_TRUE(rd16(HTR_R_FLAGS) & HTR_F_HOST_TIMEOUT_STOP);
    TEST_ASSERT_EQUAL_UINT8(HTR_PHASE_STOP_REQUESTED, rd8(HTR_R_PHASE));
    TEST_ASSERT_TRUE(g.line[HTR_LINE_TX_EN]);                        /* never a power cut */
    mwrite1(HTR_R_RUN_REQUEST, 0); loop_to(now + 20);
    TEST_ASSERT_FALSE(rd16(HTR_R_FLAGS) & HTR_F_HOST_TIMEOUT_STOP);
}
void test_host_timeout_zero_disables(void){
    boot(); loop_to(7000);
    mwrite1(HTR_R_HOST_TIMEOUT, 0); mwrite1(HTR_R_RUN_REQUEST, 1);
    fake_ctx[0].hint = HTR_HINT_RUNNING;          /* healthy run, no G0B1 traffic */
    loop_to(7000 + 30u*60000u);
    TEST_ASSERT_EQUAL_UINT8(HTR_PHASE_RUNNING, rd8(HTR_R_PHASE));
    TEST_ASSERT_EQUAL(0u, fake_ctx[0].stops);
}
void test_commands(void){
    boot(); loop_to(7000);
    mwrite1(HTR_R_COMMAND, HTR_CMD_START); loop_to(7100);
    TEST_ASSERT_EQUAL_UINT8(1, rd8(HTR_R_RUN_REQUEST)); TEST_ASSERT_EQUAL(1u, fake_ctx[0].starts);
    mwrite1(HTR_R_COMMAND, HTR_CMD_AUTO_DETECT); loop_to(7200);
    TEST_ASSERT_EQUAL_UINT8(HTR_RES_BUSY, rd8(HTR_R_CMD_RESULT));
    mwrite1(HTR_R_COMMAND, HTR_CMD_STOP); loop_to(7300);
    TEST_ASSERT_EQUAL_UINT8(0, rd8(HTR_R_RUN_REQUEST)); TEST_ASSERT_EQUAL(1u, fake_ctx[0].stops);
    mwrite1(HTR_R_COMMAND, 99); loop_to(7400);
    TEST_ASSERT_EQUAL_UINT8(HTR_RES_BAD_ARG, rd8(HTR_R_CMD_RESULT));
    mwrite1(HTR_R_HEATER_TYPE, HTR_TYPE_NONE); loop_to(30000);
    mwrite1(HTR_R_COMMAND, HTR_CMD_START); loop_to(30100);
    TEST_ASSERT_EQUAL_UINT8(HTR_RES_NO_TYPE, rd8(HTR_R_CMD_RESULT));
}
void test_status_block_reflects_driver_and_echo_fault_is_sticky(void){
    boot(); loop_to(7000);
    TEST_ASSERT_EQUAL_UINT8(1, rd8(HTR_R_ACTIVE_TYPE));
    TEST_ASSERT_EQUAL_HEX16(1u << 0, rd16(HTR_R_CAPS));
    uint16_t f = rd16(HTR_R_FLAGS);
    TEST_ASSERT_TRUE(f & HTR_F_PRESENT); TEST_ASSERT_TRUE(f & HTR_F_FRESH);
    TEST_ASSERT_TRUE(f & HTR_F_SAFE_OFF); TEST_ASSERT_TRUE(f & HTR_F_IFACE_POWERED);
    htr_regs_publish(now, 5);
    TEST_ASSERT_TRUE(rd16(HTR_R_FLAGS) & HTR_F_ECHO_FAULT);
    TEST_ASSERT_EQUAL_UINT16(5, rd16(HTR_R_ECHO_ERRS));
    now += 3100; htr_regs_publish(now, 5);
    TEST_ASSERT_FALSE(rd16(HTR_R_FLAGS) & HTR_F_ECHO_FAULT);
}
int main(void){ UNITY_BEGIN();
    RUN_TEST(test_identity_block_and_defaults);
    RUN_TEST(test_read_is_one_consistent_snapshot);
    RUN_TEST(test_control_block_write_reaches_driver);
    RUN_TEST(test_ro_writes_ignored_and_bad_values_rejected);
    RUN_TEST(test_half_written_ambient_is_rejected);
    RUN_TEST(test_heater_type_write_persists_and_validates);
    RUN_TEST(test_persisted_settings_load_at_boot);
    RUN_TEST(test_host_timeout_stops_via_cooldown_path_and_flags);
    RUN_TEST(test_host_timeout_zero_disables);
    RUN_TEST(test_commands);
    RUN_TEST(test_status_block_reflects_driver_and_echo_fault_is_sticky);
    return UNITY_END(); }
````

Append to `Tests/CMakeLists.txt`:

```cmake
add_heater_test(test_htr_regs test_htr_regs.c ${HCORE}/htr_regs.c ${HCORE}/htr_fsm.c ${HCORE}/htr_iface.c fakes/fake_htr_driver.c fakes/fake_htr_gpio.c fakes/fake_htr_uart.c)
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake -S Tests -B Tests/build >/dev/null && cmake --build Tests/build --target test_htr_regs`
Expected: FAIL — `htr_regs.h: No such file or directory`.

- [ ] **Step 3: Write the implementation**

`heater-coproc/core/htr_nv.h`:

````c
#ifndef HTR_NV_H
#define HTR_NV_H
/* Persisted settings seam: L011 data EEPROM, C011 one flash page, tests a fake. */
#include <stdbool.h>
#include <stdint.h>
typedef struct htr_nv {
    bool (*load)(void *ctx, uint8_t *type_setting, uint8_t *host_timeout_min); /* false = blank/corrupt */
    void (*save)(void *ctx, uint8_t type_setting, uint8_t host_timeout_min);
    void *ctx;
} htr_nv_t;
#define HTR_NV_DEFAULT_TYPE     0xFFu   /* HTR_TYPE_AUTO */
#define HTR_NV_DEFAULT_TIMEOUT  10u
#endif
````

`heater-coproc/core/htr_version.h`:

````c
#ifndef HTR_VERSION_H
#define HTR_VERSION_H
#define HTR_FW_MAJOR 0u
#define HTR_FW_MINOR 1u
#define HTR_FW_BUILD 1u
#endif
````

`heater-coproc/core/htr_regs.h`:

````c
#ifndef HTR_REGS_H
#define HTR_REGS_H
/* I2C register file (spec section 6). The I2C slave ISR calls the
   htr_regs_i2c_* functions; the superloop calls htr_regs_poll() (apply
   writes/commands, host timeout) and htr_regs_publish() (refresh status).
   Shared state is guarded by the lock()/unlock() hooks (IRQ off on target). */
#include "htr_nv.h"
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    const htr_nv_t *nv;
    uint8_t hw_id;
    uint16_t supported_types;
    void (*lock)(void);
    void (*unlock)(void);
} htr_regs_cfg_t;

/* Loads persisted settings. Returns the HEATER_TYPE setting to boot with. */
uint8_t htr_regs_init(const htr_regs_cfg_t *cfg, uint32_t now);
uint8_t htr_regs_host_timeout_min(void);

/* ---- ISR side ---- */
void    htr_regs_i2c_begin(bool is_read);   /* address match */
void    htr_regs_i2c_rx(uint8_t byte);      /* master wrote a byte */
uint8_t htr_regs_i2c_tx(void);              /* master reads a byte */
void    htr_regs_i2c_end(void);             /* STOP */

/* ---- superloop side ---- */
void htr_regs_poll(uint32_t now);
void htr_regs_publish(uint32_t now, uint32_t echo_errs);
#endif
````

`heater-coproc/core/htr_regs.c`:

````c
#include "htr_regs.h"
#include "htr_fsm.h"
#include "htr_i2c_map.h"
#include "htr_types.h"
#include "htr_version.h"
#include <string.h>

#define ECHO_STICKY_MS 3000u

static htr_regs_cfg_t s_cfg;
/* live: written by the superloop; snap: frozen copy served to one read txn. */
static uint8_t s_live[HTR_REG_COUNT];
static uint8_t s_snap[HTR_REG_COUNT];
/* staged master writes, consumed by htr_regs_poll() */
static uint8_t s_stage[HTR_REG_COUNT];
static uint8_t s_dirty[HTR_REG_COUNT];
static volatile bool s_commit;
static volatile bool s_host_seen;
/* ISR transaction state */
static bool s_in_read;
static bool s_have_ptr;
static uint8_t s_ptr;
/* control state owned by the superloop */
static htr_request_t s_rq;
static uint8_t s_type_setting;
static uint8_t s_timeout_min;
static uint8_t s_cmd_result;
static uint32_t s_host_last;
static bool s_host_stop;
static uint32_t s_prev_echo;
static uint32_t s_echo_tick;
static bool s_echo_seen;

static void lock(void)   { if (s_cfg.lock) s_cfg.lock(); }
static void unlock(void) { if (s_cfg.unlock) s_cfg.unlock(); }

static void put16(uint8_t *m, uint8_t r, uint16_t v) { m[r] = (uint8_t)(v & 0xFFu); m[r + 1u] = (uint8_t)(v >> 8); }
static uint16_t sat16(uint32_t v) { return (v > 0xFFFFu) ? 0xFFFFu : (uint16_t)v; }

static void fill_control(uint8_t *m)
{
    m[HTR_R_HEATER_TYPE] = s_type_setting;
    m[HTR_R_RUN_REQUEST] = s_rq.run;
    m[HTR_R_LEVEL] = s_rq.level;
    m[HTR_R_SETPOINT_C] = (uint8_t)s_rq.setpoint_c;
    put16(m, HTR_R_AMBIENT_C_X10, (uint16_t)s_rq.ambient_c_x10);
    m[HTR_R_HOST_TIMEOUT] = s_timeout_min;
    m[HTR_R_COMMAND] = 0u;
    m[HTR_R_CMD_RESULT] = s_cmd_result;
}

uint8_t htr_regs_init(const htr_regs_cfg_t *cfg, uint32_t now)
{
    s_cfg = *cfg;
    memset(s_live, 0, sizeof(s_live));
    memset(s_dirty, 0, sizeof(s_dirty));
    s_commit = false;
    s_host_seen = false;
    s_in_read = false;
    s_have_ptr = false;
    s_ptr = 0u;
    s_rq.run = 0u;
    s_rq.level = 1u;
    s_rq.setpoint_c = 20;
    s_rq.ambient_c_x10 = HTR_AMBIENT_UNKNOWN;
    s_cmd_result = HTR_RES_OK;
    s_host_last = now;
    s_host_stop = false;
    s_prev_echo = 0u;
    s_echo_seen = false;
    if ((cfg->nv == NULL) || !cfg->nv->load(cfg->nv->ctx, &s_type_setting, &s_timeout_min) ||
        (s_timeout_min > 60u)) {
        s_type_setting = HTR_NV_DEFAULT_TYPE;
        s_timeout_min = HTR_NV_DEFAULT_TIMEOUT;
    }
    s_live[HTR_R_DEVICE_ID] = HTR_DEVICE_ID;
    s_live[HTR_R_MAP_VERSION] = HTR_MAP_VERSION;
    s_live[HTR_R_FW_MAJOR] = HTR_FW_MAJOR;
    s_live[HTR_R_FW_MINOR] = HTR_FW_MINOR;
    put16(s_live, HTR_R_FW_BUILD, HTR_FW_BUILD);
    put16(s_live, HTR_R_SUPPORTED_TYPES, cfg->supported_types);
    s_live[HTR_R_HW_ID] = cfg->hw_id;
    fill_control(s_live);
    return s_type_setting;
}

uint8_t htr_regs_host_timeout_min(void) { return s_timeout_min; }

/* ---------------- ISR side ---------------- */
void htr_regs_i2c_begin(bool is_read)
{
    s_host_seen = true;
    s_in_read = is_read;
    if (is_read) {
        memcpy(s_snap, s_live, sizeof(s_snap));   /* one consistent image per read */
    } else {
        s_have_ptr = false;
    }
}

void htr_regs_i2c_rx(uint8_t byte)
{
    if (!s_have_ptr) { s_ptr = byte; s_have_ptr = true; return; }
    if (s_ptr < HTR_REG_COUNT) {
        s_stage[s_ptr] = byte;
        s_dirty[s_ptr] = 1u;
    }
    s_ptr++;
}

uint8_t htr_regs_i2c_tx(void)
{
    uint8_t v = (s_ptr < HTR_REG_COUNT) ? s_snap[s_ptr] : 0xFFu;
    s_ptr++;
    return v;
}

void htr_regs_i2c_end(void)
{
    if (!s_in_read) s_commit = true;
}

/* ---------------- superloop side ---------------- */
static bool known_type(uint8_t t)
{
    return (t == HTR_TYPE_AUTO) || (t == HTR_TYPE_NONE) ||
           ((t < 16u) && ((s_cfg.supported_types >> t) & 1u));
}

static void persist(void)
{
    if (s_cfg.nv != NULL) s_cfg.nv->save(s_cfg.nv->ctx, s_type_setting, s_timeout_min);
}

static void run_command(uint8_t cmd, uint32_t now)
{
    switch (cmd) {
    case HTR_CMD_START:
        if (s_type_setting == HTR_TYPE_NONE) { s_cmd_result = HTR_RES_NO_TYPE; return; }
        s_rq.run = 1u; s_host_stop = false; s_cmd_result = HTR_RES_OK; return;
    case HTR_CMD_STOP:
        s_rq.run = 0u; s_cmd_result = HTR_RES_OK; return;
    case HTR_CMD_CLEAR_FAULT:
        s_cmd_result = htr_fsm_clear_fault(now); return;
    case HTR_CMD_REINIT_IFACE:
        s_cmd_result = htr_fsm_reinit_iface(now); return;
    case HTR_CMD_AUTO_DETECT:
        s_cmd_result = htr_fsm_redetect(now); return;
    default:
        s_cmd_result = HTR_RES_BAD_ARG; return;
    }
}

static void apply_writes(const uint8_t *st, const uint8_t *dt, uint32_t now)
{
    if (dt[HTR_R_HEATER_TYPE]) {
        uint8_t t = st[HTR_R_HEATER_TYPE];
        if (!known_type(t)) s_cmd_result = HTR_RES_NOT_SUPPORTED;
        else if (t != s_type_setting) {
            s_cmd_result = htr_fsm_set_type(t, now);
            if (s_cmd_result == HTR_RES_OK) { s_type_setting = t; persist(); }
        }
    }
    if (dt[HTR_R_RUN_REQUEST]) {
        if (st[HTR_R_RUN_REQUEST] > 1u) s_cmd_result = HTR_RES_BAD_ARG;
        else { s_rq.run = st[HTR_R_RUN_REQUEST]; s_host_stop = false; }
    }
    if (dt[HTR_R_LEVEL]) {
        if ((st[HTR_R_LEVEL] < 1u) || (st[HTR_R_LEVEL] > 10u)) s_cmd_result = HTR_RES_BAD_ARG;
        else s_rq.level = st[HTR_R_LEVEL];
    }
    if (dt[HTR_R_SETPOINT_C]) {
        int8_t sp = (int8_t)st[HTR_R_SETPOINT_C];
        if ((sp < 5) || (sp > 30)) s_cmd_result = HTR_RES_BAD_ARG;
        else s_rq.setpoint_c = sp;
    }
    if (dt[HTR_R_AMBIENT_C_X10] && dt[HTR_R_AMBIENT_C_X10 + 1u]) {
        int16_t a = (int16_t)(uint16_t)(st[HTR_R_AMBIENT_C_X10] | ((uint16_t)st[HTR_R_AMBIENT_C_X10 + 1u] << 8));
        if ((a == HTR_AMBIENT_UNKNOWN) || ((a >= -400) && (a <= 850))) s_rq.ambient_c_x10 = a;
        else s_cmd_result = HTR_RES_BAD_ARG;
    } else if (dt[HTR_R_AMBIENT_C_X10] || dt[HTR_R_AMBIENT_C_X10 + 1u]) {
        s_cmd_result = HTR_RES_BAD_ARG;             /* half an s16 is never applied */
    }
    if (dt[HTR_R_HOST_TIMEOUT]) {
        if (st[HTR_R_HOST_TIMEOUT] > 60u) s_cmd_result = HTR_RES_BAD_ARG;
        else if (st[HTR_R_HOST_TIMEOUT] != s_timeout_min) { s_timeout_min = st[HTR_R_HOST_TIMEOUT]; persist(); }
    }
    if (dt[HTR_R_COMMAND]) run_command(st[HTR_R_COMMAND], now);
}

void htr_regs_poll(uint32_t now)
{
    uint8_t st[HTR_REG_COUNT];
    uint8_t dt[HTR_REG_COUNT];
    bool commit;
    lock();
    commit = s_commit;
    if (commit) {
        memcpy(st, s_stage, sizeof(st));
        memcpy(dt, s_dirty, sizeof(dt));
        memset(s_dirty, 0, sizeof(s_dirty));
        s_commit = false;
    }
    if (s_host_seen) { s_host_seen = false; s_host_last = now; }
    unlock();

    if (commit) apply_writes(st, dt, now);

    /* Host timeout (D2): controlled stop through the normal cooldown path. */
    if (s_rq.run && (s_timeout_min != 0u) &&
        ((uint32_t)(now - s_host_last) >= (uint32_t)s_timeout_min * 60000u)) {
        s_rq.run = 0u;
        s_host_stop = true;
    }
    htr_fsm_set_request(&s_rq);
}

void htr_regs_publish(uint32_t now, uint32_t echo_errs)
{
    htr_fsm_view_t v;
    uint8_t m[HTR_REG_COUNT];
    uint16_t flags;
    uint32_t age;

    htr_fsm_view(now, &v);
    s_rq.run = v.rq.run;                     /* FAULT forces run to 0: reflect it */
    if (echo_errs != s_prev_echo) { s_prev_echo = echo_errs; s_echo_tick = now; s_echo_seen = true; }

    flags = v.flags;
    if (s_host_stop) flags |= HTR_F_HOST_TIMEOUT_STOP;
    if (s_echo_seen && ((uint32_t)(now - s_echo_tick) <= ECHO_STICKY_MS)) flags |= HTR_F_ECHO_FAULT;
    age = v.st.any_rx ? (uint32_t)(now - v.st.last_rx_tick) : 0xFFFFFFFFu;

    memset(&m[HTR_STATUS_FIRST], 0, HTR_STATUS_LEN);
    m[HTR_R_PHASE] = v.phase;
    m[HTR_R_ACTIVE_TYPE] = v.active_type;
    put16(m, HTR_R_FLAGS, flags);
    put16(m, HTR_R_VALID, v.st.valid);
    m[HTR_R_VENDOR_STATE] = v.st.vendor_state;
    m[HTR_R_VENDOR_SUB] = v.st.vendor_sub;
    m[HTR_R_FAULT_CODE] = v.st.fault_code;
    m[HTR_R_ACTIVE_LEVEL] = v.st.active_level;
    put16(m, HTR_R_SUPPLY_MV, v.st.supply_mv);
    put16(m, HTR_R_FAN_RPM, v.st.fan_rpm);
    put16(m, HTR_R_PUMP_HZ_X10, v.st.pump_hz_x10);
    put16(m, HTR_R_EXCH_RAW, v.st.exch_raw);
    put16(m, HTR_R_EXCH_C_X10, (uint16_t)v.st.exch_c_x10);
    put16(m, HTR_R_STATE_SECONDS, v.st.state_seconds);
    put16(m, HTR_R_AGE_MS, sat16(age));
    put16(m, HTR_R_VALID_FRAMES, (uint16_t)(v.st.valid_frames & 0xFFFFu));
    put16(m, HTR_R_CRC_FAILS, sat16(v.st.crc_fails));
    put16(m, HTR_R_TRANSPORT_ERRS, sat16(v.st.transport_errs));
    put16(m, HTR_R_ECHO_ERRS, sat16(echo_errs));
    put16(m, HTR_R_CAPS, v.caps);
    put16(m, HTR_R_HOST_IDLE_S, sat16((uint32_t)(now - s_host_last) / 1000u));
    m[HTR_R_LINE_SENSE] = v.line_sense;
    m[HTR_R_DETECT_RESULT] = v.detect_result;

    lock();
    fill_control(s_live);
    memcpy(&s_live[HTR_STATUS_FIRST], &m[HTR_STATUS_FIRST], HTR_STATUS_LEN);
    unlock();
}
````

- [ ] **Step 4: Run test to verify it passes, then the whole suite**

Run: `cmake --build Tests/build --target test_htr_regs && (cd Tests/build && ctest -R '^test_htr_regs$' --output-on-failure)`
Expected: PASS (11 tests).
Run: `(cd Tests/build && cmake --build . && ctest)`
Expected: `100% tests passed` (all pre-existing tests + the 8 new targets).

- [ ] **Step 5: Commit**

```bash
git add heater-coproc/core/htr_nv.h heater-coproc/core/htr_version.h heater-coproc/core/htr_regs.[ch] Tests/test_htr_regs.c Tests/CMakeLists.txt
git commit -m "feat(heater-coproc): I2C register file, commands, host timeout

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_017uxgfN1Y67gVKWmTWmpxxk"
```
