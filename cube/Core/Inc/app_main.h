#ifndef APP_MAIN_H
#define APP_MAIN_H

/* Application entry point. Called once from the CubeMX-generated main()
 * after all MX_*_Init() have run. Never returns — it owns the superloop.
 *
 * Keeping the application in app_main() (not in main.c's USER CODE blocks)
 * means CubeMX code regeneration never touches our logic: main.c only gains
 * a single #include + one call, both inside USER CODE guards.
 *
 * Bench bring-up plan: docs/superpowers/plans/2026-08-17-stm32g0-apu-bench-bringup.md
 * Task 1 = heartbeat only. Task 2 replaces the loop body with the real
 * sched_service()/sched_run() superloop.
 */
void app_main(void);

#endif /* APP_MAIN_H */
