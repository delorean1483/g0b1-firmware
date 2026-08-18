#ifndef DRV_BSP_IO_H
#define DRV_BSP_IO_H

/* Concrete STM32G0 GPIO backend for the portable bsp_io service (App/services).
 *
 * Maps the board_pins.h logical IDs onto physical EF-G0B1R pins per spec §4,
 * confirmed against the known-good PIC original (src/main.c relay + switch
 * polarity) and — the final authority — the "G0B1 APU Manager R1" schematic.
 *
 * Bench bring-up plan: docs/superpowers/plans/2026-08-17-stm32g0-apu-bench-bringup.md
 * Task 3 = relays + discrete inputs + safe-default-off.
 */
#include "bsp_io_backend.h"

/* Master toggle for the Task 3 Step 4 bench aid (default OFF — normal builds
 * must never walk the relays). Define =1 in the build (or flip here) to compile
 * drv_bsp_io_bench_relay_walk() and its call site in app_main. */
#ifndef BSP_IO_BENCH_RELAY_WALK
#define BSP_IO_BENCH_RELAY_WALK 0
#endif

/* Factory: hand this to bsp_io_init(). */
const bsp_io_backend_t *drv_bsp_io_backend(void);

#if BSP_IO_BENCH_RELAY_WALK
/* Blocking bench aid: energize each relay for ~1 s in turn, one at a time, so
 * the tech can hear/meter the click and confirm ONLY the intended relay moves.
 * Runs once before the superloop; uses HAL_Delay, so never call it after the
 * scheduler is live. */
void drv_bsp_io_bench_relay_walk(void);
#endif

#endif /* DRV_BSP_IO_H */
