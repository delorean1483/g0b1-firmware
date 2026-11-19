/* I2C1 slave at the G0B1-facing address. Byte-level events go straight to
   the portable register file (htr_regs_i2c_*). Clock stretching enabled. */
#include "port.h"
#include "htr_board.h"
#include "htr_regs.h"
#include "l011_update.h"

#ifdef L011_RELOCATED_APP
static unsigned update_length;
static bool write_frame, update_match;
static volatile bool update_requested;

bool port_take_update_request(void)
{
    __disable_irq();
    bool requested = update_requested;
    update_requested = false;
    __enable_irq();
    return requested;
}
#endif

void port_i2c_slave_init(uint8_t addr7)
{
    RCC->CCIPR = (RCC->CCIPR & ~RCC_CCIPR_I2C1SEL) | RCC_CCIPR_I2C1SEL_1;   /* HSI16 */
    RCC->APB1ENR |= RCC_APB1ENR_I2C1EN;
    I2C1->CR1 = 0u;
    I2C1->TIMINGR = HB_I2C_TIMINGR;
    I2C1->OAR1 = 0u;
    I2C1->OAR1 = I2C_OAR1_OA1EN | ((uint32_t)addr7 << 1);
    I2C1->CR1 = I2C_CR1_PE | I2C_CR1_ADDRIE | I2C_CR1_RXIE | I2C_CR1_TXIE |
                I2C_CR1_STOPIE | I2C_CR1_NACKIE | I2C_CR1_ERRIE;
    NVIC_SetPriority(I2C1_IRQn, 0u);
    NVIC_EnableIRQ(I2C1_IRQn);
}

void I2C1_IRQHandler(void)
{
    uint32_t isr = I2C1->ISR;
    if (isr & I2C_ISR_ADDR) {
        bool rd = (isr & I2C_ISR_DIR) != 0u;
#ifdef L011_RELOCATED_APP
        write_frame = !rd;
        update_length = 0;
        update_match = true;
#endif
        if (rd) I2C1->ISR |= I2C_ISR_TXE;          /* drop any stale preloaded byte */
        htr_regs_i2c_begin(rd);
        I2C1->ICR = I2C_ICR_ADDRCF;
    }
    if (isr & I2C_ISR_RXNE) {
        uint8_t byte = (uint8_t)I2C1->RXDR;
#ifdef L011_RELOCATED_APP
        static const uint8_t key[] = {L011_ENTER_REG, L011_ENTER_KEY, L011_ENTER_KEY ^ 0xFFu};
        if (update_length >= sizeof(key) || byte != key[update_length]) update_match = false;
        if (update_length <= sizeof(key)) ++update_length;
        if (update_length <= 1u || !update_match)
#endif
        htr_regs_i2c_rx(byte);
    }
    if (isr & I2C_ISR_TXIS) I2C1->TXDR = htr_regs_i2c_tx();
    if (isr & I2C_ISR_NACKF) I2C1->ICR = I2C_ICR_NACKCF;
    /* Errors before STOP: a write that saw a bus error is dropped, not applied. */
    if (isr & (I2C_ISR_BERR | I2C_ISR_ARLO | I2C_ISR_OVR)) {
        I2C1->ICR = I2C_ICR_BERRCF | I2C_ICR_ARLOCF | I2C_ICR_OVRCF;
        htr_regs_i2c_error();
#ifdef L011_RELOCATED_APP
        update_match = false;
#endif
    }
    if (isr & I2C_ISR_STOPF) {
        I2C1->ICR = I2C_ICR_STOPCF;
        htr_regs_i2c_end();
#ifdef L011_RELOCATED_APP
        if (write_frame && update_match && update_length == 3u) update_requested = true;
#endif
    }
}
