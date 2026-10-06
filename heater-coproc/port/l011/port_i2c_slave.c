/* I2C1 slave at the G0B1-facing address. Byte-level events go straight to
   the portable register file (htr_regs_i2c_*). Clock stretching enabled. */
#include "port.h"
#include "htr_board.h"
#include "htr_regs.h"

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
        if (rd) I2C1->ISR |= I2C_ISR_TXE;          /* drop any stale preloaded byte */
        htr_regs_i2c_begin(rd);
        I2C1->ICR = I2C_ICR_ADDRCF;
    }
    if (isr & I2C_ISR_RXNE) htr_regs_i2c_rx((uint8_t)I2C1->RXDR);
    if (isr & I2C_ISR_TXIS) I2C1->TXDR = htr_regs_i2c_tx();
    if (isr & I2C_ISR_NACKF) I2C1->ICR = I2C_ICR_NACKCF;
    if (isr & I2C_ISR_STOPF) {
        I2C1->ICR = I2C_ICR_STOPCF;
        htr_regs_i2c_end();
    }
    if (isr & (I2C_ISR_BERR | I2C_ISR_ARLO | I2C_ISR_OVR)) {
        I2C1->ICR = I2C_ICR_BERRCF | I2C_ICR_ARLOCF | I2C_ICR_OVRCF;
    }
}
