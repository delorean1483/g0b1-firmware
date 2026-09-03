/* boot_main.c — bootloader entry: decide, then jump to a slot or enter update
 * mode.
 *
 * Flow: HAL_Init -> clock -> IWDG (covers the whole decision + update session)
 * -> read & consume the .noinit reset-magic -> load boot-config -> boot_decide.
 * A JUMP_SLOT decision relocates into the chosen app; otherwise we bring up the
 * RS-485 bus and run the update-mode receive/dispatch loop.
 */
#include "stm32g0xx_hal.h"
#include "g0b1_slots.h"
#include "boot_jump.h"
#include "boot_uart.h"
#include "bl_update_mode.h"
#include "bootcfg.h"
#include "boot_decision.h"
#include "drv_iflash.h"

volatile uint32_t g_boot_magic __attribute__((section(".noinit")));

void boot_iwdg_init(void);
void boot_clock_init(void);
extern IWDG_HandleTypeDef hiwdg;

static const iflash_backend_t *FL;
static bootcfg_t CFG;
static int CFG_VALID;

static int slot_ok(void *v, slot_id_t s)
{
  (void)v;
  return bootcfg_slot_crc_ok(FL, &CFG, s);
}

int main(void)
{
  HAL_Init();
  boot_clock_init();
  boot_iwdg_init();                 /* covers boot decision + update mode */
  FL = drv_iflash_backend();

  uint32_t magic = g_boot_magic;
  g_boot_magic = 0;                 /* consume the request */

  CFG_VALID = (bootcfg_load(FL, &CFG) == 0);
  boot_decision_t d = boot_decide(&CFG, CFG_VALID, magic, slot_ok, 0);
  if (d.cfg_dirty)
  {
    (void)bootcfg_save(FL, &CFG);
  }

  if (d.action == BOOT_ACTION_JUMP_SLOT)
  {
    HAL_IWDG_Refresh(&hiwdg);       /* fresh window for the app's own init */
    boot_jump_to_slot(bootcfg_slot_base(d.slot));
  }

  /* ENTER_UPDATE: bring up the bus and receive an image. */
  boot_uart_init();
  bl_update_mode(FL, &CFG);         /* loops; resets on COMMIT */
  for (;;)
  {
    HAL_IWDG_Refresh(&hiwdg);       /* unreachable safety net */
  }
}

void Error_Handler(void)
{
  __disable_irq();
  for (;;)
  {
    /* halt; the IWDG will reset the MCU if it was already armed */
  }
}
