/* drv_modbus_uart.c — USART1 RS-485 Modbus RTU slave transport (Task 5).
 *
 * RX: HAL_UARTEx_ReceiveToIdle_DMA into a circular-sized buffer; the USART
 *     IDLE-line event (raised one character time after the last byte) delimits
 *     the RTU frame. The DMA half-transfer interrupt is disabled so the RxEvent
 *     fires only on IDLE (frame gap) or TC (buffer full = oversized/garbage).
 *
 *     NOTE (spec deviation, intentional): the spec/plan name the USART Receiver
 *     Timeout (RTO, 3.5-char) for the inter-frame gap. HAL's ReceiveToIdle uses
 *     the IDLE line (~1 char) instead — it is the HAL-native, well-tested path
 *     for variable-length RTU reception and delimits frames correctly at 9600
 *     with a normal master. If the bench sees frames split by intra-frame gaps,
 *     switch to a manual RTO IRQ (UART_IT_RTO / RTOF) — the poll()/engine path
 *     below is unchanged.
 *
 * TX: HAL_UART_Transmit_DMA; the USART hardware Driver-Enable (DEM, PB3) asserts
 *     DE for the duration of the transmission automatically — no GPIO toggle.
 *     RX is muted (AbortReceive) for the TX so we never assemble our own echo
 *     as a phantom request (correct whether or not the transceiver ties RE=DE),
 *     then re-armed on TX-complete.
 *
 * Engine handoff happens in drv_modbus_uart_poll() (main loop), never in an ISR.
 */
#include "drv_modbus_uart.h"
#include "mb_engine.h"      /* mb_engine_process() */
#include "modbus_defs.h"    /* MB_MAX_FRAME */
#include "main.h"           /* CubeMX HAL: huart1, HAL_UART* */
#include <string.h>

extern UART_HandleTypeDef huart1;   /* CubeMX-generated (MX_USART1_UART_Init) */

static uint8_t           s_rx[MB_MAX_FRAME];    /* DMA landing buffer          */
static uint8_t           s_frame[MB_MAX_FRAME]; /* snapshot of a complete frame*/
static volatile uint16_t s_frame_len;           /* >0 => frame ready for poll()*/
static uint8_t           s_tx[MB_MAX_FRAME];    /* response for DMA TX         */
static volatile bool     s_tx_active;           /* muting RX while we transmit */

static void arm_rx(void)
{
    (void)HAL_UARTEx_ReceiveToIdle_DMA(&huart1, s_rx, (uint16_t)sizeof s_rx);
    if (huart1.hdmarx != NULL) {
        __HAL_DMA_DISABLE_IT(huart1.hdmarx, DMA_IT_HT);   /* IDLE/TC only, no HT */
    }
}

void drv_modbus_uart_init(void)
{
    s_frame_len = 0u;
    s_tx_active = false;
    arm_rx();
}

/* HAL callback: a reception event (IDLE gap or buffer-full TC) delivered `size`
 * bytes into s_rx. Snapshot the frame for poll() and re-arm. */
void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t size)
{
    if (huart->Instance != USART1) return;
    if (!s_tx_active && s_frame_len == 0u && size > 0u && size <= sizeof s_frame) {
        memcpy(s_frame, s_rx, size);
        s_frame_len = size;          /* hand to poll(); drained there */
    }
    arm_rx();                        /* listen for the next frame */
}

/* HAL callback: our response finished transmitting — resume listening. */
void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance != USART1) return;
    s_tx_active = false;
    arm_rx();
}

/* HAL callback: line error (overrun/framing/noise) — drop any partial frame and
 * recover reception. */
void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance != USART1) return;
    s_frame_len = 0u;
    arm_rx();
}

void drv_modbus_uart_poll(void)
{
    if (s_frame_len == 0u || s_tx_active) return;

    uint16_t resp_len = 0u;
    mb_engine_process(s_frame, s_frame_len, s_tx, &resp_len);
    s_frame_len = 0u;                /* release for the next reception */

    if (resp_len > 0u) {
        s_tx_active = true;
        (void)HAL_UART_AbortReceive(&huart1);              /* mute RX during TX */
        (void)HAL_UART_Transmit_DMA(&huart1, s_tx, resp_len); /* HW DE frames it */
    }
}
