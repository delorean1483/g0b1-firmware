/* bl_update_mode.c — bootloader update-mode receive/dispatch loop.
 *
 * Receive one RTU frame (1 s idle budget), gate it on our address + CRC16, hand
 * the PDU (CRC stripped) to the portable bl_session state machine, append a
 * fresh CRC16 to its response body, and transmit. On a COMMIT ack the session
 * asks for a reset; let the TX drain, then reboot into the freshly committed
 * slot.
 */
#include "bl_update_mode.h"
#include "bl_session.h"
#include "boot_uart.h"
#include "modbus_frame.h"
#include "modbus_crc.h"
#include "stm32g0xx_hal.h"

extern IWDG_HandleTypeDef hiwdg;

void bl_update_mode(const iflash_backend_t *fl, bootcfg_t *cfg)
{
  bl_session_t s;
  bl_session_init(&s, fl, cfg);
  static uint8_t req[256];
  static uint8_t resp[256];

  for (;;)
  {
    HAL_IWDG_Refresh(&hiwdg);
    uint16_t n = boot_uart_recv_frame(req, sizeof req, 1000u);   /* 1 s idle budget */
    if (n == 0)
    {
      continue;                                                  /* stay in update mode */
    }
    if (mb_check_frame(req, n, 1u) != MB_FRAME_OK)
    {
      continue;                                                  /* addr/CRC gate */
    }
    uint16_t blen = bl_session_process(&s, req, (uint16_t)(n - 2u), resp); /* strip CRC16 */
    if (blen == 0)
    {
      continue;
    }
    uint16_t crc = modbus_crc16(resp, blen);
    resp[blen] = (uint8_t)crc;
    resp[blen + 1] = (uint8_t)(crc >> 8);
    boot_uart_send(resp, (uint16_t)(blen + 2u));

    if (s.want_reset)                    /* COMMIT acked; let TX drain, then reset */
    {
      HAL_Delay(20);
      NVIC_SystemReset();
    }
  }
}
